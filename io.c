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

#include "io.h"

#include <string.h>

git_svn_status_t
io_printf(svn_stream_t *out, apr_pool_t *pool, const char *fmt, ...)
{
    const char *message;
    size_t len;
    va_list args;
    svn_error_t *err;

    va_start(args, fmt);
    message = apr_pvsprintf(pool, fmt, args);
    va_end(args);

    len = strlen(message);

    err = svn_stream_write(out, message, &len);
    if (err) {
        handle_svn_error(err);
        return GIT_SVN_FAILURE;
    }

    return GIT_SVN_SUCCESS;
}

git_svn_status_t
io_readline(svn_stream_t *in, const char **dst, apr_pool_t *pool)
{
    svn_error_t *err;
    svn_boolean_t eof;
    svn_stringbuf_t *buf;

    err = svn_stream_readline(in, &buf, "\n", &eof, pool);
    if (err) {
        handle_svn_error(err);
        return GIT_SVN_FAILURE;
    }

    *dst = buf->data;

    return GIT_SVN_SUCCESS;
}
