/* Copyright (C) 2014-2015 by Maxim Bublis <b@codemonkey.ru>
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
    // Output stream.
    svn_stream_t *out;
    // Response stream.
    svn_stream_t *back;
    // Verbose mode.
    int verbose;
} backend_t;

svn_error_t *
backend_write_commit(const backend_t *be,
                     const branch_t *branch,
                     const commit_t *commit,
                     const apr_array_header_t *nodes,
                     const char *author,
                     const char *message,
                     int64_t timestamp,
                     apr_pool_t *pool);

svn_error_t *
backend_reset_branch(const backend_t *be,
                     const branch_t *branch,
                     const commit_t *commit,
                     apr_pool_t *pool);

svn_error_t *
backend_write_blob_header(const backend_t *be,
                          const blob_t *blob,
                          apr_pool_t *pool);

svn_error_t *
backend_remove_branch(const backend_t *be,
                      const branch_t *branch,
                      apr_pool_t *pool);

svn_error_t *
backend_notify_branch_found(const backend_t *be,
                            const branch_t *branch,
                            apr_pool_t *pool);

svn_error_t *
backend_notify_branch_updated(const backend_t *be,
                              const branch_t *branch,
                              apr_pool_t *pool);

svn_error_t *
backend_notify_branch_removed(const backend_t *be,
                              const branch_t *branch,
                              apr_pool_t *pool);

svn_error_t *
backend_notify_revision_skipped(const backend_t *be,
                                revnum_t revnum,
                                apr_pool_t *pool);

svn_error_t *
backend_notify_revision_imported(const backend_t *be,
                                 revnum_t revnum,
                                 apr_pool_t *pool);

svn_error_t *
backend_get_checksum(svn_checksum_t **dst,
                     const backend_t *be,
                     const commit_t *commit,
                     const char *path,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool);

svn_error_t *
backend_finished(const backend_t *be, apr_pool_t *pool);

#endif // GIT_SVN_FAST_IMPORT_BACKEND_H_
