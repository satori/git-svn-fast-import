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

#ifndef GIT_SVN_FAST_IMPORT_BRANCH_H_
#define GIT_SVN_FAST_IMPORT_BRANCH_H_

#include "compat.h"
#include "commit.h"
#include "tree.h"
#include <apr_pools.h>

// Abstract type for branch.
typedef struct branch_t branch_t;

// Returns branch reference name.
const char *
branch_refname_get(const branch_t *b);

// Skips branch path prefix from absolute path.
const char *
branch_skip_prefix(const branch_t *b, const char *path);

// Returns branch head commit.
const commit_t *
branch_head_get(const branch_t *b);

// Set commit as branch head.
void
branch_head_set(branch_t *b, const commit_t *c);

// Tests if path is branch's root
bool
branch_path_is_root(const branch_t *b, const char *path);

// Abstract type for branch storage.
typedef struct branch_storage_t branch_storage_t;

// Create new branch storage.
// Use bpfx as branch path prefixes.
// Use tpfx as tag path prefixes. 
branch_storage_t *
branch_storage_create(apr_pool_t *pool, tree_t *bpfx, tree_t *tpfx);

// Add new branch reference name and path.
branch_t *
branch_storage_add_branch(branch_storage_t *bs, const char *ref, const char *path);

// Lookup a branch by path.
branch_t *
branch_storage_lookup_path(branch_storage_t *bs, const char *path);

#endif // GIT_SVN_FAST_IMPORT_BRANCH_H_
