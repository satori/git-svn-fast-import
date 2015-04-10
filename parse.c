/* Copyright (C) 2014-2015 by Maxim Bublis <b@codemonkey.ru>
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
#include "author.h"
#include "backend.h"
#include "branch.h"
#include "symlink.h"
#include "tree.h"
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

static const int one = 1;
static const void *NOT_NULL = &one;

typedef struct
{
    // Subversion revision number
    revnum_t revnum;
    // branch_t to commit_t mapping
    apr_hash_t *commits;
    // branch_t remove set
    apr_hash_t *remove;
} revision_t;

typedef struct
{
    apr_pool_t *pool;
    const char *message;
    const author_t *author;
    int64_t timestamp;
    revision_t *rev;
    // commit_t to apr_array_header_t mapping
    apr_hash_t *nodes;
} revision_ctx_t;

typedef struct
{
    apr_pool_t *pool;
    backend_t backend;
    // Branch storage.
    branch_storage_t *bs;
    mark_t last_mark;
    git_svn_options_t *options;
    apr_hash_t *authors;
    apr_hash_t *revisions;
    apr_hash_t *blobs;
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

    revision_t *rev = apr_pcalloc(ctx->pool, sizeof(revision_t));
    rev->revnum = SVN_INVALID_REVNUM;
    rev->commits = apr_hash_make(ctx->pool);
    rev->remove = apr_hash_make(ctx->pool);

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

static const commit_t *
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

        if (commit != NULL) {
            return commit;
        }
    }

    return branch_head_get(copyfrom_branch);
}

static svn_error_t *
get_node_blob(blob_t **dst, apr_hash_t *headers, parser_ctx_t *ctx)
{
    const char *content_sha1, *content_length;
    svn_checksum_t *checksum;
    blob_t *blob;
    int is_copyfrom = 0;

    content_sha1 = apr_hash_get(headers, SVN_REPOS_DUMPFILE_TEXT_CONTENT_SHA1, APR_HASH_KEY_STRING);
    if (content_sha1 == NULL) {
        content_sha1 = apr_hash_get(headers, SVN_REPOS_DUMPFILE_TEXT_COPY_SOURCE_SHA1, APR_HASH_KEY_STRING);
        is_copyfrom = 1;
    }

    if (content_sha1 == NULL) {
        return SVN_NO_ERROR;
    }

    SVN_ERR(svn_checksum_parse_hex(&checksum, svn_checksum_sha1,
                                   content_sha1, ctx->rev_ctx->pool));

    blob = apr_hash_get(ctx->blobs, checksum->digest, svn_checksum_size(checksum));

    if (blob == NULL && !is_copyfrom) {
        blob = apr_pcalloc(ctx->pool, sizeof(blob_t));
        blob->checksum = svn_checksum_dup(checksum, ctx->pool);

        content_length = apr_hash_get(headers, SVN_REPOS_DUMPFILE_TEXT_CONTENT_LENGTH, APR_HASH_KEY_STRING);
        if (content_length != NULL) {
            SVN_ERR(svn_cstring_atoui64(&blob->length, content_length));
        }

        apr_hash_set(ctx->blobs, blob->checksum->digest,
                     svn_checksum_size(blob->checksum), blob);
    }

    *dst = blob;

    return GIT_SVN_SUCCESS;
}

static svn_error_t *
new_node_record(void **n_ctx, apr_hash_t *headers, void *r_ctx, apr_pool_t *pool)
{
    const char *path, *copyfrom_path, *node_path, *ignored;
    parser_ctx_t *ctx = r_ctx;
    revision_t *rev = ctx->rev_ctx->rev;
    commit_t *commit;
    branch_t *branch = NULL, *copyfrom_branch = NULL;
    apr_array_header_t *nodes;
    node_t *node;
    node_action_t action = get_node_action(headers);
    node_kind_t kind = get_node_kind(headers);

    *n_ctx = ctx;

    path = apr_hash_get(headers, SVN_REPOS_DUMPFILE_NODE_PATH, APR_HASH_KEY_STRING);
    if (path == NULL) {
        return SVN_NO_ERROR;
    }

    copyfrom_path = apr_hash_get(headers, SVN_REPOS_DUMPFILE_NODE_COPYFROM_PATH, APR_HASH_KEY_STRING);
    if (copyfrom_path != NULL) {
        copyfrom_branch = branch_storage_lookup_path(ctx->bs, copyfrom_path);
    }

    branch = branch_storage_lookup_path(ctx->bs, path);
    if (branch == NULL) {
        return SVN_NO_ERROR;
    }

    if (kind == KIND_DIR && action == ACTION_ADD && copyfrom_path == NULL) {
        return SVN_NO_ERROR;
    }

    if (kind == KIND_UNKNOWN && action == ACTION_DELETE && branch_path_is_root(branch, path)) {
        apr_hash_set(rev->remove, branch, sizeof(branch_t *), NOT_NULL);
        return SVN_NO_ERROR;
    }

    node_path = branch_skip_prefix(branch, path);
    if (node_path == NULL) {
        return SVN_NO_ERROR;
    }

    if (*node_path == '/') {
        node_path++;
    }

    ignored = tree_find_longest_prefix(ctx->options->ignore, node_path);
    if (ignored != NULL) {
        return SVN_NO_ERROR;
    }

    commit = apr_hash_get(rev->commits, branch, sizeof(branch_t *));
    if (commit == NULL) {
        commit = commit_create(ctx->pool);
        commit_parent_set(commit, branch_head_get(branch));
        apr_hash_set(rev->commits, branch, sizeof(branch_t *), commit);
    }

    nodes = apr_hash_get(ctx->rev_ctx->nodes, branch, sizeof(branch_t *));
    if (nodes == NULL) {
        nodes = apr_array_make(ctx->rev_ctx->pool, 8, sizeof(node_t));
        apr_hash_set(ctx->rev_ctx->nodes, branch, sizeof(branch_t *), nodes);
    }

    node = &APR_ARRAY_PUSH(nodes, node_t);
    node->mode = get_node_default_mode(kind);
    node->kind = kind;
    node->action = action;
    node->path = apr_pstrdup(ctx->rev_ctx->pool, node_path);

    if (node->kind == KIND_FILE) {
        SVN_ERR(get_node_blob(&node->content.data.blob, headers, ctx));

        if (node->content.data.blob != NULL) {
            node->content.kind = CONTENT_BLOB;
        }
    }

    if (node->content.kind == CONTENT_UNKNOWN) {
        const char *copyfrom_subpath;
        const commit_t *copyfrom_commit = NULL;

        if (copyfrom_branch != NULL) {
            copyfrom_commit = get_copyfrom_commit(headers, ctx, copyfrom_branch);

            if (copyfrom_commit != NULL) {
                copyfrom_subpath = branch_skip_prefix(copyfrom_branch, copyfrom_path);

                if (*copyfrom_subpath == '/') {
                    copyfrom_subpath++;
                }

                if (*copyfrom_subpath == '\0') {
                    commit_copyfrom_set(commit, copyfrom_commit);
                }
            }
        } else {
            // In case of file mode modification without content modification
            // Subversion does not dump its' checksum, so we have to look
            // for it in the previous commit
            copyfrom_commit = commit_parent_get(commit);
            copyfrom_subpath = node->path;
        }

        if (copyfrom_commit != NULL) {
            SVN_ERR(backend_get_checksum(&node->content.data.checksum,
                                         &ctx->backend,
                                         copyfrom_commit, copyfrom_subpath,
                                         ctx->rev_ctx->pool,
                                         ctx->rev_ctx->pool));

            if (node->content.data.checksum != NULL) {
                node->content.kind = CONTENT_CHECKSUM;
            }
        }
    }

    ctx->node = node;

    return SVN_NO_ERROR;
}

static const author_t *
find_author(const parser_ctx_t *ctx, const char *svn_name)
{
    author_t *author;

    author = apr_hash_get(ctx->authors, svn_name, APR_HASH_KEY_STRING);
    if (author == NULL) {
        author = apr_pcalloc(ctx->pool, sizeof(author_t));
        author->svn_name = apr_pstrdup(ctx->pool, svn_name);
        author->name = "unknown";
        author->email = apr_psprintf(ctx->pool, "%s@local", author->svn_name);
        apr_hash_set(ctx->authors, author->svn_name, APR_HASH_KEY_STRING, author);
    }

    return author;
}

static svn_error_t *
set_revision_property(void *r_ctx, const char *name, const svn_string_t *value)
{
    parser_ctx_t *ctx = r_ctx;
    apr_pool_t *pool = ctx->rev_ctx->pool;

    if (strcmp(name, SVN_PROP_REVISION_AUTHOR) == 0) {
        ctx->rev_ctx->author = find_author(ctx, value->data);
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

    if (node->mode == MODE_SYMLINK) {
        // We need to wrap the output stream with a special stream
        // which will strip a symlink marker from the beginning of a content.
        *stream = symlink_content_stream_create(*stream, ctx->rev_ctx->pool);
        // Subtract a symlink marker length from the blob length.
        blob->length -= sizeof(SYMLINK_CONTENT_PREFIX);
    }

    SVN_ERR(backend_write_blob_header(&ctx->backend, blob, ctx->rev_ctx->pool));

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
    revision_ctx_t *rev_ctx = ctx->rev_ctx;
    revision_t *rev = rev_ctx->rev;
    apr_hash_index_t *idx;
    branch_t *branch;
    ssize_t keylen = sizeof(branch_t *);

    if (apr_hash_count(rev->commits) == 0 && apr_hash_count(rev->remove) == 0) {
        SVN_ERR(backend_notify_revision_skipped(&ctx->backend, rev->revnum, rev_ctx->pool));

        return SVN_NO_ERROR;
    }

    for (idx = apr_hash_first(ctx->rev_ctx->pool, rev->commits); idx; idx = apr_hash_next(idx)) {
        const void *key;
        commit_t *commit;
        void *value;

        apr_hash_this(idx, &key, &keylen, &value);
        branch = (branch_t *) key;
        commit = value;

        apr_array_header_t *nodes = apr_hash_get(ctx->rev_ctx->nodes, branch, sizeof(branch_t *));

        if (nodes->nelts == 1 && commit_copyfrom_get(commit) != NULL) {
            // In case there is only one node in a commit and this node is
            // a root directory copied from another branch, mark this commit
            // as dummy, set copyfrom commit as its parent and reset branch to it.
            commit_parent_set(commit, commit_copyfrom_get(commit));
            commit_dummy_set(commit);

            SVN_ERR(backend_reset_branch(&ctx->backend, branch, commit, rev_ctx->pool));
        }
        else {
            commit_mark_set(commit, ctx->last_mark++);

            SVN_ERR(backend_write_commit(&ctx->backend, branch, commit, nodes, rev_ctx->author, rev_ctx->message, rev_ctx->timestamp, rev_ctx->pool));
        }

        SVN_ERR(backend_notify_branch_updated(&ctx->backend, branch, rev_ctx->pool));

        branch_head_set(branch, commit);
    }

    for (idx = apr_hash_first(ctx->rev_ctx->pool, rev->remove); idx; idx = apr_hash_next(idx)) {
        const void *key;

        apr_hash_this(idx, &key, &keylen, NULL);
        branch = (branch_t *) key;

        SVN_ERR(backend_remove_branch(&ctx->backend, branch, rev_ctx->pool));
        SVN_ERR(backend_notify_branch_removed(&ctx->backend, branch, rev_ctx->pool));
    }

    SVN_ERR(backend_notify_revision_imported(&ctx->backend, rev->revnum, rev_ctx->pool));

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

svn_error_t *
git_svn_parse_dumpstream(svn_stream_t *dst,
                         svn_stream_t *src,
                         git_svn_options_t *options,
                         apr_pool_t *pool)
{
    apr_status_t apr_err;
    int back_fd = BACK_FILENO;
    apr_file_t *back_file;

    parser_ctx_t ctx = {};
    ctx.pool = pool;
    ctx.authors = apr_hash_make(pool);
    ctx.bs = branch_storage_create(pool, options->branches, options->tags);
    ctx.revisions = apr_hash_make(pool);
    ctx.blobs = apr_hash_make(pool);
    ctx.last_mark = 1;

    if (options->authors != NULL) {
        svn_stream_t *authors;
        svn_error_t *err;
        SVN_ERR(svn_stream_open_readonly(&authors, options->authors,
                                         pool, pool));
        err = git_svn_parse_authors(ctx.authors, authors, pool);
        if (err) {
            return svn_error_quick_wrap(err, "Malformed authors file");
        }
        SVN_ERR(svn_stream_close(authors));
    }

    branch_storage_add_branch(ctx.bs, "refs/heads/master", options->trunk);
    ctx.options = options;

    ctx.backend.verbose = options->verbose;
    ctx.backend.out = dst;

    // Read backend answers
    apr_err = apr_os_file_put(&back_file, &back_fd, APR_FOPEN_READ, pool);
    if (apr_err) {
        return svn_error_wrap_apr(apr_err, NULL);
    }
    ctx.backend.back = svn_stream_from_aprfile2(back_file, false, pool);

#if (SVN_VER_MAJOR == 1 && SVN_VER_MINOR > 7)
    SVN_ERR(svn_repos_parse_dumpstream3(src, &callbacks, &ctx, false, check_cancel, NULL, pool));
#else
    SVN_ERR(svn_repos_parse_dumpstream2(src, &callbacks, &ctx, check_cancel, NULL, pool));
#endif

    SVN_ERR(backend_finished(&ctx.backend, pool));

    return GIT_SVN_SUCCESS;
}
