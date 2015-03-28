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

#include "symlink.h"

typedef struct {
    svn_stream_t *out;
    size_t remaining;
} stream_ctx_t;

static svn_error_t *
write_handler(void *stream_ctx, const char *data, apr_size_t *len)
{
    stream_ctx_t *ctx = stream_ctx;
    apr_size_t wlen = *len;

    if (ctx->remaining >= wlen) {
        ctx->remaining -= wlen;
        return SVN_NO_ERROR;
    }

    if (ctx->remaining > 0) {
        data += ctx->remaining;
        wlen -= ctx->remaining;
        ctx->remaining = 0;
    }

    return svn_stream_write(ctx->out, data, &wlen);
}

static svn_error_t *
close_handler(void *stream_ctx)
{
    stream_ctx_t *ctx = stream_ctx;

    return svn_stream_close(ctx->out);
}

svn_stream_t *
symlink_content_stream_create(svn_stream_t *out, apr_pool_t *pool)
{
    stream_ctx_t *ctx = apr_pcalloc(pool, sizeof(stream_ctx_t));
    ctx->out = out;
    ctx->remaining = sizeof(SYMLINK_CONTENT_PREFIX);

    svn_stream_t *stream = svn_stream_create(ctx, pool);
    svn_stream_set_write(stream, write_handler);
    svn_stream_set_close(stream, close_handler);

    return stream;
}
