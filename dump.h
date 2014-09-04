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

#ifndef GIT_SVN_FAST_IMPORT_DUMP_H_
#define GIT_SVN_FAST_IMPORT_DUMP_H_

#include "types.h"

#include <svn_io.h>

svn_error_t *
git_svn_dump_revision_begin(svn_stream_t *, git_svn_revision_t *, apr_pool_t *);

svn_error_t *
git_svn_dump_revision_end(svn_stream_t *, git_svn_revision_t *, apr_pool_t *);

svn_error_t *
git_svn_dump_node(svn_stream_t *, git_svn_node_t *, apr_pool_t *);

svn_error_t *
git_svn_dump_blob_header(svn_stream_t *, git_svn_blob_t *, apr_pool_t *);

#endif // GIT_SVN_FAST_IMPORT_DUMP_H_