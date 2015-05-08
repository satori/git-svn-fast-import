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
#include <svn_fs.h>

int
svn_sort_compare_items_gitlike(const svn_sort__item_t *a,
                               const svn_sort__item_t *b)
{
    int val;
    apr_size_t len;
    svn_fs_dirent_t *entry;

    // Compare bytes of a's key and b's key up to the common length
    len = (a->klen < b->klen) ? a->klen : b->klen;
    val = memcmp(a->key, b->key, len);
    if (val != 0) {
        return val;
    }

    // They match up until one of them ends.
    // If lesser key stands for directory entry,
    // compare "/" suffix with the rest of the larger key.
    // Otherwise whichever is longer is greater.
    if (a->klen < b->klen) {
        entry = a->value;
        if (entry->kind == svn_node_dir) {
            return memcmp("/", b->key + len, 1);
        }
        return -1;
    } else if (a->klen > b->klen) {
        entry = b->value;
        if (entry->kind == svn_node_dir) {
            return memcmp(a->key + len, "/", 1);
        }
        return 1;
    }

    return 0;
}
