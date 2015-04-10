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

#ifndef GIT_SVN_FAST_IMPORT_COMMIT_H_
#define GIT_SVN_FAST_IMPORT_COMMIT_H_

#include "compat.h"
#include "mark.h"
#include <apr_pools.h>

// Abstract type for commit.
typedef struct commit_t commit_t;

// Create new commit.
commit_t *
commit_create(apr_pool_t *pool);

// Return commit mark.
mark_t
commit_mark_get(const commit_t *c);

// Set commit mark.
void
commit_mark_set(commit_t *c, mark_t mark);

// Return commit's copyfrom.
const commit_t *
commit_copyfrom_get(const commit_t *c);

// Set commit's copyfrom.
void
commit_copyfrom_set(commit_t *c, const commit_t *copyfrom);

// Return commit parent.
const commit_t *
commit_parent_get(const commit_t *c);

// Set commit parent.
void
commit_parent_set(commit_t *c, const commit_t *p);

// Set commit as dummy.
void
commit_dummy_set(commit_t *c);

#endif // GIT_SVN_FAST_IMPORT_COMMIT_H_
