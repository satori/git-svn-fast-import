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

typedef struct
{
    // Output fileno.
    int out;
    // Response fileno.
    int back;
    // Verbose mode.
    int verbose;
} backend_t;

git_svn_status_t
backend_write_commit(backend_t *be,
                     const commit_t *commit,
                     const apr_array_header_t *nodes,
                     const char *author,
                     const char *message,
                     int64_t timestamp);

git_svn_status_t
backend_write_blob_header(backend_t *be, const blob_t *blob);

git_svn_status_t
backend_notify_branch_found(backend_t *be, const branch_t *branch);

git_svn_status_t
backend_notify_branch_updated(backend_t *be, const branch_t *branch);

git_svn_status_t
backend_notify_revision_skipped(backend_t *be, revnum_t revnum);

git_svn_status_t
backend_notify_revision_imported(backend_t *be, revnum_t revnum);

git_svn_status_t
backend_get_checksum(backend_t *be,
                     uint8_t *dst,
                     const commit_t *commit,
                     const char *path);

git_svn_status_t
backend_finished(backend_t *be);

#endif // GIT_SVN_FAST_IMPORT_BACKEND_H_
