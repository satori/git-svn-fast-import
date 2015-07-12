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

#include "tree.h"
#include <svn_hash.h>
#include <svn_path.h>

tree_t *
tree_create(apr_pool_t *pool)
{
    tree_t *t = apr_pcalloc(pool, sizeof(tree_t));
    t->pool = pool;
    t->nodes = apr_hash_make(pool);
    t->value = NULL;

    return t;
}

void
tree_copy(tree_t **dst, const tree_t *src, apr_pool_t *pool)
{
    apr_hash_index_t *idx;
    tree_t *t = tree_create(pool);
    t->value = src->value;

    for (idx = apr_hash_first(pool, src->nodes); idx; idx = apr_hash_next(idx)) {
        const char *key = apr_hash_this_key(idx);
        const tree_t *val = apr_hash_this_val(idx);
        tree_t *subtree;

        tree_copy(&subtree, val, pool);
        svn_hash_sets(t->nodes, apr_pstrdup(t->pool, key), subtree);
    }

    *dst = t;
}

void
tree_merge(tree_t **dst, const tree_t *t1, const tree_t *t2, apr_pool_t *pool)
{
    apr_hash_index_t *idx;

    if (t1 == NULL) {
        return tree_copy(dst, t2, pool);
    }

    if (t2 == NULL) {
        return tree_copy(dst, t1, pool);
    }

    tree_t *t = tree_create(pool);

    for (idx = apr_hash_first(pool, t1->nodes); idx; idx = apr_hash_next(idx)) {
        const char *key1 = apr_hash_this_key(idx);
        const tree_t *val1 = apr_hash_this_val(idx);
        const tree_t *val2 = svn_hash_gets(t2->nodes, key1);
        tree_t *subtree;
        tree_merge(&subtree, val1, val2, pool);

        svn_hash_sets(t->nodes, apr_pstrdup(t->pool, key1), subtree);
    }

    for (idx = apr_hash_first(pool, t2->nodes); idx; idx = apr_hash_next(idx)) {
        const char *key2 = apr_hash_this_key(idx);
        const tree_t *val2 = apr_hash_this_val(idx);
        const tree_t *val1 = svn_hash_gets(t1->nodes, key2);

        if (val1 == NULL) {
            tree_t *subtree;
            tree_copy(&subtree, val2, pool);

            svn_hash_sets(t->nodes, apr_pstrdup(t->pool, key2), subtree);
        }
    }

    *dst = t;
}

void
tree_insert(tree_t *t,
            const char *path,
            const void *value,
            apr_pool_t *pool)
{
    apr_array_header_t *components = svn_path_decompose(path, pool);

    for (int i = 0; i < components->nelts; i++) {
        const char *key = APR_ARRAY_IDX(components, i, const char *);
        tree_t *subtree = svn_hash_gets(t->nodes, key);

        if (subtree == NULL) {
            subtree = tree_create(t->pool);
            svn_hash_sets(t->nodes, apr_pstrdup(t->pool, key), subtree);
        }

        t = subtree;
    }

    t->value = value;
}

const void *
tree_match(const tree_t *t, const char *path, apr_pool_t *pool)
{
    const void *value = t->value;
    apr_array_header_t *components = svn_path_decompose(path, pool);

    for (int i = 0; i < components->nelts; i++) {
        const char *key = APR_ARRAY_IDX(components, i, const char *);
        tree_t *subtree = svn_hash_gets(t->nodes, key);

        if (subtree == NULL) {
            break;
        }

        if (subtree->value != NULL) {
            value = subtree->value;
        }

        t = subtree;
    }

    return value;
}

const tree_t *
tree_subtree(const tree_t *t, const char *path, apr_pool_t *pool)
{
    apr_array_header_t *components = svn_path_decompose(path, pool);

    for (int i = 0; i < components->nelts; i++) {
        const char *key = APR_ARRAY_IDX(components, i, const char *);
        t = svn_hash_gets(t->nodes, key);
        if (t == NULL) {
            break;
        }
    }

    return t;
}
