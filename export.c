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
#include "checksum.h"
#include "tree.h"
#include "utils.h"
#include <apr_portable.h>
#include <svn_dirent_uri.h>
#include <svn_hash.h>
#include <svn_pools.h>
#include <svn_props.h>
#include <svn_sorts.h>
#include <svn_time.h>

typedef struct
{
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
    svn_stream_t *dst;
    // Author storage.
    author_storage_t *authors;
    // Branch storage.
    branch_storage_t *branches;
    // Revision storage.
    revision_storage_t *revisions;
    checksum_cache_t *blobs;
    tree_t *ignores;
    mark_t last_mark;
    revision_ctx_t *rev_ctx;
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
    ctx->rev_ctx->root = root;

    *r_ctx = ctx;

    return SVN_NO_ERROR;
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
new_node_record(void **n_ctx, const char *path, svn_fs_path_change2_t *change, void *r_ctx, apr_pool_t *pool)
{
    const char *copyfrom_path = NULL, *node_path, *ignored;
    parser_ctx_t *ctx = r_ctx;
    revision_t *rev = ctx->rev_ctx->rev;
    commit_t *commit;
    branch_t *branch = NULL, *copyfrom_branch = NULL;
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

    if (kind == svn_node_dir) {
        switch (action) {
        case svn_fs_path_change_add:
        case svn_fs_path_change_modify:
            if (copyfrom_branch == NULL) {
                return SVN_NO_ERROR;
            }
            break;
        case svn_fs_path_change_delete:
            if (branch_path_is_root(branch, path)) {
                revision_removes_add(rev, branch);
                return SVN_NO_ERROR;
            }
            break;
        default:
            // do nothing
            break;
        }
    }

    node_path = branch_skip_prefix(branch, path);
    if (node_path == NULL) {
        return SVN_NO_ERROR;
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
    node->kind = kind;
    node->action = action;
    node->path = node_path;

    if (action == svn_fs_path_change_delete) {
        return SVN_NO_ERROR;
    }

    SVN_ERR(set_node_mode(&node->mode, ctx->rev_ctx->root, path, pool));

    if (kind == svn_node_file) {
        SVN_ERR(set_content_checksum(&node->checksum, ctx->dst, ctx->blobs,
                                     ctx->rev_ctx->root, path, pool));
    } else if (kind == svn_node_dir && action == svn_fs_path_change_replace && copyfrom_path == NULL) {
        node->action = svn_fs_path_change_delete;
    } else if (kind == svn_node_dir && copyfrom_branch != NULL) {
        apr_array_header_t *dummy;
        const commit_t *copyfrom_commit;
        svn_fs_t *fs = svn_fs_root_fs(ctx->rev_ctx->root);
        svn_fs_root_t *copyfrom_root;

        SVN_ERR(svn_fs_revision_root(&copyfrom_root, fs, change->copyfrom_rev, pool));
        SVN_ERR(set_tree_checksum(&node->checksum, &dummy, ctx->dst, ctx->blobs,
                                  copyfrom_root, copyfrom_path,
                                  branch_path_get(copyfrom_branch),
                                  ctx->ignores, pool));

        copyfrom_commit = get_copyfrom_commit(change, ctx, copyfrom_branch);

        if (copyfrom_commit != NULL) {
            const char *copyfrom_subpath;

            copyfrom_subpath = branch_skip_prefix(copyfrom_branch, copyfrom_path);

            if (*copyfrom_subpath == '\0') {
                commit_copyfrom_set(commit, copyfrom_commit);
            }
        }
    }

    return SVN_NO_ERROR;
}

static svn_error_t *
set_revision_property(void *r_ctx, const char *name, const svn_string_t *value, apr_pool_t *pool)
{
    parser_ctx_t *ctx = r_ctx;

    if (strcmp(name, SVN_PROP_REVISION_AUTHOR) == 0) {
        ctx->rev_ctx->author = author_storage_lookup(ctx->authors, value->data);
    }
    else if (strcmp(name, SVN_PROP_REVISION_DATE) == 0) {
        apr_time_t timestamp;
        svn_time_from_cstring(&timestamp, value->data, pool);
        ctx->rev_ctx->timestamp = apr_time_sec(timestamp);
    }
    else if (strcmp(name, SVN_PROP_REVISION_LOG) == 0) {
        ctx->rev_ctx->message = value->data;
    }

    return SVN_NO_ERROR;
}

static svn_error_t *
write_commit(void *p_ctx, branch_t *branch, commit_t *commit, apr_pool_t *pool)
{
    apr_array_header_t *nodes;
    parser_ctx_t *ctx = p_ctx;
    revision_ctx_t *rev_ctx = ctx->rev_ctx;

    nodes = node_storage_list(rev_ctx->nodes, branch);

    if (nodes->nelts == 1 && commit_copyfrom_get(commit) != NULL) {
        // In case there is only one node in a commit and this node is
        // a root directory copied from another branch, mark this commit
        // as dummy, set copyfrom commit as its parent and reset branch to it.
        commit_parent_set(commit, commit_copyfrom_get(commit));
        commit_dummy_set(commit);

        SVN_ERR(backend_reset_branch(ctx->dst, branch, commit, pool));
    } else {
        commit_mark_set(commit, ctx->last_mark++);
        SVN_ERR(backend_write_commit(ctx->dst, branch, commit, nodes, rev_ctx->author, rev_ctx->message, rev_ctx->timestamp, pool));
    }

    branch_head_set(branch, commit);

    return SVN_NO_ERROR;
}

static svn_error_t *
remove_branch(void *p_ctx, branch_t *branch, apr_pool_t *pool)
{
    parser_ctx_t *ctx = p_ctx;

    SVN_ERR(backend_remove_branch(ctx->dst, branch, pool));

    return SVN_NO_ERROR;
}

static svn_error_t *
close_revision(void *r_ctx, apr_pool_t *pool)
{
    parser_ctx_t *ctx = r_ctx;
    revision_ctx_t *rev_ctx = ctx->rev_ctx;
    revision_t *rev = rev_ctx->rev;

    if (revision_commits_count(rev) == 0 && revision_removes_count(rev) == 0) {
        SVN_ERR(backend_notify_revision_skipped(ctx->dst,
                                                revision_revnum_get(rev),
                                                pool));

        return SVN_NO_ERROR;
    }

    if (rev_ctx->author == NULL) {
        rev_ctx->author = author_storage_default_author(ctx->authors);
    }

    revision_commits_apply(rev, &write_commit, ctx, pool);
    revision_removes_apply(rev, &remove_branch, ctx, pool);

    SVN_ERR(backend_notify_revision_imported(ctx->dst, revision_revnum_get(rev), pool));

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
    svn_revnum_t rev;

    parser_ctx_t ctx = {};
    ctx.dst = dst;
    ctx.authors = authors;
    ctx.branches = branches;
    ctx.revisions = revisions;
    ctx.blobs = checksum_cache_create(pool);
    ctx.ignores = ignores;
    ctx.last_mark = 1;

    subpool = svn_pool_create(pool);

    for (rev = lower; rev <= upper; rev++) {
        apr_array_header_t *changes;
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
            SVN_ERR(set_revision_property(r_ctx, name, value, subpool));
        }

        // Fetch the paths changed under root.
        SVN_ERR(svn_fs_paths_changed2(&fs_changes, root, subpool));

        changes = svn_sort__hash(fs_changes, svn_sort_compare_items_lexically, subpool);

        for (int i = 0; i < changes->nelts; i++) {
            svn_sort__item_t item = APR_ARRAY_IDX(changes, i, svn_sort__item_t);
            const char *path = item.key;
            svn_fs_path_change2_t *change = item.value;
            path = svn_dirent_skip_ancestor("/", path);
            void *n_ctx;

            SVN_ERR(new_node_record(&n_ctx, path, change, r_ctx, subpool));
        }

        SVN_ERR(close_revision(r_ctx, subpool));
    }

    SVN_ERR(svn_stream_printf(dst, pool, "done\n"));

    return SVN_NO_ERROR;
}
