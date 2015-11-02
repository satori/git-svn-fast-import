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
#include "node.h"
#include "sorts.h"
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
    svn_fs_root_t *root;
    svn_revnum_t revnum;
    apr_time_t timestamp;
    const author_t *author;
    svn_string_t *message;
    apr_hash_t *commits;
    apr_hash_t *removes;
    apr_hash_t *changes;
} revision_t;

typedef struct
{
    svn_fs_path_change_kind_t action;
    node_t *node;
} change_t;

static svn_error_t *
get_revision(revision_t **rev,
             svn_revnum_t revnum,
             svn_fs_t *fs,
             export_ctx_t *ctx,
             apr_pool_t *pool)
{
    apr_hash_t *revprops;
    revision_t *r;
    svn_fs_root_t *root;
    svn_string_t *value;

    SVN_ERR(svn_fs_revision_root(&root, fs, revnum, pool));
    SVN_ERR(svn_fs_revision_proplist(&revprops, fs, revnum, pool));

    r = apr_pcalloc(pool, sizeof(revision_t));
    r->root = root;
    r->revnum = revnum;
    r->message = svn_string_create_empty(pool);
    r->commits = apr_hash_make(pool);
    r->removes = apr_hash_make(pool);
    r->changes = apr_hash_make(pool);

    value = svn_hash_gets(revprops, SVN_PROP_REVISION_AUTHOR);
    if (value != NULL) {
        r->author = author_storage_lookup(ctx->authors, value->data);
    } else {
        r->author = author_storage_default_author(ctx->authors);
    }

    value = svn_hash_gets(revprops, SVN_PROP_REVISION_DATE);
    if (value != NULL) {
        SVN_ERR(svn_time_from_cstring(&r->timestamp, value->data, pool));
    }

    value = svn_hash_gets(revprops, SVN_PROP_REVISION_LOG);
    if (value != NULL) {
        r->message = value;
    }

    *rev = r;

    return SVN_NO_ERROR;
}

static apr_array_header_t *
get_branch_changes(revision_t *rev, branch_t *branch)
{
    apr_array_header_t *changes;

    changes = apr_hash_get(rev->changes, branch, sizeof(branch_t *));
    if (changes == NULL) {
        apr_pool_t *pool = apr_hash_pool_get(rev->changes);
        changes = apr_array_make(pool, 0, sizeof(change_t));
        apr_hash_set(rev->changes, branch, sizeof(branch_t *), changes);
    }

    return changes;
}

static svn_error_t *
get_mergeinfo_for_path(svn_mergeinfo_t *mergeinfo,
                       svn_fs_root_t *root,
                       const char *path,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
    apr_array_header_t *paths;
    svn_mergeinfo_catalog_t catalog;

    paths = apr_array_make(scratch_pool, 1, sizeof(const char *));
    APR_ARRAY_PUSH(paths, const char *) = path;

    SVN_ERR(svn_fs_get_mergeinfo2(&catalog, root, paths,
                                  svn_mergeinfo_inherited, FALSE, TRUE,
                                  result_pool, scratch_pool));

    *mergeinfo = svn_hash_gets(catalog, path);

    return SVN_NO_ERROR;
}

static svn_error_t *
process_change_record(const char *path,
                      svn_fs_path_change2_t *change,
                      svn_stream_t *dst,
                      revision_t *rev,
                      export_ctx_t *ctx,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
    const char *src_path = NULL, *node_path;
    const char *ignored, *not_ignored;
    commit_t *commit, *parent;
    branch_t *branch = NULL, *src_branch = NULL, *merge_branch;
    node_t *node;
    svn_boolean_t dst_is_root = FALSE, src_is_root = FALSE;
    svn_boolean_t modify;
    svn_fs_path_change_kind_t action = change->change_kind;
    svn_mergeinfo_t mergeinfo = NULL;
    svn_node_kind_t kind = change->node_kind;
    svn_revnum_t src_rev = change->copyfrom_rev;

    ignored = tree_match(ctx->absignores, path, scratch_pool);
    if (ignored != NULL) {
        return SVN_NO_ERROR;
    }

    modify = (action == svn_fs_path_change_replace ||
              action == svn_fs_path_change_add ||
              action == svn_fs_path_change_modify);

    if (change->copyfrom_known && SVN_IS_VALID_REVNUM(src_rev)) {
        src_path = change->copyfrom_path;
        src_branch = branch_storage_lookup_path(ctx->branches, src_path, scratch_pool);
    }

    branch = branch_storage_lookup_path(ctx->branches, path, scratch_pool);
    if (branch == NULL) {
        return SVN_NO_ERROR;
    }

    dst_is_root = branch_path_is_root(branch, path);
    src_is_root = (src_branch != NULL && branch_path_is_root(src_branch, src_path));

    if (kind == svn_node_dir && src_branch == NULL) {
        if (action == svn_fs_path_change_add || action == svn_fs_path_change_modify) {
            return SVN_NO_ERROR;
        }
    }

    if (action == svn_fs_path_change_delete && dst_is_root) {
        apr_hash_set(rev->removes, branch, sizeof(branch_t *), branch);
        return SVN_NO_ERROR;
    }

    node_path = svn_relpath_skip_ancestor(branch->path, path);
    if (node_path == NULL) {
        return SVN_NO_ERROR;
    }

    ignored = tree_match(ctx->ignores, node_path, scratch_pool);
    not_ignored = tree_match(ctx->no_ignores, path, scratch_pool);
    if (ignored != NULL && not_ignored == NULL) {
        return SVN_NO_ERROR;
    }

    commit = apr_hash_get(rev->commits, branch, sizeof(branch_t *));
    if (commit == NULL) {
        parent = commit_cache_get(ctx->commits, rev->revnum - 1, branch);
        commit = commit_cache_add(ctx->commits, rev->revnum, branch);
        if (parent != NULL) {
            commit->parent = parent->mark;
        }

        apr_hash_set(rev->commits, branch, sizeof(branch_t *), commit);
    }

    if (modify && src_branch != NULL && dst_is_root && src_is_root && branch->dirty) {
        parent = commit_cache_get(ctx->commits, change->copyfrom_rev, src_branch);
        if (parent != NULL) {
            commit->parent = parent->mark;
            return SVN_NO_ERROR;
        }
    }

    apr_array_header_t *changes = get_branch_changes(rev, branch);
    change_t *c = apr_array_push(changes);

    node = apr_pcalloc(result_pool, sizeof(node_t));
    node->kind = kind;
    node->path = node_path;

    c->action = action;
    c->node = node;

    if (action == svn_fs_path_change_delete) {
        return SVN_NO_ERROR;
    }

    if (action == svn_fs_path_change_replace && kind == svn_node_dir && src_path == NULL) {
        c->action = svn_fs_path_change_delete;
        return SVN_NO_ERROR;
    }

    SVN_ERR(set_node_mode(&node->mode, rev->root, path, scratch_pool));

    if (change->mergeinfo_mod == svn_tristate_true) {
        SVN_ERR(get_mergeinfo_for_path(&mergeinfo, rev->root, path, scratch_pool, scratch_pool));
    }

    if (mergeinfo != NULL) {
        apr_hash_index_t *idx;
        for (idx = apr_hash_first(scratch_pool, mergeinfo); idx; idx = apr_hash_next(idx)) {
            const char *merge_src_path = apr_hash_this_key(idx);
            merge_src_path = svn_dirent_skip_ancestor("/", merge_src_path);
            svn_rangelist_t *merge_ranges = apr_hash_this_val(idx);
            svn_merge_range_t *last_range;
            svn_revnum_t last_merged;

            merge_branch = branch_storage_lookup_path(ctx->branches, merge_src_path, scratch_pool);
            if (merge_branch == NULL) {
                continue;
            }

            last_range = &APR_ARRAY_IDX(merge_ranges, merge_ranges->nelts - 1, svn_merge_range_t);

            if (last_range->start < last_range->end) {
                last_merged = last_range->end;
            } else {
                last_merged = last_range->start;
            }

            if (last_merged > rev->revnum) {
                last_merged = rev->revnum - 1;
            }

            parent = commit_cache_get(ctx->commits, last_merged, merge_branch);
            if (parent != NULL) {
                commit_cache_add_merge(ctx->commits, commit, parent, scratch_pool);
            }
        }
    }

    if (src_branch != NULL) {
        parent = commit_cache_get(ctx->commits, change->copyfrom_rev, src_branch);
        if (parent != NULL) {
            commit_cache_add_merge(ctx->commits, commit, parent, scratch_pool);
        }
    }

    if (kind == svn_node_file) {
        SVN_ERR(set_content_checksum(&node->checksum, &node->cached,
                                     dst, ctx->blobs, rev->root, path,
                                     result_pool, scratch_pool));
        return SVN_NO_ERROR;
    }

    if (kind == svn_node_dir && src_branch != NULL) {
        const char *src_node_path = svn_relpath_skip_ancestor(src_branch->path, src_path);
        svn_fs_t *fs = svn_fs_root_fs(rev->root);
        svn_fs_root_t *src_root;
        tree_t *ignores = NULL;

        // Ignore paths of sub-branches
        const tree_t *sub_branches = tree_subtree(ctx->branches->tree, src_path, scratch_pool);
        // Ignore by absolute path
        const tree_t *abs_ignores = tree_subtree(ctx->absignores, src_path, scratch_pool);
        // Ignore by relative path
        const tree_t *rel_ignores = tree_subtree(ctx->ignores, src_node_path, scratch_pool);
        // Do not ignore by absolute path
        const tree_t *no_ignores = tree_subtree(ctx->no_ignores, src_path, scratch_pool);

        tree_merge(&ignores, sub_branches, abs_ignores, scratch_pool);
        tree_merge(&ignores, ignores, rel_ignores, scratch_pool);
        tree_diff(&ignores, ignores, no_ignores, scratch_pool);

        SVN_ERR(svn_fs_revision_root(&src_root, fs, change->copyfrom_rev, scratch_pool));
        SVN_ERR(set_tree_checksum(&node->checksum, &node->cached, &node->entries,
                                  dst, ctx->blobs, src_root, src_path,
                                  src_path, node->path, ignores,
                                  result_pool, scratch_pool));
    }

    return SVN_NO_ERROR;
}

static svn_error_t *
reset_branch(svn_stream_t *dst,
             branch_t *branch,
             commit_t *commit,
             apr_pool_t *pool)
{
    SVN_ERR(svn_stream_printf(dst, pool, "reset %s\n", branch->refname));
    SVN_ERR(svn_stream_printf(dst, pool, "from :%d\n", commit->mark));

    return SVN_NO_ERROR;
}

static svn_error_t *
node_modify(svn_stream_t *dst, const node_t *node, apr_pool_t *pool)
{
    const char *checksum;
    checksum = svn_checksum_to_cstring_display(node->checksum, pool);

    if (node->kind == svn_node_dir && node->cached == FALSE) {
        for (int i = 0; i < node->entries->nelts; i++) {
            const node_t *subnode = &APR_ARRAY_IDX(node->entries, i, node_t);
            SVN_ERR(node_modify(dst, subnode, pool));
        }
    } else {
        SVN_ERR(svn_stream_printf(dst, pool, "M %o %s \"%s\"\n",
                                  node->mode, checksum, node->path));
    }

    return SVN_NO_ERROR;
}

static svn_error_t *
node_delete(svn_stream_t *dst, const node_t *node, apr_pool_t *pool)
{
    SVN_ERR(svn_stream_printf(dst, pool, "D \"%s\"\n", node->path));

    return SVN_NO_ERROR;
}

static svn_error_t *
write_commit(svn_stream_t *dst,
             branch_t *branch,
             commit_t *commit,
             revision_t *rev,
             export_ctx_t *ctx,
             apr_pool_t *pool)
{
    apr_array_header_t *changes;
    changes = get_branch_changes(rev, branch);

    if (changes->nelts == 0 && commit->parent) {
        // In case there is only one node in a commit and this node is
        // a root directory copied from another branch, mark this commit
        // as dummy, set copyfrom commit as its parent and reset branch to it.
        commit->mark = commit->parent;
        SVN_ERR(reset_branch(dst, branch, commit, pool));
    } else {
        apr_time_exp_t time_exp;
        commit_cache_set_mark(ctx->commits, commit);
        apr_time_exp_lt(&time_exp, rev->timestamp);

        SVN_ERR(svn_stream_printf(dst, pool, "commit %s\n", branch->refname));
        SVN_ERR(svn_stream_printf(dst, pool, "mark :%d\n", commit->mark));
        SVN_ERR(svn_stream_printf(dst, pool, "committer %s %ld %+.2d%.2d\n",
                                  author_to_cstring(rev->author, pool),
                                  apr_time_sec(rev->timestamp),
                                  time_exp.tm_gmtoff / 3600,
                                  (abs(time_exp.tm_gmtoff) / 60) % 60));
        SVN_ERR(svn_stream_printf(dst, pool, "data %ld\n", rev->message->len));
        SVN_ERR(svn_stream_printf(dst, pool, "%s\n", rev->message->data));

        if (commit->parent) {
            SVN_ERR(svn_stream_printf(dst, pool, "from :%d\n", commit->parent));
        }

        for (int i = 0; i < commit->merges->nelts; i++) {
            mark_t merge = APR_ARRAY_IDX(commit->merges, i, mark_t);
            SVN_ERR(svn_stream_printf(dst, pool, "merge :%d\n", merge));
        }
    }

    for (int i = 0; i < changes->nelts; i++) {
        change_t *change = &APR_ARRAY_IDX(changes, i, change_t);
        switch (change->action) {
        case svn_fs_path_change_add:
        case svn_fs_path_change_modify:
            SVN_ERR(node_modify(dst, change->node, pool));
            break;
        case svn_fs_path_change_delete:
            SVN_ERR(node_delete(dst, change->node, pool));
            break;
        case svn_fs_path_change_replace:
            SVN_ERR(node_delete(dst, change->node, pool));
            SVN_ERR(node_modify(dst, change->node, pool));
        default:
            // noop
            break;
        }
    }

    branch->dirty = FALSE;

    return SVN_NO_ERROR;
}

static svn_error_t *
remove_branch(svn_stream_t *dst, branch_t *branch, apr_pool_t *pool)
{
    SVN_ERR(svn_stream_printf(dst, pool, "reset %s\n", branch->refname));
    SVN_ERR(svn_stream_printf(dst, pool, "from %s\n", NULL_SHA1));

    branch->dirty = TRUE;

    return SVN_NO_ERROR;
}

static svn_error_t *
write_revision(svn_stream_t *dst,
               revision_t *rev,
               export_ctx_t *ctx,
               apr_pool_t *pool)
{
    apr_hash_index_t *idx;
    svn_boolean_t skip;

    skip = (apr_hash_count(rev->commits) == 0 &&
            apr_hash_count(rev->removes) == 0);

    if (skip) {
        SVN_ERR(svn_stream_printf(dst, pool,
                                  "progress Skipped revision %ld\n",
                                  rev->revnum));
        return SVN_NO_ERROR;
    }

    for (idx = apr_hash_first(pool, rev->removes); idx; idx = apr_hash_next(idx)) {
        branch_t *branch = apr_hash_this_val(idx);
        SVN_ERR(remove_branch(dst, branch, pool));
    }

    for (idx = apr_hash_first(pool, rev->commits); idx; idx = apr_hash_next(idx)) {
        branch_t *branch = (branch_t *) apr_hash_this_key(idx);
        commit_t *commit = apr_hash_this_val(idx);
        SVN_ERR(write_commit(dst, branch, commit, rev, ctx, pool));
    }

    SVN_ERR(svn_stream_printf(dst, pool,
                              "progress Imported revision %ld\n",
                              rev->revnum));

    return SVN_NO_ERROR;
}

static svn_error_t *
prepare_changes(apr_array_header_t **dst,
                apr_array_header_t *fs_changes,
                svn_fs_root_t *root,
                export_ctx_t *ctx,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
    apr_array_header_t *changes = apr_array_make(result_pool, 0, sizeof(sort_item_t));

    for (int i = 0; i < fs_changes->nelts; i++) {
        apr_array_header_t *copies, *removes;
        svn_boolean_t remove, modify;
        sort_item_t item = APR_ARRAY_IDX(fs_changes, i, sort_item_t);
        svn_fs_path_change2_t *change = item.value;
        svn_fs_path_change_kind_t action = change->change_kind;

        // Convert dirent paths to relpaths.
        const char *path = svn_dirent_skip_ancestor("/", item.key);
        if (change->copyfrom_known && SVN_IS_VALID_REVNUM(change->copyfrom_rev)) {
            change->copyfrom_path = svn_dirent_skip_ancestor("/", change->copyfrom_path);
        }

        sort_item_t *new_item = apr_array_push(changes);
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
            removes = branch_storage_collect_branches(ctx->branches, path, scratch_pool);

            for (int i = 0; i < removes->nelts; i++) {
                branch_t *branch = APR_ARRAY_IDX(removes, i, branch_t *);
                if (branch_path_is_root(branch, path)) {
                    continue;
                }
                sort_item_t *subitem = apr_array_push(changes);
                svn_fs_path_change2_t *subchange = apr_pcalloc(result_pool, sizeof(svn_fs_path_change2_t));
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
            SVN_ERR(svn_fs_revision_root(&src_root, fs, change->copyfrom_rev, scratch_pool));
            copies = branch_storage_collect_branches(ctx->branches, src_path, scratch_pool);

            for (int i = 0; i < copies->nelts; i++) {
                const char *new_path;
                svn_node_kind_t kind;
                branch_t *branch = APR_ARRAY_IDX(copies, i, branch_t *);
                if (branch_path_is_root(branch, path)) {
                    continue;
                }
                SVN_ERR(svn_fs_check_path(&kind, src_root, branch->path, scratch_pool));
                if (kind == svn_node_none) {
                    continue;
                }

                new_path = svn_relpath_join(path, svn_relpath_skip_ancestor(src_path, branch->path), result_pool);

                sort_item_t *subitem = apr_array_push(changes);
                svn_fs_path_change2_t *subchange = apr_pcalloc(result_pool, sizeof(svn_fs_path_change2_t));
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

export_ctx_t *
export_ctx_create(apr_pool_t *pool)
{
    export_ctx_t *ctx = apr_pcalloc(pool, sizeof(export_ctx_t));
    ctx->authors = author_storage_create(pool);
    ctx->branches = branch_storage_create(pool);
    ctx->commits = commit_cache_create(pool);
    ctx->blobs = checksum_cache_create(pool);
    ctx->ignores = tree_create(pool);
    ctx->absignores = tree_create(pool);
    ctx->no_ignores = tree_create(pool);

    return ctx;
}

svn_error_t *
export_revision_range(svn_stream_t *dst,
                      svn_fs_t *fs,
                      svn_revnum_t lower,
                      svn_revnum_t upper,
                      export_ctx_t *ctx,
                      svn_cancel_func_t cancel_func,
                      apr_pool_t *pool)
{
    apr_pool_t *rev_pool, *scratch_pool;

    rev_pool = svn_pool_create(pool);

    for (svn_revnum_t revnum = lower; revnum <= upper; revnum++) {
        SVN_ERR(cancel_func(NULL));
        apr_array_header_t *changes, *sorted_changes;
        apr_hash_t *fs_changes;
        revision_t *rev;

        svn_pool_clear(rev_pool);

        scratch_pool = svn_pool_create(rev_pool);

        SVN_ERR(get_revision(&rev, revnum, fs, ctx, rev_pool));

        // Fetch the paths changed under revision root.
        SVN_ERR(svn_fs_paths_changed2(&fs_changes, rev->root, rev_pool));
        sorted_changes = sort_hash(fs_changes, compare_items_as_paths, rev_pool);
        SVN_ERR(prepare_changes(&changes, sorted_changes, rev->root, ctx, rev_pool, scratch_pool));

        for (int i = 0; i < changes->nelts; i++) {
            svn_pool_clear(scratch_pool);
            sort_item_t item = APR_ARRAY_IDX(changes, i, sort_item_t);
            SVN_ERR(process_change_record(item.key, item.value, dst, rev, ctx, rev_pool, scratch_pool));
        }

        SVN_ERR(write_revision(dst, rev, ctx, rev_pool));
    }

    SVN_ERR(svn_stream_printf(dst, pool, "done\n"));

    return SVN_NO_ERROR;
}
