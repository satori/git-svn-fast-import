/* Copyright (C) 2014 by Maxim Bublis <b@codemonkey.ru>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "parse.h"

#include "backend.h"
#include "trie.h"
#include "utils.h"

#include <apr_portable.h>
#include <svn_props.h>
#include <svn_repos.h>
#include <svn_time.h>
#include <svn_version.h>

#define BACK_FILENO 3

#define MODE_NORMAL     0100644
#define MODE_EXECUTABLE 0100755
#define MODE_SYMLINK    0120000
#define MODE_DIR        0040000

typedef struct
{
    apr_pool_t *pool;
    revision_t *rev;
    apr_array_header_t *nodes;
} revision_ctx_t;

typedef struct
{
    apr_pool_t *pool;
    backend_t backend;
    mark_t last_mark;
    git_svn_options_t *options;
    apr_hash_t *blobs;
    apr_hash_t *revisions;
    trie_t *branches;
    revision_ctx_t *rev_ctx;
    node_t *node;
} parser_ctx_t;

#if (SVN_VER_MAJOR == 1 && SVN_VER_MINOR > 7)
static svn_error_t *
magic_header_record(int version, void *ctx, apr_pool_t *pool)
{
    return SVN_NO_ERROR;
}
#endif

static svn_error_t *
uuid_record(const char *uuid, void *ctx, apr_pool_t *pool)
{
    return SVN_NO_ERROR;
}

static svn_error_t *
new_revision_record(void **r_ctx, apr_hash_t *headers, void *p_ctx, apr_pool_t *pool)
{
    const char *revnum;
    parser_ctx_t *ctx = p_ctx;
    ctx->rev_ctx = apr_pcalloc(pool, sizeof(revision_ctx_t));
    revision_t *rev = apr_pcalloc(ctx->pool, sizeof(*rev));

    rev->revnum = SVN_INVALID_REVNUM;
    rev->branch = NULL;

    revnum = apr_hash_get(headers, SVN_REPOS_DUMPFILE_REVISION_NUMBER, APR_HASH_KEY_STRING);
    if (revnum != NULL) {
        rev->revnum = SVN_STR_TO_REV(revnum);
    }

    ctx->rev_ctx->rev = rev;
    ctx->rev_ctx->nodes = apr_array_make(pool, 8, sizeof(node_t));
    ctx->rev_ctx->pool = pool;

    *r_ctx = ctx;

    return SVN_NO_ERROR;
}

static node_kind_t
get_node_kind(apr_hash_t *headers)
{
    const char *kind;
    kind = apr_hash_get(headers, SVN_REPOS_DUMPFILE_NODE_KIND, APR_HASH_KEY_STRING);
    if (kind != NULL) {
        if (strcmp(kind, "file") == 0) {
            return KIND_FILE;
        }
        else if (strcmp(kind, "dir") == 0) {
            return KIND_DIR;
        }
    }
    return KIND_UNKNOWN;
}

static node_action_t
get_node_action(apr_hash_t *headers)
{
    const char *action;
    action = apr_hash_get(headers, SVN_REPOS_DUMPFILE_NODE_ACTION, APR_HASH_KEY_STRING);
    if (action != NULL) {
        if (strcmp(action, "change") == 0) {
            return ACTION_CHANGE;
        }
        else if (strcmp(action, "add") == 0) {
            return ACTION_ADD;
        }
        else if (strcmp(action, "delete") == 0) {
            return ACTION_DELETE;
        }
        else if (strcmp(action, "replace") == 0) {
            return ACTION_REPLACE;
        }
    }
    return ACTION_NOOP;
}

static uint32_t
get_node_default_mode(node_kind_t kind)
{
    switch(kind) {
    case KIND_DIR:
        return MODE_DIR;
    default:
        return MODE_NORMAL;
    }
}

static revision_t *
get_node_copyfrom_rev(apr_hash_t *headers, parser_ctx_t *ctx)
{
    const char *copyfrom_revnum;
    revnum_t revnum;

    copyfrom_revnum = apr_hash_get(headers, SVN_REPOS_DUMPFILE_NODE_COPYFROM_REV, APR_HASH_KEY_STRING);
    if (copyfrom_revnum == NULL) {
        return NULL;
    }

    revnum = SVN_STR_TO_REV(copyfrom_revnum);

    return apr_hash_get(ctx->revisions, &revnum, sizeof(revnum_t));
}

static git_svn_status_t
get_node_blob(blob_t **dst, apr_hash_t *headers, parser_ctx_t *ctx)
{
    git_svn_status_t err;
    const char *content_sha1, *content_length;
    checksum_t checksum;
    blob_t *blob;

    content_sha1 = apr_hash_get(headers, SVN_REPOS_DUMPFILE_TEXT_CONTENT_SHA1, APR_HASH_KEY_STRING);
    if (content_sha1 == NULL) {
        content_sha1 = apr_hash_get(headers, SVN_REPOS_DUMPFILE_TEXT_COPY_SOURCE_SHA1, APR_HASH_KEY_STRING);
    }

    if (content_sha1 == NULL) {
        return GIT_SVN_SUCCESS;
    }

    err = hex_to_bytes(checksum, content_sha1, CHECKSUM_BYTES_LENGTH);
    if (err) {
        return err;
    }

    blob = apr_hash_get(ctx->blobs, checksum, sizeof(checksum_t));

    if (blob == NULL) {
        blob = apr_pcalloc(ctx->pool, sizeof(blob_t));
        memcpy(blob->checksum, checksum, sizeof(checksum_t));

        content_length = apr_hash_get(headers, SVN_REPOS_DUMPFILE_TEXT_CONTENT_LENGTH, APR_HASH_KEY_STRING);
        if (content_length != NULL) {
            blob->length = svn__atoui64(content_length);
        }

        apr_hash_set(ctx->blobs, blob->checksum, sizeof(checksum_t), blob);
    }

    *dst = blob;

    return GIT_SVN_SUCCESS;
}

static svn_error_t *
new_node_record(void **n_ctx, apr_hash_t *headers, void *r_ctx, apr_pool_t *pool)
{
    const char *path, *subpath;
    const char *copyfrom_path, *copyfrom_subpath;
    parser_ctx_t *ctx = r_ctx;
    revision_t *rev, *copyfrom_rev = NULL;
    node_t *node;
    node_action_t action;
    node_kind_t kind;
    branch_t *branch = NULL, *copyfrom_branch = NULL;
    git_svn_status_t err;

    rev = ctx->rev_ctx->rev;
    *n_ctx = ctx;

    path = apr_hash_get(headers, SVN_REPOS_DUMPFILE_NODE_PATH, APR_HASH_KEY_STRING);
    if (path == NULL) {
        return SVN_NO_ERROR;
    }

    copyfrom_rev = get_node_copyfrom_rev(headers, ctx);

    copyfrom_path = apr_hash_get(headers, SVN_REPOS_DUMPFILE_NODE_COPYFROM_PATH, APR_HASH_KEY_STRING);
    if (copyfrom_path != NULL) {
        copyfrom_branch = trie_find_exact(ctx->branches, copyfrom_path);
    }

    subpath = cstring_skip_prefix(path, ctx->options->branches);
    if (subpath == NULL) {
        subpath = cstring_skip_prefix(path, ctx->options->tags);
    }
    if (subpath != NULL) {
        if (*subpath == '/') {
            subpath++;
        }
        if (*subpath == '\0') {
            subpath = NULL;
        }
    }

    if (copyfrom_branch != NULL && subpath != NULL) {
        if (copyfrom_rev != NULL) {
            rev->copyfrom = copyfrom_rev;
        }
        branch = apr_pcalloc(ctx->pool, sizeof(branch_t));
        branch->name = apr_pstrdup(ctx->pool, subpath);
        branch->path = apr_pstrdup(ctx->pool, path);
        backend_notify_branch_found(&ctx->backend, branch, pool);
        trie_insert(ctx->branches, branch->path, branch);
    }

    if (copyfrom_branch == NULL && copyfrom_path != NULL) {
        copyfrom_branch = trie_find_longest_prefix(ctx->branches, copyfrom_path);
    }

    if (branch == NULL) {
        branch = trie_find_longest_prefix(ctx->branches, path);
    }
    if (branch == NULL && subpath != NULL) {
        const char *branch_root;
        branch_root = strchr(subpath, '/');
        branch = apr_pcalloc(ctx->pool, sizeof(branch_t));
        if (branch_root != NULL) {
            branch->name = apr_pstrndup(ctx->pool, subpath, branch_root - subpath);
            branch->path = apr_pstrndup(ctx->pool, path, branch_root - path);
        }
        else {
            branch->name = apr_pstrdup(ctx->pool, subpath);
            branch->path = apr_pstrdup(ctx->pool, path);
        }
        backend_notify_branch_found(&ctx->backend, branch, pool);
        trie_insert(ctx->branches, branch->path, branch);
    }

    if (branch == NULL) {
        return SVN_NO_ERROR;
    }

    rev->branch = branch;

    kind = get_node_kind(headers);
    action = get_node_action(headers);

    if (kind == KIND_DIR && action == ACTION_ADD && copyfrom_path == NULL) {
        return SVN_NO_ERROR;
    }

    node = &APR_ARRAY_PUSH(ctx->rev_ctx->nodes, node_t);
    node->mode = get_node_default_mode(kind);
    node->kind = kind;
    node->action = action;
    node->path = "";

    subpath = cstring_skip_prefix(path, branch->path);
    if (subpath != NULL) {
        if (*subpath == '/') {
            subpath++;
        }
        node->path = apr_pstrdup(ctx->rev_ctx->pool, subpath);
    }

    if (node->kind == KIND_FILE) {
        err = get_node_blob(&node->content.data.blob, headers, ctx);
        if (err) {
            return svn_error_create(SVN_ERR_BASE, NULL, NULL);
        }
        if (node->content.data.blob != NULL) {
            node->content.kind = CONTENT_BLOB;
        }
    }

    if (node->content.kind == CONTENT_UNKNOWN && copyfrom_branch != NULL) {
        copyfrom_subpath = cstring_skip_prefix(copyfrom_path, copyfrom_branch->path);
        if (*copyfrom_subpath == '/') {
            copyfrom_subpath++;
        }

        if (copyfrom_rev != NULL && strcmp(copyfrom_rev->branch->name, copyfrom_branch->name) == 0) {
            err = backend_get_checksum(&ctx->backend, node->content.data.checksum, copyfrom_rev, copyfrom_subpath, ctx->rev_ctx->pool);
        }
        else {
            err = backend_get_checksum(&ctx->backend, node->content.data.checksum, copyfrom_branch->last_rev, copyfrom_subpath, ctx->rev_ctx->pool);
        }

        if (err) {
            return svn_error_create(SVN_ERR_BASE, NULL, NULL);
        }
        node->content.kind = CONTENT_CHECKSUM;
    }

    ctx->node = node;

    return SVN_NO_ERROR;
}

static svn_error_t *
set_revision_property(void *r_ctx, const char *name, const svn_string_t *value)
{
    parser_ctx_t *ctx = r_ctx;
    revision_t *rev = ctx->rev_ctx->rev;
    apr_pool_t *pool = ctx->rev_ctx->pool;

    // It is safe to ignore revision 0 properties
    if (rev->revnum == 0) {
        return SVN_NO_ERROR;
    }

    if (strcmp(name, SVN_PROP_REVISION_AUTHOR) == 0) {
        rev->author = apr_pstrdup(pool, value->data);
    }
    else if (strcmp(name, SVN_PROP_REVISION_DATE) == 0) {
        apr_time_t timestamp;
        svn_time_from_cstring(&timestamp, value->data, pool);
        rev->timestamp = apr_time_sec(timestamp);
    }
    else if (strcmp(name, SVN_PROP_REVISION_LOG) == 0) {
        rev->message = apr_pstrdup(pool, value->data);
    }

    return SVN_NO_ERROR;
}

static svn_error_t *
set_node_property(void *n_ctx, const char *name, const svn_string_t *value)
{
    parser_ctx_t *ctx = n_ctx;
    node_t *node = ctx->node;

    if (node == NULL) {
        return SVN_NO_ERROR;
    }

    if (strcmp(name, SVN_PROP_EXECUTABLE) == 0) {
        node->mode = MODE_EXECUTABLE;
    }
    else if (strcmp(name, SVN_PROP_SPECIAL) == 0) {
        node->mode = MODE_SYMLINK;
    }

    return SVN_NO_ERROR;
}

static svn_error_t *
remove_node_props(void *n_ctx)
{
    parser_ctx_t *ctx = n_ctx;
    node_t *node = ctx->node;

    if (node == NULL) {
        return SVN_NO_ERROR;
    }

    node->mode = get_node_default_mode(node->kind);

    return SVN_NO_ERROR;
}

static svn_error_t *
delete_node_property(void *n_ctx, const char *name)
{
    parser_ctx_t *ctx = n_ctx;
    node_t *node = ctx->node;

    if (node == NULL) {
        return SVN_NO_ERROR;
    }

    if (strcmp(name, SVN_PROP_EXECUTABLE) == 0 || strcmp(name, SVN_PROP_SPECIAL) == 0) {
        node->mode = MODE_NORMAL;
    }

    return SVN_NO_ERROR;
}

static svn_error_t *
set_fulltext(svn_stream_t **stream, void *n_ctx)
{
    git_svn_status_t err;
    parser_ctx_t *ctx = n_ctx;
    node_t *node = ctx->node;

    if (node == NULL) {
        return SVN_NO_ERROR;
    }

    blob_t *blob = node->content.data.blob;
    apr_pool_t *pool = ctx->rev_ctx->pool;

    if (blob->mark) {
        // Blob has been uploaded, if we've already assigned mark to it
        return SVN_NO_ERROR;
    }

    blob->mark = ctx->last_mark++;
    SVN_ERR(svn_stream_for_stdout(stream, pool));

    err = backend_write_blob_header(&ctx->backend, blob, pool);
    if (err) {
        return svn_error_create(SVN_ERR_BASE, NULL, NULL);
    }

    return SVN_NO_ERROR;
}

static svn_error_t *
apply_textdelta(svn_txdelta_window_handler_t *handler, void **handler_baton, void *n_ctx)
{
    // TODO
    return SVN_NO_ERROR;
}

static svn_error_t *
close_node(void *n_ctx)
{
    parser_ctx_t *ctx = n_ctx;
    ctx->node = NULL;

    return SVN_NO_ERROR;
}

static svn_error_t *
close_revision(void *r_ctx)
{
    git_svn_status_t err;
    parser_ctx_t *ctx = r_ctx;
    revision_t *rev = ctx->rev_ctx->rev;
    apr_pool_t *pool = ctx->rev_ctx->pool;

    if (rev->revnum == 0 || rev->branch == NULL) {
        err = backend_notify_skip_revision(&ctx->backend, rev, pool);
        if (err) {
            return svn_error_create(SVN_ERR_BASE, NULL, NULL);
        }
        return SVN_NO_ERROR;
    }

    rev->mark = ctx->last_mark++;

    err = backend_write_revision(&ctx->backend, rev, ctx->rev_ctx->nodes, pool);
    if (err) {
        return svn_error_create(SVN_ERR_BASE, NULL, NULL);
    }

    apr_hash_set(ctx->revisions, &rev->revnum, sizeof(revnum_t), rev);

    rev->branch->last_rev = rev;

    ctx->rev_ctx = NULL;

    return SVN_NO_ERROR;
}

static svn_error_t *
check_cancel(void *ctx)
{
    return SVN_NO_ERROR;
}

#if (SVN_VER_MAJOR == 1 && SVN_VER_MINOR > 7)
static const svn_repos_parse_fns3_t callbacks = {
    magic_header_record,
    uuid_record,
    new_revision_record,
#else
static const svn_repos_parse_fns2_t callbacks = {
    new_revision_record,
    uuid_record,
#endif
    new_node_record,
    set_revision_property,
    set_node_property,
    delete_node_property,
    remove_node_props,
    set_fulltext,
    apply_textdelta,
    close_node,
    close_revision
};

git_svn_status_t
git_svn_parse_dumpstream(git_svn_options_t *options, apr_pool_t *pool)
{
    apr_file_t *back_file;
    apr_status_t apr_err;
    svn_error_t *svn_err;
    svn_stream_t *input;
    int back_fd = BACK_FILENO;

    // Read the input from stdin
    svn_err = svn_stream_for_stdin(&input, pool);
    if (svn_err != NULL) {
        handle_svn_error(svn_err);
        return GIT_SVN_FAILURE;
    }

    parser_ctx_t ctx = {};
    ctx.pool = pool;
    ctx.blobs = apr_hash_make(pool);
    ctx.revisions = apr_hash_make(pool);
    ctx.last_mark = 1;
    ctx.branches = trie_create(pool);

    branch_t *master = apr_pcalloc(pool, sizeof(*master));
    master->name = "master";
    master->path = options->trunk;

    trie_insert(ctx.branches, master->path, master);
    ctx.options = options;

    // Write to stdout
    svn_err = svn_stream_for_stdout(&ctx.backend.out, pool);
    if (svn_err != NULL) {
        handle_svn_error(svn_err);
        return GIT_SVN_FAILURE;
    }

    // Read backend answers
    apr_err = apr_os_file_put(&back_file, &back_fd, APR_FOPEN_READ, pool);
    if (apr_err) {
        return GIT_SVN_FAILURE;
    }

    ctx.backend.back = svn_stream_from_aprfile2(back_file, FALSE, pool);

#if (SVN_VER_MAJOR == 1 && SVN_VER_MINOR > 7)
    svn_err = svn_repos_parse_dumpstream3(input, &callbacks, &ctx, FALSE, check_cancel, NULL, pool);
#else
    svn_err = svn_repos_parse_dumpstream2(input, &callbacks, &ctx, check_cancel, NULL, pool);
#endif
    if (svn_err != NULL) {
        handle_svn_error(svn_err);
        return GIT_SVN_FAILURE;
    }

    return GIT_SVN_SUCCESS;
}
