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

#include "export.h"
#include "backend.h"
#include "symlink.h"
#include "tree.h"
#include "utils.h"
#include <apr_portable.h>
#include <svn_dirent_uri.h>
#include <svn_pools.h>
#include <svn_props.h>
#include <svn_repos.h>
#include <svn_time.h>
#include <svn_version.h>

#define BACK_FILENO 3

typedef struct
{
    apr_pool_t *pool;
    const char *message;
    const author_t *author;
    int64_t timestamp;
    revision_t *rev;
    // Node storage.
    node_storage_t *nodes;
    svn_fs_root_t *root;
} revision_ctx_t;

typedef struct
{
    apr_pool_t *pool;
    backend_t backend;
    // Author storage.
    author_storage_t *authors;
    // Branch storage.
    branch_storage_t *branches;
    // Revision storage.
    revision_storage_t *revisions;
    // Blob storage.
    blob_storage_t *blobs;
    tree_t *ignores;
    mark_t last_mark;
    revision_ctx_t *rev_ctx;
    node_t *node;
} parser_ctx_t;

static svn_error_t *
new_revision_record(void **r_ctx,
                    svn_revnum_t revnum,
                    svn_fs_root_t *root,
                    void *p_ctx,
                    apr_pool_t *pool)
{
    parser_ctx_t *ctx = p_ctx;
    ctx->rev_ctx = apr_pcalloc(pool, sizeof(revision_ctx_t));
    revision_t *rev;

    rev = revision_storage_add_revision(ctx->revisions, revnum);

    ctx->rev_ctx->rev = rev;
    ctx->rev_ctx->nodes = node_storage_create(pool);
    ctx->rev_ctx->pool = pool;
    ctx->rev_ctx->root = root;

    *r_ctx = ctx;

    return SVN_NO_ERROR;
}

static uint32_t
get_node_default_mode(svn_node_kind_t kind)
{
    switch(kind) {
    case svn_node_dir:
        return MODE_DIR;
    default:
        return MODE_NORMAL;
    }
}

static const commit_t *
get_copyfrom_commit(svn_fs_path_change2_t *change, parser_ctx_t *ctx, branch_t *copyfrom_branch)
{
    const commit_t *commit = NULL;
    const revision_t *rev;

    if (!change->copyfrom_known) {
        return NULL;
    }

    rev = revision_storage_get_by_revnum(ctx->revisions, change->copyfrom_rev);
    if (rev != NULL) {
        commit = revision_commits_get(rev, copyfrom_branch);
    }

    if (commit == NULL) {
        commit = branch_head_get(copyfrom_branch);
    }

    return commit;
}

static svn_error_t *
get_node_blob(blob_t **dst, const char *path, parser_ctx_t *ctx)
{
    svn_checksum_t *checksum;
    blob_t *blob;
    svn_filesize_t size;

    SVN_ERR(svn_fs_file_checksum(&checksum,
                                 svn_checksum_sha1,
                                 ctx->rev_ctx->root,
                                 path,
                                 FALSE,
                                 ctx->rev_ctx->pool));

    blob = blob_storage_get(ctx->blobs, checksum);

    SVN_ERR(svn_fs_file_length(&size,
                               ctx->rev_ctx->root,
                               path,
                               ctx->rev_ctx->pool));
    blob_size_set(blob, size);

    *dst = blob;

    return SVN_NO_ERROR;
}

static svn_error_t *
new_node_record(void **n_ctx, const char *path, svn_fs_path_change2_t *change, void *r_ctx, apr_pool_t *pool)
{
    const char *copyfrom_path = NULL, *node_path, *ignored;
    parser_ctx_t *ctx = r_ctx;
    revision_t *rev = ctx->rev_ctx->rev;
    commit_t *commit;
    branch_t *branch = NULL, *copyfrom_branch = NULL;
    blob_t *blob = NULL;
    node_t *node;
    svn_fs_path_change_kind_t action = change->change_kind;
    svn_node_kind_t kind = change->node_kind;

    *n_ctx = ctx;

    if (change->copyfrom_known && SVN_IS_VALID_REVNUM(change->copyfrom_rev)) {
        copyfrom_path = svn_dirent_skip_ancestor("/", change->copyfrom_path);
        copyfrom_branch = branch_storage_lookup_path(ctx->branches, copyfrom_path);
    }

    branch = branch_storage_lookup_path(ctx->branches, path);
    if (branch == NULL) {
        return SVN_NO_ERROR;
    }

    if (kind == svn_node_dir && action == svn_fs_path_change_add && copyfrom_path == NULL) {
        return SVN_NO_ERROR;
    }

    if (kind == svn_node_dir && action == svn_fs_path_change_delete && branch_path_is_root(branch, path)) {
        revision_removes_add(rev, branch);
        return SVN_NO_ERROR;
    }

    node_path = branch_skip_prefix(branch, path);
    if (node_path == NULL) {
        return SVN_NO_ERROR;
    }

    if (*node_path == '/') {
        node_path++;
    }

    ignored = tree_find_longest_prefix(ctx->ignores, node_path);
    if (ignored != NULL) {
        return SVN_NO_ERROR;
    }

    commit = revision_commits_get(rev, branch);
    if (commit == NULL) {
        commit = revision_commits_add(rev, branch);
    }

    node = node_storage_add(ctx->rev_ctx->nodes, branch);
    node_mode_set(node, get_node_default_mode(kind));
    node_kind_set(node, kind);
    node_action_set(node, action);
    node_path_set(node, apr_pstrdup(ctx->rev_ctx->pool, node_path));

    if (kind == svn_node_file && action != svn_fs_path_change_delete) {
        SVN_ERR(get_node_blob(&blob, path, ctx));

        if (blob != NULL) {
            node_content_blob_set(node, blob);
        }
    }

    if (node_content_kind_get(node) == CONTENT_UNKNOWN) {
        const char *copyfrom_subpath;
        const commit_t *copyfrom_commit = NULL;

        if (copyfrom_branch != NULL) {
            copyfrom_commit = get_copyfrom_commit(change, ctx, copyfrom_branch);

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
            copyfrom_subpath = node_path_get(node);
        }

        if (copyfrom_commit != NULL) {
            svn_checksum_t *checksum = NULL;
            node_mode_t mode;
            SVN_ERR(backend_get_mode_checksum(&mode,
                                              &checksum,
                                              &ctx->backend,
                                              copyfrom_commit,
                                              copyfrom_subpath,
                                              ctx->rev_ctx->pool,
                                              ctx->rev_ctx->pool));

            if (checksum != NULL) {
                node_mode_set(node, mode);
                node_content_checksum_set(node, checksum);
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
        ctx->rev_ctx->author = author_storage_lookup(ctx->authors, value->data);
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
        node_mode_set(node, MODE_EXECUTABLE);
    }
    else if (strcmp(name, SVN_PROP_SPECIAL) == 0) {
        node_mode_set(node, MODE_SYMLINK);
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

    blob_t *blob = node_content_blob_get(node);

    if (blob_mark_get(blob)) {
        // Blob has been uploaded, if we've already assigned mark to it
        return SVN_NO_ERROR;
    }

    blob_mark_set(blob, ctx->last_mark++);
    SVN_ERR(svn_stream_for_stdout(stream, ctx->rev_ctx->pool));

    if (node_mode_get(node) == MODE_SYMLINK) {
        // We need to wrap the output stream with a special stream
        // which will strip a symlink marker from the beginning of a content.
        *stream = symlink_content_stream_create(*stream, ctx->rev_ctx->pool);
        // Subtract a symlink marker length from the blob size.
        blob_size_set(blob, blob_size_get(blob) - sizeof(SYMLINK_CONTENT_PREFIX));
    }

    SVN_ERR(backend_write_blob_header(&ctx->backend, blob, ctx->rev_ctx->pool));

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
write_commit(void *p_ctx, branch_t *branch, commit_t *commit, apr_pool_t *pool)
{
    parser_ctx_t *ctx = p_ctx;
    revision_ctx_t *rev_ctx = ctx->rev_ctx;

    const node_list_t *nodes = node_storage_list(rev_ctx->nodes, branch);

    if (node_list_count(nodes) == 1 && commit_copyfrom_get(commit) != NULL) {
        // In case there is only one node in a commit and this node is
        // a root directory copied from another branch, mark this commit
        // as dummy, set copyfrom commit as its parent and reset branch to it.
        commit_parent_set(commit, commit_copyfrom_get(commit));
        commit_dummy_set(commit);

        SVN_ERR(backend_reset_branch(&ctx->backend, branch, commit, pool));
    } else {
        commit_mark_set(commit, ctx->last_mark++);
        SVN_ERR(backend_write_commit(&ctx->backend, branch, commit, nodes, rev_ctx->author, rev_ctx->message, rev_ctx->timestamp, pool));
    }

    SVN_ERR(backend_notify_branch_updated(&ctx->backend, branch, pool));

    branch_head_set(branch, commit);

    return SVN_NO_ERROR;
}

static svn_error_t *
remove_branch(void *p_ctx, branch_t *branch, apr_pool_t *pool)
{
    parser_ctx_t *ctx = p_ctx;

    SVN_ERR(backend_remove_branch(&ctx->backend, branch, pool));
    SVN_ERR(backend_notify_branch_removed(&ctx->backend, branch, pool));

    return SVN_NO_ERROR;
}

static svn_error_t *
close_revision(void *r_ctx)
{
    parser_ctx_t *ctx = r_ctx;
    revision_ctx_t *rev_ctx = ctx->rev_ctx;
    revision_t *rev = rev_ctx->rev;

    if (revision_commits_count(rev) == 0 && revision_removes_count(rev) == 0) {
        SVN_ERR(backend_notify_revision_skipped(&ctx->backend,
                                                revision_revnum_get(rev),
                                                rev_ctx->pool));

        return SVN_NO_ERROR;
    }

    if (rev_ctx->author == NULL) {
        rev_ctx->author = author_storage_default_author(ctx->authors);
    }

    revision_commits_apply(rev, &write_commit, ctx, rev_ctx->pool);
    revision_removes_apply(rev, &remove_branch, ctx, rev_ctx->pool);

    SVN_ERR(backend_notify_revision_imported(&ctx->backend, revision_revnum_get(rev), rev_ctx->pool));

    ctx->rev_ctx = NULL;

    return SVN_NO_ERROR;
}

svn_error_t *
export_revision_range(svn_stream_t *dst,
                      svn_fs_t *fs,
                      svn_revnum_t lower,
                      svn_revnum_t upper,
                      branch_storage_t *branches,
                      revision_storage_t *revisions,
                      author_storage_t *authors,
                      tree_t *ignores,
                      apr_pool_t *pool)
{
    apr_pool_t *subpool;
    apr_status_t apr_err;
    int back_fd = BACK_FILENO;
    apr_file_t *back_file;
    svn_revnum_t rev;

    parser_ctx_t ctx = {};
    ctx.pool = pool;
    ctx.authors = authors;
    ctx.branches = branches;
    ctx.revisions = revisions;
    ctx.blobs = blob_storage_create(pool);
    ctx.ignores = ignores;
    ctx.last_mark = 1;

    ctx.backend.out = dst;

    // Read backend answers
    apr_err = apr_os_file_put(&back_file, &back_fd, APR_FOPEN_READ, pool);
    if (apr_err) {
        return svn_error_wrap_apr(apr_err, NULL);
    }
    ctx.backend.back = svn_stream_from_aprfile2(back_file, false, pool);

    subpool = svn_pool_create(pool);

    for (rev = lower; rev <= upper; rev++) {
        apr_hash_t *fs_changes, *revprops;
        apr_hash_index_t *idx;
        svn_fs_root_t *root;
        void *r_ctx;

        svn_pool_clear(subpool);

        SVN_ERR(svn_fs_revision_root(&root, fs, rev, subpool));

        SVN_ERR(new_revision_record(&r_ctx, rev, root, &ctx, subpool));

        SVN_ERR(svn_fs_revision_proplist(&revprops, fs, rev, subpool));
        for (idx = apr_hash_first(subpool, revprops); idx; idx = apr_hash_next(idx)) {
            const char *name = apr_hash_this_key(idx);
            const svn_string_t *value = apr_hash_this_val(idx);
            SVN_ERR(set_revision_property(r_ctx, name, value));
        }

        // Fetch the paths changed under root.
        SVN_ERR(svn_fs_paths_changed2(&fs_changes, root, subpool));

        for (idx = apr_hash_first(subpool, fs_changes); idx; idx = apr_hash_next(idx)) {
            const char *path = apr_hash_this_key(idx);
            path = svn_dirent_skip_ancestor("/", path);
            svn_fs_path_change2_t *change = apr_hash_this_val(idx);
            void *n_ctx;

            SVN_ERR(new_node_record(&n_ctx, path, change, r_ctx, subpool));

            if (change->change_kind != svn_fs_path_change_delete) {
                apr_hash_t *props;
                apr_hash_index_t *idx2;
                svn_stream_t *output = NULL;

                SVN_ERR(svn_fs_node_proplist(&props, root, path, subpool));

                for (idx2 = apr_hash_first(subpool, props); idx2; idx2 = apr_hash_next(idx2)) {
                    const char *name = apr_hash_this_key(idx2);
                    const svn_string_t *value = apr_hash_this_val(idx2);
                    SVN_ERR(set_node_property(n_ctx, name, value));
                }

                SVN_ERR(set_fulltext(&output, n_ctx));

                if (output != NULL) {
                    svn_stream_t *content;
                    svn_filesize_t size;
                    SVN_ERR(svn_fs_file_length(&size, root, path, subpool));
                    SVN_ERR(svn_fs_file_contents(&content, root, path, subpool));
                    SVN_ERR(svn_stream_copy3(content, output, NULL, NULL, subpool));
                }
            }

            SVN_ERR(close_node(n_ctx));
        }

        SVN_ERR(close_revision(r_ctx));
    }

    SVN_ERR(backend_finished(&ctx.backend, pool));

    return SVN_NO_ERROR;
}
