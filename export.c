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
#include "tree.h"
#include "utils.h"
#include <apr_portable.h>
#include <svn_dirent_uri.h>
#include <svn_hash.h>
#include <svn_pools.h>
#include <svn_props.h>
#include <svn_sorts.h>
#include <svn_time.h>

#define NULL_SHA1 "0000000000000000000000000000000000000000"

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
get_copyfrom_commit(svn_revnum_t revnum, parser_ctx_t *ctx, branch_t *copyfrom_branch)
{
    while (revnum > 0) {
        const revision_t *rev;
        const commit_t *commit = NULL;
        rev = revision_storage_get_by_revnum(ctx->revisions, revnum);

        if (rev != NULL) {
            commit = revision_commits_get(rev, copyfrom_branch);
        }

        if (commit != NULL) {
            return commit;
        }

        --revnum;
    }

    return NULL;
}

static svn_error_t *
process_change_record(const char *path, svn_fs_path_change2_t *change, void *r_ctx, apr_pool_t *pool)
{
    const char *src_path = NULL, *node_path, *ignored;
    parser_ctx_t *ctx = r_ctx;
    revision_t *rev = ctx->rev_ctx->rev;
    commit_t *commit;
    branch_t *branch = NULL, *src_branch = NULL;
    node_t *node;
    svn_boolean_t dst_is_root, src_is_root;
    svn_boolean_t modify, remove;
    svn_fs_path_change_kind_t action = change->change_kind;
    svn_node_kind_t kind = change->node_kind;
    svn_revnum_t src_rev = change->copyfrom_rev;

    remove = (action == svn_fs_path_change_replace ||
              action == svn_fs_path_change_delete);

    modify = (action == svn_fs_path_change_replace ||
              action == svn_fs_path_change_add ||
              action == svn_fs_path_change_modify);

    if (change->copyfrom_known && SVN_IS_VALID_REVNUM(src_rev)) {
        src_path = change->copyfrom_path;
        src_branch = branch_storage_lookup_path(ctx->branches, src_path, pool);
        src_is_root = (src_branch != NULL && branch_path_is_root(src_branch, src_path));
    }

    branch = branch_storage_lookup_path(ctx->branches, path, pool);
    if (branch == NULL) {
        return SVN_NO_ERROR;
    }

    dst_is_root = branch_path_is_root(branch, path);

    if (kind == svn_node_dir && src_branch == NULL) {
        if (action == svn_fs_path_change_add || action == svn_fs_path_change_modify) {
            return SVN_NO_ERROR;
        }
    }

    if (action == svn_fs_path_change_delete && dst_is_root) {
        revision_removes_add(rev, branch);
        return SVN_NO_ERROR;
    }

    node_path = branch_skip_prefix(branch, path);
    if (node_path == NULL) {
        return SVN_NO_ERROR;
    }

    ignored = tree_match(ctx->ignores, node_path, pool);
    if (ignored != NULL) {
        return SVN_NO_ERROR;
    }

    commit = revision_commits_get(rev, branch);
    if (commit == NULL) {
        commit = revision_commits_add(rev, branch);
    }

    if (modify && dst_is_root && src_is_root) {
        commit->copyfrom = get_copyfrom_commit(change->copyfrom_rev, ctx, src_branch);
        return SVN_NO_ERROR;
    }

    node = node_storage_add(ctx->rev_ctx->nodes, branch);
    node->kind = kind;
    node->action = action;
    node->path = node_path;

    if (action == svn_fs_path_change_delete) {
        return SVN_NO_ERROR;
    }

    if (action == svn_fs_path_change_replace && src_path == NULL) {
        node->action = svn_fs_path_change_delete;
        return SVN_NO_ERROR;
    }

    SVN_ERR(set_node_mode(&node->mode, ctx->rev_ctx->root, path, pool));

    if (kind == svn_node_file) {
        SVN_ERR(set_content_checksum(&node->checksum, ctx->dst, ctx->blobs,
                                     ctx->rev_ctx->root, path, pool));
        return SVN_NO_ERROR;
    }

    if (kind == svn_node_dir && src_branch != NULL) {
        apr_array_header_t *dummy;
        svn_fs_t *fs = svn_fs_root_fs(ctx->rev_ctx->root);
        svn_fs_root_t *src_root;
        tree_t *ignores;
        const tree_t *subbranches = tree_subtree(ctx->branches->tree, src_branch->path, pool);
        tree_merge(&ignores, ctx->ignores, subbranches, pool);

        SVN_ERR(svn_fs_revision_root(&src_root, fs, change->copyfrom_rev, pool));
        SVN_ERR(set_tree_checksum(&node->checksum, &dummy, ctx->dst, ctx->blobs,
                                  src_root, src_path, src_branch->path, ignores,
                                  pool));
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
reset_branch(void *p_ctx, const branch_t *branch, const commit_t *commit, apr_pool_t *pool)
{
    parser_ctx_t *ctx = p_ctx;

    SVN_ERR(svn_stream_printf(ctx->dst, pool, "reset %s\n", branch->refname));
    SVN_ERR(svn_stream_printf(ctx->dst, pool, "from :%d\n",
                              commit_mark_get(commit)));

    return SVN_NO_ERROR;
}

static svn_error_t *
node_modify(svn_stream_t *dst, const node_t *node, apr_pool_t *pool)
{
    const char *checksum;
    checksum = svn_checksum_to_cstring_display(node->checksum, pool);

    SVN_ERR(svn_stream_printf(dst, pool, "M %o %s \"%s\"\n",
                              node->mode, checksum, node->path));

    return SVN_NO_ERROR;
}

static svn_error_t *
node_delete(svn_stream_t *dst, const node_t *node, apr_pool_t *pool)
{
    SVN_ERR(svn_stream_printf(dst, pool, "D \"%s\"\n", node->path));

    return SVN_NO_ERROR;
}

static svn_error_t *
write_commit(void *p_ctx, branch_t *branch, commit_t *commit, apr_pool_t *pool)
{
    apr_array_header_t *nodes;
    parser_ctx_t *ctx = p_ctx;
    svn_stream_t *dst = ctx->dst;
    revision_ctx_t *rev_ctx = ctx->rev_ctx;

    nodes = node_storage_list(rev_ctx->nodes, branch);

    if (nodes->nelts == 0 && commit->copyfrom != NULL) {
        // In case there is only one node in a commit and this node is
        // a root directory copied from another branch, mark this commit
        // as dummy, set copyfrom commit as its parent and reset branch to it.
        commit->dummy = TRUE;
        commit->parent = commit->copyfrom;
        SVN_ERR(reset_branch(ctx, branch, commit->copyfrom, pool));
    } else {
        commit->mark = ctx->last_mark++;

        SVN_ERR(svn_stream_printf(dst, pool, "commit %s\n", branch->refname));
        SVN_ERR(svn_stream_printf(dst, pool, "mark :%d\n", commit->mark));
        SVN_ERR(svn_stream_printf(dst, pool, "committer %s %"PRId64" +0000\n",
                                  author_to_cstring(rev_ctx->author, pool),
                                  rev_ctx->timestamp));
        SVN_ERR(svn_stream_printf(dst, pool, "data %ld\n", strlen(rev_ctx->message)));
        SVN_ERR(svn_stream_printf(dst, pool, "%s\n", rev_ctx->message));

        if (commit->copyfrom != NULL) {
            SVN_ERR(svn_stream_printf(dst, pool, "from :%d\n",
                                      commit_mark_get(commit->copyfrom)));
        }
    }

    for (int i = 0; i < nodes->nelts; i++) {
        const node_t *node = &APR_ARRAY_IDX(nodes, i, node_t);
        switch (node->action) {
        case svn_fs_path_change_add:
        case svn_fs_path_change_modify:
            SVN_ERR(node_modify(dst, node, pool));
            break;
        case svn_fs_path_change_delete:
            SVN_ERR(node_delete(dst, node, pool));
            break;
        case svn_fs_path_change_replace:
            SVN_ERR(node_delete(dst, node, pool));
            SVN_ERR(node_modify(dst, node, pool));
        default:
            // noop
            break;
        }
    }

    branch->head = commit;

    return SVN_NO_ERROR;
}

static svn_error_t *
remove_branch(void *p_ctx, branch_t *branch, apr_pool_t *pool)
{
    parser_ctx_t *ctx = p_ctx;

    SVN_ERR(svn_stream_printf(ctx->dst, pool, "reset %s\n", branch->refname));
    SVN_ERR(svn_stream_printf(ctx->dst, pool, "from %s\n", NULL_SHA1));

    return SVN_NO_ERROR;
}

static svn_error_t *
close_revision(void *r_ctx, apr_pool_t *pool)
{
    parser_ctx_t *ctx = r_ctx;
    revision_ctx_t *rev_ctx = ctx->rev_ctx;
    revision_t *rev = rev_ctx->rev;

    if (revision_commits_count(rev) == 0 && revision_removes_count(rev) == 0) {
        SVN_ERR(svn_stream_printf(ctx->dst, pool,
                                  "progress Skipped revision %ld\n",
                                  revision_revnum_get(rev)));
        return SVN_NO_ERROR;
    }

    if (rev_ctx->author == NULL) {
        rev_ctx->author = author_storage_default_author(ctx->authors);
    }

    revision_removes_apply(rev, &remove_branch, ctx, pool);
    revision_commits_apply(rev, &write_commit, ctx, pool);

    SVN_ERR(svn_stream_printf(ctx->dst, pool,
                              "progress Imported revision %ld\n",
                              revision_revnum_get(rev)));

    ctx->rev_ctx = NULL;

    return SVN_NO_ERROR;
}

static svn_error_t *
prepare_changes(apr_array_header_t **dst,
                apr_array_header_t *fs_changes,
                svn_fs_root_t *root,
                parser_ctx_t *ctx,
                apr_pool_t *pool)
{
    apr_array_header_t *changes = apr_array_make(pool, 0, sizeof(svn_sort__item_t));

    for (int i = 0; i < fs_changes->nelts; i++) {
        apr_array_header_t *copies, *removes;
        svn_boolean_t remove, modify;
        svn_sort__item_t item = APR_ARRAY_IDX(fs_changes, i, svn_sort__item_t);
        svn_fs_path_change2_t *change = item.value;
        svn_fs_path_change_kind_t action = change->change_kind;

        // Convert dirent paths to relpaths.
        const char *path = svn_dirent_skip_ancestor("/", item.key);
        if (change->copyfrom_known && SVN_IS_VALID_REVNUM(change->copyfrom_rev)) {
            change->copyfrom_path = svn_dirent_skip_ancestor("/", change->copyfrom_path);
        }

        svn_sort__item_t *new_item = apr_array_push(changes);
        new_item->key = path;
        new_item->value = change;

        if (change->node_kind != svn_node_dir) {
            continue;
        }

        remove = (action == svn_fs_path_change_replace ||
                  action == svn_fs_path_change_delete);

        modify = (action == svn_fs_path_change_replace ||
                  action == svn_fs_path_change_add ||
                  action == svn_fs_path_change_modify);

        if (remove) {
            removes = branch_storage_collect_branches(ctx->branches, path, pool);

            for (int i = 0; i < removes->nelts; i++) {
                branch_t *branch = APR_ARRAY_IDX(removes, i, branch_t *);
                if (branch_path_is_root(branch, path)) {
                    continue;
                }
                svn_sort__item_t *subitem = apr_array_push(changes);
                svn_fs_path_change2_t *subchange = apr_pcalloc(pool, sizeof(svn_fs_path_change2_t));
                subitem->key = branch->path;
                subitem->value = subchange;

                subchange->change_kind = svn_fs_path_change_delete;
                subchange->node_kind = svn_node_dir;
            }
        }

        if (modify && change->copyfrom_known && SVN_IS_VALID_REVNUM(change->copyfrom_rev)) {
            const char *src_path = change->copyfrom_path;
            svn_fs_t *fs = svn_fs_root_fs(root);
            svn_fs_root_t *src_root;
            SVN_ERR(svn_fs_revision_root(&src_root, fs, change->copyfrom_rev, pool));
            copies = branch_storage_collect_branches(ctx->branches, src_path, pool);

            for (int i = 0; i < copies->nelts; i++) {
                const char *new_path;
                svn_node_kind_t kind;
                branch_t *branch = APR_ARRAY_IDX(copies, i, branch_t *);
                if (branch_path_is_root(branch, path)) {
                    continue;
                }
                SVN_ERR(svn_fs_check_path(&kind, src_root, branch->path, pool));
                if (kind == svn_node_none) {
                    continue;
                }

                new_path = svn_relpath_join(path, svn_relpath_skip_ancestor(src_path, branch->path), pool);

                svn_sort__item_t *subitem = apr_array_push(changes);
                svn_fs_path_change2_t *subchange = apr_pcalloc(pool, sizeof(svn_fs_path_change2_t));
                subitem->key = new_path;
                subitem->value = subchange;

                subchange->change_kind = svn_fs_path_change_add;
                subchange->node_kind = svn_node_dir;
                subchange->copyfrom_known = TRUE;
                subchange->copyfrom_rev = change->copyfrom_rev;
                subchange->copyfrom_path = branch->path;
            }
        }
    }

    *dst = changes;

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
                      checksum_cache_t *cache,
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
    ctx.blobs = cache;
    ctx.ignores = ignores;
    ctx.last_mark = 1;

    subpool = svn_pool_create(pool);

    for (rev = lower; rev <= upper; rev++) {
        apr_array_header_t *changes, *sorted_changes;
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
        sorted_changes = svn_sort__hash(fs_changes, svn_sort_compare_items_lexically, subpool);
        SVN_ERR(prepare_changes(&changes, sorted_changes, root, &ctx, subpool));

        for (int i = 0; i < changes->nelts; i++) {
            svn_sort__item_t item = APR_ARRAY_IDX(changes, i, svn_sort__item_t);
            SVN_ERR(process_change_record(item.key, item.value, r_ctx, subpool));
        }

        SVN_ERR(close_revision(r_ctx, subpool));
    }

    SVN_ERR(svn_stream_printf(dst, pool, "done\n"));

    return SVN_NO_ERROR;
}
