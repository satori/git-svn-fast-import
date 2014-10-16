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
#include "tree.h"
#include "utils.h"

#include <apr_portable.h>
#include <svn_props.h>
#include <svn_repos.h>
#include <svn_time.h>
#include <svn_version.h>

#define OUT_FILENO 1
#define BACK_FILENO 3

#define MODE_NORMAL     0100644
#define MODE_EXECUTABLE 0100755
#define MODE_SYMLINK    0120000
#define MODE_DIR        0040000

typedef struct
{
    // Subversion revision number
    revnum_t revnum;
    // branch_t to commit_t mapping
    apr_hash_t *commits;
} revision_t;

typedef struct
{
    apr_pool_t *pool;
    const char *message;
    const char *author;
    int64_t timestamp;
    revision_t *rev;
    // commit_t to apr_array_header_t mapping
    apr_hash_t *nodes;
} revision_ctx_t;

typedef struct
{
    apr_pool_t *pool;
    backend_t backend;
    mark_t last_mark;
    git_svn_options_t *options;
    apr_hash_t *blobs;
    apr_hash_t *revisions;
    tree_t *branches;
    revision_ctx_t *rev_ctx;
    node_t *node;
} parser_ctx_t;

static svn_error_t *
svn_generic_error() {
    return svn_error_create(SVN_ERR_BASE, NULL, NULL);
}

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

    revision_t *rev = apr_pcalloc(ctx->pool, sizeof(revision_t));
    rev->revnum = SVN_INVALID_REVNUM;
    rev->commits = apr_hash_make(ctx->pool);

    revnum = apr_hash_get(headers, SVN_REPOS_DUMPFILE_REVISION_NUMBER, APR_HASH_KEY_STRING);
    if (revnum != NULL) {
        rev->revnum = SVN_STR_TO_REV(revnum);
    }

    ctx->rev_ctx->rev = rev;
    ctx->rev_ctx->nodes = apr_hash_make(pool);
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

static commit_t *
get_copyfrom_commit(apr_hash_t *headers, parser_ctx_t *ctx, branch_t *copyfrom_branch)
{
    const char *copyfrom_revnum;
    revnum_t revnum;
    revision_t *rev;

    copyfrom_revnum = apr_hash_get(headers, SVN_REPOS_DUMPFILE_NODE_COPYFROM_REV, APR_HASH_KEY_STRING);
    if (copyfrom_revnum == NULL) {
        return NULL;
    }

    revnum = SVN_STR_TO_REV(copyfrom_revnum);

    rev = apr_hash_get(ctx->revisions, &revnum, sizeof(revnum_t));
    if (rev != NULL) {
        commit_t *commit;
        commit = apr_hash_get(rev->commits, copyfrom_branch, sizeof(branch_t *));

        if (commit != NULL && strcmp(commit->branch->name, copyfrom_branch->name) == 0) {
            return commit;
        }
    }

    return copyfrom_branch->last_commit;
}

static git_svn_status_t
get_node_blob(blob_t **dst, apr_hash_t *headers, parser_ctx_t *ctx)
{
    git_svn_status_t err;
    const char *content_sha1, *content_length;
    checksum_t checksum;
    blob_t *blob;
    int is_copyfrom = 0;

    content_sha1 = apr_hash_get(headers, SVN_REPOS_DUMPFILE_TEXT_CONTENT_SHA1, APR_HASH_KEY_STRING);
    if (content_sha1 == NULL) {
        content_sha1 = apr_hash_get(headers, SVN_REPOS_DUMPFILE_TEXT_COPY_SOURCE_SHA1, APR_HASH_KEY_STRING);
        is_copyfrom = 1;
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
        // Do not create new blobs for copied paths.
        if (is_copyfrom) {
            return GIT_SVN_SUCCESS;
        }
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

static branch_t *
find_branch(const parser_ctx_t *ctx, const char *path)
{
    branch_t *branch;
    const char *prefix, *root, *subpath;

    branch = (branch_t *) tree_find_longest_prefix(ctx->branches, path);
    if (branch != NULL) {
        return branch;
    }

    prefix = tree_find_longest_prefix(ctx->options->branches_pfx, path);
    if (prefix == NULL) {
        prefix = tree_find_longest_prefix(ctx->options->tags_pfx, path);
    }

    if (prefix == NULL) {
        return NULL;
    }

    subpath = cstring_skip_prefix(path, prefix);
    if (subpath == NULL) {
        return NULL;
    }

    if (*subpath == '/') {
        subpath++;
    }

    if (*subpath == '\0') {
        return NULL;
    }

    branch = apr_pcalloc(ctx->pool, sizeof(branch_t));

    root = strchr(subpath, '/');
    if (root == NULL) {
        branch->name = apr_pstrdup(ctx->pool, subpath);
        branch->path = apr_pstrdup(ctx->pool, path);
        return branch;
    }

    branch->name = apr_pstrndup(ctx->pool, subpath, root - subpath);
    branch->path = apr_pstrndup(ctx->pool, path, root - path);

    return branch;
}

static git_svn_status_t
store_branch(parser_ctx_t *ctx, branch_t *branch)
{
    git_svn_status_t err;

    if (branch->is_saved) {
        return GIT_SVN_SUCCESS;
    }

    err = backend_notify_branch_found(&ctx->backend, branch);
    if (err) {
        return err;
    }

    tree_insert(ctx->branches, branch->path, branch);
    branch->is_saved = 1;

    return GIT_SVN_SUCCESS;
}

static svn_error_t *
new_node_record(void **n_ctx, apr_hash_t *headers, void *r_ctx, apr_pool_t *pool)
{
    const char *path, *subpath;
    const char *copyfrom_path;
    parser_ctx_t *ctx = r_ctx;
    revision_t *rev = ctx->rev_ctx->rev;
    commit_t *commit, *copyfrom_commit = NULL;
    branch_t *branch = NULL, *copyfrom_branch = NULL;
    apr_array_header_t *nodes;
    node_t *node;
    node_action_t action = get_node_action(headers);
    node_kind_t kind = get_node_kind(headers);
    git_svn_status_t err;

    *n_ctx = ctx;

    path = apr_hash_get(headers, SVN_REPOS_DUMPFILE_NODE_PATH, APR_HASH_KEY_STRING);
    if (path == NULL) {
        return SVN_NO_ERROR;
    }

    copyfrom_path = apr_hash_get(headers, SVN_REPOS_DUMPFILE_NODE_COPYFROM_PATH, APR_HASH_KEY_STRING);
    if (copyfrom_path != NULL) {
        copyfrom_branch = (branch_t *) tree_find_longest_prefix(ctx->branches, copyfrom_path);
    }

    branch = find_branch(ctx, path);
    if (branch == NULL) {
        return SVN_NO_ERROR;
    }

    if (kind == KIND_DIR && action == ACTION_ADD && copyfrom_path == NULL) {
        return SVN_NO_ERROR;
    }

    err = store_branch(ctx, branch);
    if (err) {
        return svn_generic_error();
    }

    commit = apr_hash_get(rev->commits, branch, sizeof(branch_t *));
    if (commit == NULL) {
        commit = apr_pcalloc(ctx->pool, sizeof(commit_t));
        commit->branch = branch;
        apr_hash_set(rev->commits, commit->branch, sizeof(branch_t *), commit);
    }

    nodes = apr_hash_get(ctx->rev_ctx->nodes, commit, sizeof(commit_t *));
    if (nodes == NULL) {
        nodes = apr_array_make(ctx->rev_ctx->pool, 8, sizeof(node_t));
        apr_hash_set(ctx->rev_ctx->nodes, commit, sizeof(commit_t *), nodes);
    }

    node = &APR_ARRAY_PUSH(nodes, node_t);
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
            return svn_generic_error();
        }
        if (node->content.data.blob != NULL) {
            node->content.kind = CONTENT_BLOB;
        }
    }

    if (node->content.kind == CONTENT_UNKNOWN && copyfrom_branch != NULL) {
        copyfrom_commit = get_copyfrom_commit(headers, ctx, copyfrom_branch);

        if (copyfrom_commit != NULL) {
            const char *copyfrom_subpath;

            copyfrom_subpath = cstring_skip_prefix(copyfrom_path, copyfrom_branch->path);
            if (*copyfrom_subpath == '/') {
                copyfrom_subpath++;
            }

            if (*copyfrom_subpath == '\0') {
                commit->copyfrom = copyfrom_commit;
            }

            err = backend_get_checksum(&ctx->backend, node->content.data.checksum, copyfrom_commit, copyfrom_subpath);
            if (!err) {
                node->content.kind = CONTENT_CHECKSUM;
            }
        }
    }

    ctx->node = node;

    return SVN_NO_ERROR;
}

static svn_error_t *
set_revision_property(void *r_ctx, const char *name, const svn_string_t *value)
{
    parser_ctx_t *ctx = r_ctx;
    apr_pool_t *pool = ctx->rev_ctx->pool;

    if (strcmp(name, SVN_PROP_REVISION_AUTHOR) == 0) {
        ctx->rev_ctx->author = apr_pstrdup(pool, value->data);
    }
    else if (strcmp(name, SVN_PROP_REVISION_DATE) == 0) {
        apr_time_t timestamp;
        svn_time_from_cstring(&timestamp, value->data, pool);
        ctx->rev_ctx->timestamp = apr_time_sec(timestamp);
    }
    else if (strcmp(name, SVN_PROP_REVISION_LOG) == 0) {
        ctx->rev_ctx->message = apr_pstrdup(pool, value->data);
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

    if (blob->mark) {
        // Blob has been uploaded, if we've already assigned mark to it
        return SVN_NO_ERROR;
    }

    blob->mark = ctx->last_mark++;
    SVN_ERR(svn_stream_for_stdout(stream, ctx->rev_ctx->pool));

    err = backend_write_blob_header(&ctx->backend, blob);
    if (err) {
        return svn_generic_error();
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
    revision_ctx_t *rev_ctx = ctx->rev_ctx;
    revision_t *rev = rev_ctx->rev;
    apr_hash_index_t *idx;

    if (rev->revnum == 0 || apr_hash_count(rev->commits) == 0) {
        err = backend_notify_revision_skipped(&ctx->backend, rev->revnum);
        if (err) {
            return svn_generic_error();
        }
        return SVN_NO_ERROR;
    }

    for (idx = apr_hash_first(ctx->rev_ctx->pool, rev->commits); idx; idx = apr_hash_next(idx)) {
        commit_t *commit;
        const void *key;
        ssize_t keylen = sizeof(branch_t *);
        void *value;

        apr_hash_this(idx, &key, &keylen, &value);
        commit = value;

        apr_array_header_t *nodes = apr_hash_get(ctx->rev_ctx->nodes, commit, sizeof(commit_t *));

        commit->mark = ctx->last_mark++;

        err = backend_write_commit(&ctx->backend, commit, nodes, rev_ctx->author, rev_ctx->message, rev_ctx->timestamp);
        if (err) {
            return svn_generic_error();
        }

        err = backend_notify_branch_updated(&ctx->backend, commit->branch);
        if (err) {
            return svn_generic_error();
        }

        commit->branch->last_commit = commit;
    }

    err = backend_notify_revision_imported(&ctx->backend, rev->revnum);
    if (err) {
        return svn_generic_error();
    }

    apr_hash_set(ctx->revisions, &rev->revnum, sizeof(revnum_t), rev);
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
    svn_error_t *svn_err;
    svn_stream_t *input;

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
    ctx.branches = tree_create(pool);

    branch_t *master = apr_pcalloc(pool, sizeof(*master));
    master->name = "master";
    master->path = options->trunk;

    tree_insert(ctx.branches, master->path, master);
    ctx.options = options;

    ctx.backend.verbose = options->verbose;

    // Write to stdout
    ctx.backend.out = OUT_FILENO;
    // Read backend answers
    ctx.backend.back = BACK_FILENO;

#if (SVN_VER_MAJOR == 1 && SVN_VER_MINOR > 7)
    svn_err = svn_repos_parse_dumpstream3(input, &callbacks, &ctx, FALSE, check_cancel, NULL, pool);
#else
    svn_err = svn_repos_parse_dumpstream2(input, &callbacks, &ctx, check_cancel, NULL, pool);
#endif
    if (svn_err != NULL) {
        handle_svn_error(svn_err);
        return GIT_SVN_FAILURE;
    }

    backend_finished(&ctx.backend);

    return GIT_SVN_SUCCESS;
}
