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

#include "dump.h"
#include "trie.h"

#include <svn_props.h>
#include <svn_repos.h>
#include <svn_time.h>
#include <svn_version.h>

#define GIT_SVN_NODE_MODE_NORMAL     0100644
#define GIT_SVN_NODE_MODE_EXECUTABLE 0100755
#define GIT_SVN_NODE_MODE_SYMLINK    0120000

typedef struct
{
    apr_pool_t *pool;
    git_svn_revision_t *rev;
    apr_array_header_t *nodes;
} revision_ctx_t;

typedef struct
{
    apr_pool_t *pool;
    svn_stream_t *output;
    git_svn_mark_t last_mark;
    git_svn_options_t *options;
    apr_hash_t *blobs;
    apr_hash_t *revisions;
    git_svn_trie_t *branches;
    revision_ctx_t *rev_ctx;
    git_svn_node_t *node;
} parser_ctx_t;


static const char *
cstring_skip_prefix(const char * str, const char *prefix)
{
    size_t len = strlen(prefix);

    if (strncmp(str, prefix, len) == 0) {
        return str + len;
    }

    return NULL;
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
    git_svn_revision_t *rev = apr_pcalloc(ctx->pool, sizeof(*rev));

    rev->revnum = SVN_INVALID_REVNUM;
    rev->branch = NULL;

    revnum = apr_hash_get(headers, SVN_REPOS_DUMPFILE_REVISION_NUMBER, APR_HASH_KEY_STRING);
    if (revnum != NULL) {
        rev->revnum = SVN_STR_TO_REV(revnum);
    }

    ctx->rev_ctx->rev = rev;
    ctx->rev_ctx->nodes = apr_array_make(pool, 8, sizeof(git_svn_node_t));
    ctx->rev_ctx->pool = pool;

    *r_ctx = ctx;

    return SVN_NO_ERROR;
}

static git_svn_node_kind_t
get_node_kind(apr_hash_t *headers)
{
    const char *kind;
    kind = apr_hash_get(headers, SVN_REPOS_DUMPFILE_NODE_KIND, APR_HASH_KEY_STRING);
    if (kind != NULL) {
        if (strcmp(kind, "file") == 0) {
            return GIT_SVN_NODE_FILE;
        }
        else if (strcmp(kind, "dir") == 0) {
            return GIT_SVN_NODE_DIR;
        }
    }
    return GIT_SVN_NODE_UNKNOWN;
}

static git_svn_node_action_t
get_node_action(apr_hash_t *headers)
{
    const char *action;
    action = apr_hash_get(headers, SVN_REPOS_DUMPFILE_NODE_ACTION, APR_HASH_KEY_STRING);
    if (action != NULL) {
        if (strcmp(action, "change") == 0) {
            return GIT_SVN_ACTION_CHANGE;
        }
        else if (strcmp(action, "add") == 0) {
            return GIT_SVN_ACTION_ADD;
        }
        else if (strcmp(action, "delete") == 0) {
            return GIT_SVN_ACTION_DELETE;
        }
        else if (strcmp(action, "replace") == 0) {
            return GIT_SVN_ACTION_REPLACE;
        }
    }
    return GIT_SVN_ACTION_NOOP;
}

static svn_error_t *
new_node_record(void **n_ctx, apr_hash_t *headers, void *r_ctx, apr_pool_t *pool)
{
    const char *value, *branch_root;
    const char *path, *subpath;
    const char *copyfrom_revnum;
    parser_ctx_t *ctx = r_ctx;
    git_svn_revision_t *rev, *copyfrom_rev = NULL;
    git_svn_node_t *node;
    git_svn_branch_t *branch;

    rev = ctx->rev_ctx->rev;
    *n_ctx = ctx;

    path = apr_hash_get(headers, SVN_REPOS_DUMPFILE_NODE_PATH, APR_HASH_KEY_STRING);
    if (path == NULL) {
        return SVN_NO_ERROR;
    }

    copyfrom_revnum = apr_hash_get(headers, SVN_REPOS_DUMPFILE_NODE_COPYFROM_REV, APR_HASH_KEY_STRING);
    if (copyfrom_revnum != NULL) {
        git_svn_revnum_t revnum = SVN_STR_TO_REV(copyfrom_revnum);
        copyfrom_rev = apr_hash_get(ctx->revisions, &revnum, sizeof(git_svn_revnum_t));
    }

    branch = git_svn_trie_find_prefix(ctx->branches, path);
    if (branch == NULL) {
        subpath = cstring_skip_prefix(path, ctx->options->branches);
        if (subpath == NULL) {
            subpath = cstring_skip_prefix(path, ctx->options->tags);
        }
        if (subpath != NULL && *subpath != '\0') {
            if (*subpath == '/') {
                subpath++;
            }
            branch_root = strchr(subpath, '/');
            branch = apr_pcalloc(ctx->pool, sizeof(*branch));
            if (branch_root != NULL) {
                branch->name = apr_pstrndup(ctx->pool, subpath, branch_root - subpath);
                branch->path = apr_pstrndup(ctx->pool, path, branch_root - path);
            }
            else {
                branch->name = apr_pstrdup(ctx->pool, subpath);
                branch->path = apr_pstrdup(ctx->pool, path);
            }
            git_svn_dump_branch_found(ctx->output, branch, pool);
            git_svn_trie_insert(ctx->branches, branch->path, branch);
        }
    }

    if (branch == NULL) {
        return SVN_NO_ERROR;
    }

    rev->branch = branch;

    node = &APR_ARRAY_PUSH(ctx->rev_ctx->nodes, git_svn_node_t);
    node->mode = GIT_SVN_NODE_MODE_NORMAL;
    node->kind = get_node_kind(headers);
    node->action = get_node_action(headers);
    node->path = "";

    subpath = cstring_skip_prefix(path, branch->path);
    if (subpath != NULL) {
        if (*subpath == '/') {
            subpath++;
        }
        node->path = apr_pstrdup(ctx->rev_ctx->pool, subpath);
    }

    if (copyfrom_rev != NULL && node->kind == GIT_SVN_NODE_DIR && node->action == GIT_SVN_ACTION_ADD && *node->path == '\0') {
        rev->copyfrom = copyfrom_rev;
    }

    value = apr_hash_get(headers, SVN_REPOS_DUMPFILE_TEXT_CONTENT_SHA1, APR_HASH_KEY_STRING);
    if (value == NULL) {
        value = apr_hash_get(headers, SVN_REPOS_DUMPFILE_TEXT_COPY_SOURCE_SHA1, APR_HASH_KEY_STRING);
    }

    if (value != NULL) {
        git_svn_blob_t *blob = apr_hash_get(ctx->blobs, value, APR_HASH_KEY_STRING);

        if (blob == NULL) {
            blob = apr_pcalloc(ctx->pool, sizeof(*blob));
            blob->checksum = apr_pstrdup(ctx->pool, value);

            value = apr_hash_get(headers, SVN_REPOS_DUMPFILE_TEXT_CONTENT_LENGTH, APR_HASH_KEY_STRING);
            if (value != NULL) {
                blob->length = svn__atoui64(value);
            }

            apr_hash_set(ctx->blobs, blob->checksum, APR_HASH_KEY_STRING, blob);
        }

        node->blob = blob;
    }

    ctx->node = node;

    return SVN_NO_ERROR;
}

static svn_error_t *
set_revision_property(void *r_ctx, const char *name, const svn_string_t *value)
{
    parser_ctx_t *ctx = r_ctx;
    git_svn_revision_t *rev = ctx->rev_ctx->rev;
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
    git_svn_node_t *node = ctx->node;

    if (node == NULL) {
        return SVN_NO_ERROR;
    }

    if (strcmp(name, SVN_PROP_EXECUTABLE) == 0) {
        node->mode = GIT_SVN_NODE_MODE_EXECUTABLE;
    }
    else if (strcmp(name, SVN_PROP_SPECIAL) == 0) {
        node->mode = GIT_SVN_NODE_MODE_SYMLINK;
    }

    return SVN_NO_ERROR;
}

static svn_error_t *
remove_node_props(void *n_ctx)
{
    parser_ctx_t *ctx = n_ctx;
    git_svn_node_t *node = ctx->node;

    if (node == NULL) {
        return SVN_NO_ERROR;
    }

    node->mode = GIT_SVN_NODE_MODE_NORMAL;

    return SVN_NO_ERROR;
}

static svn_error_t *
delete_node_property(void *n_ctx, const char *name)
{
    parser_ctx_t *ctx = n_ctx;
    git_svn_node_t *node = ctx->node;

    if (node == NULL) {
        return SVN_NO_ERROR;
    }

    if (strcmp(name, SVN_PROP_EXECUTABLE) == 0 || strcmp(name, SVN_PROP_SPECIAL) == 0) {
        node->mode = GIT_SVN_NODE_MODE_NORMAL;
    }

    return SVN_NO_ERROR;
}

static svn_error_t *
set_fulltext(svn_stream_t **stream, void *n_ctx)
{
    parser_ctx_t *ctx = n_ctx;
    git_svn_node_t *node = ctx->node;

    if (node == NULL) {
        return SVN_NO_ERROR;
    }

    git_svn_blob_t *blob = node->blob;
    apr_pool_t *pool = ctx->rev_ctx->pool;

    if (blob->mark) {
        // Blob has been uploaded, if we've already assigned mark to it
        return SVN_NO_ERROR;
    }

    blob->mark = ctx->last_mark++;
    SVN_ERR(svn_stream_for_stdout(stream, pool));
    SVN_ERR(git_svn_dump_blob_header(*stream, blob, pool));

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
    parser_ctx_t *ctx = r_ctx;
    git_svn_revision_t *rev = ctx->rev_ctx->rev;
    apr_pool_t *pool = ctx->rev_ctx->pool;
    svn_stream_t *out = ctx->output;

    if (rev->revnum == 0 || rev->branch == NULL) {
        SVN_ERR(git_svn_dump_revision_noop(out, rev, pool));
        return SVN_NO_ERROR;
    }

    rev->mark = ctx->last_mark++;

    SVN_ERR(git_svn_dump_revision_begin(out, rev, pool));

    for (int i = 0; i < ctx->rev_ctx->nodes->nelts; i++) {
        git_svn_node_t *node = &APR_ARRAY_IDX(ctx->rev_ctx->nodes, i, git_svn_node_t);
        SVN_ERR(git_svn_dump_node(out, node, pool));
    }

    SVN_ERR(git_svn_dump_revision_end(out, rev, pool));

    apr_hash_set(ctx->revisions, &rev->revnum, sizeof(git_svn_revnum_t), rev);

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
    svn_error_t *err;

    svn_stream_t *input;
    // Read the input from stdin
    err = svn_stream_for_stdin(&input, pool);
    if (err != NULL) {
        return GIT_SVN_FAILURE;
    }

    parser_ctx_t ctx = {};
    ctx.pool = pool;
    ctx.blobs = apr_hash_make(pool);
    ctx.revisions = apr_hash_make(pool);
    ctx.last_mark = 1;
    ctx.branches = git_svn_trie_create(pool);

    git_svn_branch_t *master = apr_pcalloc(pool, sizeof(*master));
    master->name = "master";
    master->path = options->trunk;

    git_svn_trie_insert(ctx.branches, master->path, master);
    ctx.options = options;

    // Write to stdout
    err = svn_stream_for_stdout(&ctx.output, pool);
    if (err != NULL) {
        return GIT_SVN_FAILURE;
    }

#if (SVN_VER_MAJOR == 1 && SVN_VER_MINOR > 7)
    err = svn_repos_parse_dumpstream3(input, &callbacks, &ctx, FALSE, check_cancel, NULL, pool);
#else
    err = svn_repos_parse_dumpstream2(input, &callbacks, &ctx, check_cancel, NULL, pool);
#endif
    if (err != NULL) {
        return GIT_SVN_FAILURE;
    }

    return GIT_SVN_SUCCESS;
}
