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

#ifndef GIT_SVN_FAST_IMPORT_TRIE_H_
#define GIT_SVN_FAST_IMPORT_TRIE_H_

#include "error.h"

#include <apr_pools.h>

#define TRIE_SIZE 256

typedef struct git_svn_trie_t
{
    apr_pool_t *pool;
    void *value;
    struct git_svn_trie_t *chars[TRIE_SIZE];
} git_svn_trie_t;

git_svn_trie_t *
git_svn_trie_create(apr_pool_t *pool);

void
git_svn_trie_insert(git_svn_trie_t *t, const char *key, void *value);

void *
git_svn_trie_find_prefix(git_svn_trie_t *t, const char *key);

#endif // GIT_SVN_FAST_IMPORT_TRIE_H_
