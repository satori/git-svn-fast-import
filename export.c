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
        svn_time_from_cstring(&r->timestamp, value->data, pool);
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

static mark_t
get_copyfrom_commit(svn_revnum_t revnum, export_ctx_t *ctx, branch_t *copyfrom_branch)
{
    while (revnum > 0) {
        commit_t *commit = NULL;
        commit = commit_cache_get(ctx->commits, revnum, copyfrom_branch);
        if (commit != NULL) {
            return commit->mark;
        }

        --revnum;
    }

    return 0;
}

static svn_error_t *
process_change_record(const char *path,
                      svn_fs_path_change2_t *change,
                      svn_stream_t *dst,
                      revision_t *rev,
                      export_ctx_t *ctx,
                      apr_pool_t *pool)
{
    const char *src_path = NULL, *node_path, *ignored;
    commit_t *commit;
    branch_t *branch = NULL, *src_branch = NULL;
    node_t *node;
    svn_boolean_t dst_is_root = FALSE, src_is_root = FALSE;
    svn_boolean_t modify;
    svn_fs_path_change_kind_t action = change->change_kind;
    svn_node_kind_t kind = change->node_kind;
    svn_revnum_t src_rev = change->copyfrom_rev;

    ignored = tree_match(ctx->absignores, path, pool);
    if (ignored != NULL) {
        return SVN_NO_ERROR;
    }

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
        apr_hash_set(rev->removes, branch, sizeof(branch_t *), branch);
        return SVN_NO_ERROR;
    }

    node_path = svn_relpath_skip_ancestor(branch->path, path);
    if (node_path == NULL) {
        return SVN_NO_ERROR;
    }

    ignored = tree_match(ctx->ignores, node_path, pool);
    if (ignored != NULL) {
        return SVN_NO_ERROR;
    }

    commit = apr_hash_get(rev->commits, branch, sizeof(branch_t *));
    if (commit == NULL) {
        commit = commit_cache_add(ctx->commits, rev->revnum, branch);
        apr_hash_set(rev->commits, branch, sizeof(branch_t *), commit);
    }

    if (modify && dst_is_root && src_is_root) {
        commit->copyfrom = get_copyfrom_commit(change->copyfrom_rev, ctx, src_branch);
        return SVN_NO_ERROR;
    }

    apr_array_header_t *changes = get_branch_changes(rev, branch);
    change_t *c = apr_array_push(changes);

    node = apr_pcalloc(pool, sizeof(node_t));
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

    SVN_ERR(set_node_mode(&node->mode, rev->root, path, pool));

    if (kind == svn_node_file) {
        SVN_ERR(set_content_checksum(&node->checksum, dst, ctx->blobs,
                                     rev->root, path, pool));
        return SVN_NO_ERROR;
    }

    if (kind == svn_node_dir && src_branch != NULL) {
        apr_array_header_t *dummy;
        svn_fs_t *fs = svn_fs_root_fs(rev->root);
        svn_fs_root_t *src_root;
        tree_t *ignores;
        const tree_t *subbranches = tree_subtree(ctx->branches->tree, src_branch->path, pool);
        tree_merge(&ignores, ctx->ignores, subbranches, pool);

        SVN_ERR(svn_fs_revision_root(&src_root, fs, change->copyfrom_rev, pool));
        SVN_ERR(set_tree_checksum(&node->checksum, &dummy, dst, ctx->blobs,
                                  src_root, src_path, src_branch->path, ignores,
                                  pool));
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
write_commit(svn_stream_t *dst,
             branch_t *branch,
             commit_t *commit,
             revision_t *rev,
             export_ctx_t *ctx,
             apr_pool_t *pool)
{
    apr_array_header_t *changes;
    changes = get_branch_changes(rev, branch);

    if (changes->nelts == 0 && commit->copyfrom) {
        // In case there is only one node in a commit and this node is
        // a root directory copied from another branch, mark this commit
        // as dummy, set copyfrom commit as its parent and reset branch to it.
        commit->mark = commit->copyfrom;
        SVN_ERR(reset_branch(dst, branch, commit, pool));
    } else {
        commit->mark = ctx->commits->last_mark++;

        SVN_ERR(svn_stream_printf(dst, pool, "commit %s\n", branch->refname));
        SVN_ERR(svn_stream_printf(dst, pool, "mark :%d\n", commit->mark));
        SVN_ERR(svn_stream_printf(dst, pool, "committer %s %ld +0000\n",
                                  author_to_cstring(rev->author, pool),
                                  apr_time_sec(rev->timestamp)));
        SVN_ERR(svn_stream_printf(dst, pool, "data %ld\n", rev->message->len));
        SVN_ERR(svn_stream_printf(dst, pool, "%s\n", rev->message->data));

        if (commit->copyfrom) {
            SVN_ERR(svn_stream_printf(dst, pool, "from :%d\n", commit->copyfrom));
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

    return SVN_NO_ERROR;
}

static svn_error_t *
remove_branch(svn_stream_t *dst, branch_t *branch, apr_pool_t *pool)
{
    SVN_ERR(svn_stream_printf(dst, pool, "reset %s\n", branch->refname));
    SVN_ERR(svn_stream_printf(dst, pool, "from %s\n", NULL_SHA1));

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
                      export_ctx_t *ctx,
                      svn_cancel_func_t cancel_func,
                      apr_pool_t *pool)
{
    apr_pool_t *subpool;

    subpool = svn_pool_create(pool);

    for (svn_revnum_t revnum = lower; revnum <= upper; revnum++) {
        SVN_ERR(cancel_func(NULL));
        apr_array_header_t *changes, *sorted_changes;
        apr_hash_t *fs_changes;
        revision_t *rev;

        svn_pool_clear(subpool);

        SVN_ERR(get_revision(&rev, revnum, fs, ctx, subpool));

        // Fetch the paths changed under revision root.
        SVN_ERR(svn_fs_paths_changed2(&fs_changes, rev->root, subpool));
        sorted_changes = svn_sort__hash(fs_changes, svn_sort_compare_items_lexically, subpool);
        SVN_ERR(prepare_changes(&changes, sorted_changes, rev->root, ctx, subpool));

        for (int i = 0; i < changes->nelts; i++) {
            svn_sort__item_t item = APR_ARRAY_IDX(changes, i, svn_sort__item_t);
            SVN_ERR(process_change_record(item.key, item.value, dst, rev, ctx, subpool));
        }

        SVN_ERR(write_revision(dst, rev, ctx, subpool));
    }

    SVN_ERR(svn_stream_printf(dst, pool, "done\n"));

    return SVN_NO_ERROR;
}
