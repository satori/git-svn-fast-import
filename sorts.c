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

#include "sorts.h"
#include <stdlib.h>
#include <svn_path.h>

int
compare_items_as_paths(const sort_item_t *a, const sort_item_t *b)
{
    return svn_path_compare_paths(a->key, b->key);
}

apr_array_header_t *
sort_hash(apr_hash_t *ht,
          int (*comparison_func)(const sort_item_t *,
                                 const sort_item_t *),
          apr_pool_t *pool)
{
    apr_array_header_t *sorted;
    apr_hash_index_t *idx;

    sorted = apr_array_make(pool, apr_hash_count(ht), sizeof(sort_item_t));

    for (idx = apr_hash_first(pool, ht); idx; idx = apr_hash_next(idx)) {
        sort_item_t *item = apr_array_push(sorted);
        apr_hash_this(idx, &item->key, &item->klen, &item->value);
    }

    qsort(sorted->elts, sorted->nelts, sorted->elt_size,
          (int (*)(const void *, const void *))comparison_func);

    return sorted;
}
