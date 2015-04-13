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

#ifndef GIT_SVN_FAST_IMPORT_REVISION_H_
#define GIT_SVN_FAST_IMPORT_REVISION_H_

#include "compat.h"
#include "branch.h"
#include "commit.h"
#include <apr_pools.h>
#include <svn_types.h>

typedef svn_error_t * (*revision_commit_handler_t)(void *ctx, branch_t *branch, commit_t *commit);
typedef svn_error_t * (*revision_remove_handler_t)(void *ctx, branch_t *branch);

// Abstract type for revision.
typedef struct revision_t revision_t;

// Returns revision number.
svn_revnum_t
revision_revnum_get(const revision_t *rev);

// Returns revision's commit for branch.
commit_t *
revision_commits_get(const revision_t *rev, const branch_t *b);

// Add commit for branch.
commit_t *
revision_commits_add(const revision_t *rev, branch_t *b);

// Return commits count.
size_t
revision_commits_count(const revision_t *rev);

// Applies function for each commit entry.
svn_error_t *
revision_commits_apply(const revision_t *rev,
                      revision_commit_handler_t apply,
                      void *ctx,
                      apr_pool_t *pool);

// Add branch to remove list.
void
revision_removes_add(const revision_t *rev, const branch_t *b);

// Return branch removes count.
size_t
revision_removes_count(const revision_t *rev);

// Applies function for each branch remove entry.
svn_error_t *
revision_removes_apply(const revision_t *rev,
                       revision_remove_handler_t apply,
                       void *ctx,
                       apr_pool_t *pool);

// Abstract type for revision storage.
typedef struct revision_storage_t revision_storage_t;

// Creates new revision storage.
revision_storage_t *
revision_storage_create(apr_pool_t *pool);

// Creates and returns new revision for revision number.
revision_t *
revision_storage_add_revision(revision_storage_t *rs, svn_revnum_t revnum);

// Returns revision by revision number.
const revision_t *
revision_storage_get_by_revnum(revision_storage_t *rs, svn_revnum_t revnum);

#endif // GIT_SVN_FAST_IMPORT_REVISION_H_
