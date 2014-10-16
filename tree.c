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

tree_t *
tree_create(apr_pool_t *pool)
{
    tree_t *t = apr_pcalloc(pool, sizeof(tree_t));
    t->pool = pool;
    t->nodes = apr_hash_make(pool);
    t->value = NULL;

    return t;
}

static size_t
iter_first(const char *key) {
    const char *next = strchr(key, '/');
    if (next != NULL) {
        return next - key;
    }
    return strlen(key);
}

static size_t
iter_next(const char **next, size_t len)
{
    const char *key = *next;
    const char *nextKey = key + len;
    if (*nextKey == '/') {
        nextKey++;
    }

    *next = nextKey;

    return iter_first(nextKey);
}

void
tree_insert(tree_t *t, const char *key, const void *value)
{
    for (size_t len = iter_first(key); len; len = iter_next(&key, len)) {
        tree_t *subtree = apr_hash_get(t->nodes, key, len);
        if (subtree == NULL) {
            subtree = tree_create(t->pool);
            apr_hash_set(t->nodes, key, len, subtree);
        }
        t = subtree;
    }

    t->value = value;
}

const void *
tree_find_longest_prefix(const tree_t *t, const char *key)
{
    for (size_t len = iter_first(key); len; len = iter_next(&key, len)) {
        tree_t *subtree = apr_hash_get(t->nodes, key, len);
        if (subtree == NULL) {
            break;
        }
        t = subtree;
    }

    return t->value;
}

const void *
tree_find_exact(const tree_t *t, const char *key)
{
    for (size_t len = iter_first(key); len; len = iter_next(&key, len)) {
        tree_t *subtree = apr_hash_get(t->nodes, key, len);
        if (subtree == NULL) {
            return NULL;
        }
        t = subtree;
    }

    return t->value;
}
