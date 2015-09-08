/* Copyright (C) 2015 by Maxim Bublis <b@codemonkey.ru>
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

#include "branch.h"
#include "utils.h"
#include <apr_strings.h>
#include <svn_dirent_uri.h>
#include <svn_string.h>

svn_boolean_t
branch_path_is_root(const branch_t *b, const char *path)
{
    return strcmp(b->path, path) == 0;
}

const char *
branch_refname_from_path(const char *path, apr_pool_t *pool)
{
    apr_array_header_t *strings = svn_cstring_split(path, "/", FALSE, pool);
    svn_stringbuf_t *buf = svn_stringbuf_create_empty(pool);

    for (int i = 0; i < strings->nelts; i++) {
        const char *str = APR_ARRAY_IDX(strings, i, const char *);
        if (buf->len > 0) {
            svn_stringbuf_appendcstr(buf, "--");
        }
        svn_stringbuf_appendcstr(buf, str);
    }

    return apr_psprintf(pool, "refs/heads/%s", buf->data);
}

branch_storage_t *
branch_storage_create(apr_pool_t *pool)
{
    branch_storage_t *bs = apr_pcalloc(pool, sizeof(branch_storage_t));

    bs->pool = pool;
    bs->tree = tree_create(pool);
    bs->pfx = tree_create(pool);

    return bs;
}

void
branch_storage_add_prefix(branch_storage_t *bs,
                          const char *pfx,
                          svn_boolean_t is_tag,
                          apr_pool_t *pool)
{
    tree_insert(bs->pfx, pfx, pfx, pool);
}

branch_t *
branch_storage_add_branch(branch_storage_t *bs,
                          const char *refname,
                          const char *path,
                          apr_pool_t *pool)
{
    branch_t *b = apr_pcalloc(bs->pool, sizeof(branch_t));
    b->refname = refname;
    b->path = path;
    b->dirty = TRUE;

    tree_insert(bs->tree, b->path, b, pool);

    return b;
}

branch_t *
branch_storage_lookup_path(branch_storage_t *bs, const char *path, apr_pool_t *pool)
{
    branch_t *branch;
    const char *branch_path, *prefix, *refname, *root, *subpath;

    branch = (branch_t *) tree_match(bs->tree, path, pool);
    if (branch != NULL) {
        return branch;
    }

    prefix = tree_match(bs->pfx, path, pool);
    if (prefix == NULL) {
        return NULL;
    }

    subpath = svn_dirent_skip_ancestor(prefix, path);
    if (subpath == NULL) {
        return NULL;
    }

    if (*subpath == '\0') {
        return NULL;
    }

    root = strchr(subpath, '/');
    if (root == NULL) {
        branch_path = apr_pstrdup(bs->pool, path);
    } else {
        branch_path = apr_pstrndup(bs->pool, path, root - path);
    }

    refname = branch_refname_from_path(branch_path, bs->pool);

    return branch_storage_add_branch(bs, refname, branch_path, pool);
}

static void
collect_values(const tree_t *t, apr_array_header_t *values, apr_pool_t *pool)
{
    apr_hash_index_t *idx;

    if (t->value != NULL) {
        APR_ARRAY_PUSH(values, const branch_t *) = t->value;
    }

    for (idx = apr_hash_first(pool, t->nodes); idx; idx = apr_hash_next(idx)) {
        const tree_t *subtree = apr_hash_this_val(idx);
        collect_values(subtree, values, pool);
    }
}

apr_array_header_t *
branch_storage_collect_branches(branch_storage_t *bs, const char *path, apr_pool_t *pool)
{
    apr_array_header_t *values = apr_array_make(pool, 0, sizeof(branch_t *));
    const tree_t *t = tree_subtree(bs->tree, path, pool);

    if (t != NULL) {
        collect_values(t, values, pool);
    }

    return values;
}
