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

#ifndef GIT_SVN_FAST_IMPORT_BACKEND_H_
#define GIT_SVN_FAST_IMPORT_BACKEND_H_

#include "error.h"
#include "types.h"

#include <svn_io.h>

typedef struct
{
    svn_stream_t *out;
    svn_stream_t *back;
} backend_t;

git_svn_status_t
backend_write_revision(backend_t *be, revision_t *rev, apr_array_header_t *nodes, apr_pool_t *pool);

git_svn_status_t
backend_write_blob_header(backend_t *be, blob_t *blob, apr_pool_t *pool);

git_svn_status_t
backend_notify_branch_found(backend_t *be, branch_t *branch, apr_pool_t *pool);

git_svn_status_t
backend_notify_skip_revision(backend_t *be, revision_t *rev, apr_pool_t *pool);

git_svn_status_t
backend_get_checksum(backend_t *be, uint8_t *sha1, revision_t *rev, const char *path, apr_pool_t *pool);

#endif // GIT_SVN_FAST_IMPORT_BACKEND_H_
