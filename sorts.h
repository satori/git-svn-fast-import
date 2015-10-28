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


#ifndef GIT_SVN_FAST_IMPORT_SORTS_H_
#define GIT_SVN_FAST_IMPORT_SORTS_H_

#include <apr_hash.h>
#include <apr_tables.h>

typedef struct
{
    const void *key;
    apr_ssize_t klen;
    void *value;
} sort_item_t;

int
compare_items_as_paths(const sort_item_t *a, const sort_item_t *b);

apr_array_header_t *
sort_hash(apr_hash_t *ht,
          int (*comparison_func)(const sort_item_t *,
                                 const sort_item_t *),
          apr_pool_t *pool);

#endif // GIT_SVN_FAST_IMPORT_SORTS_H_
