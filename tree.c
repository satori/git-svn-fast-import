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
tree_match(const tree_t *t,
           const char *path,
           apr_pool_t *pool)
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
