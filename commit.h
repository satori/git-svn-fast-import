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

#include "branch.h"
#include <inttypes.h>
#include <svn_io.h>

typedef uint32_t mark_t;

typedef struct
{
    svn_revnum_t revnum;
    branch_t *branch;
    mark_t mark;
    mark_t copyfrom;
    apr_array_header_t *merges;
} commit_t;

typedef struct
{
    apr_pool_t *pool;
    apr_array_header_t *commits;
    apr_hash_t *idx;
    apr_hash_t *marks;
    mark_t last_mark;
} commit_cache_t;

commit_cache_t *
commit_cache_create(apr_pool_t *pool);

commit_t *
commit_cache_get(commit_cache_t *c, svn_revnum_t revnum, branch_t *branch);

commit_t *
commit_cache_get_by_mark(commit_cache_t *c, mark_t mark);

void
commit_cache_set_mark(commit_cache_t *c, commit_t *commit);

commit_t *
commit_cache_add(commit_cache_t *c, svn_revnum_t revnum, branch_t *branch);

svn_error_t *
commit_cache_dump(commit_cache_t *c, svn_stream_t *dst, apr_pool_t *pool);

svn_error_t *
commit_cache_dump_path(commit_cache_t *c, const char *path, apr_pool_t *pool);

#endif // GIT_SVN_FAST_IMPORT_COMMIT_H_
