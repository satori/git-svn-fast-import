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

#include "trie.h"

trie_t *
trie_create(apr_pool_t *pool)
{
    trie_t *t = apr_pcalloc(pool, sizeof(trie_t));
    t->pool = pool;
    return t;
}

void
trie_insert(trie_t *t, const char *key, void *value)
{
    unsigned char c;
    while ((c = *key++)) {
        if (t->chars[c] == NULL) {
            t->chars[c] = trie_create(t->pool);
        }
        t = t->chars[c];
    }
    t->value = value;
}

void *
trie_find_prefix(trie_t *t, const char *key)
{
    unsigned char c;
    while ((c = *key++)) {
        if (t->value != NULL || t->chars[c] == NULL) {
            break;
        }
        t = t->chars[c];
    }
    return t->value;
}
