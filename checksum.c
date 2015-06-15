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

#include "checksum.h"

struct checksum_cache_t
{
    apr_hash_t *cache;
    apr_pool_t *pool;
};

checksum_cache_t *
checksum_cache_create(apr_pool_t *pool)
{
    checksum_cache_t *c = apr_pcalloc(pool, sizeof(checksum_cache_t));
    c->pool = pool;
    c->cache = apr_hash_make(pool);

    return c;
}

svn_checksum_t *
checksum_cache_get(checksum_cache_t *c,
                   const svn_checksum_t *svn_checksum)
{
    return apr_hash_get(c->cache, svn_checksum->digest, svn_checksum_size(svn_checksum));
}

void
checksum_cache_set(checksum_cache_t *c,
                   const svn_checksum_t *svn_checksum,
                   const svn_checksum_t *git_checksum)
{
    svn_checksum_t *key = svn_checksum_dup(svn_checksum, c->pool);
    svn_checksum_t *val = svn_checksum_dup(git_checksum, c->pool);
    apr_hash_set(c->cache, key->digest, svn_checksum_size(key), val);
}

typedef struct
{
    svn_stream_t *stream;
    svn_checksum_ctx_t *read_ctx;
    svn_checksum_ctx_t *write_ctx;
} stream_ctx_t;

static svn_error_t *
read_handler(void *stream_ctx, char *buf, apr_size_t *len)
{
    stream_ctx_t *ctx = stream_ctx;

    SVN_ERR(svn_stream_read(ctx->stream, buf, len));

    if (ctx->read_ctx != NULL) {
        SVN_ERR(svn_checksum_update(ctx->read_ctx, buf, *len));
    }

    return SVN_NO_ERROR;
}

static svn_error_t *
write_handler(void *stream_ctx, const char *data, apr_size_t *len)
{
    stream_ctx_t *ctx = stream_ctx;

    if (ctx->write_ctx != NULL) {
        SVN_ERR(svn_checksum_update(ctx->write_ctx, data, *len));
    }

    SVN_ERR(svn_stream_write(ctx->stream, data, len));

    return SVN_NO_ERROR;
}

static svn_error_t *
close_handler(void *stream_ctx)
{
    stream_ctx_t *ctx = stream_ctx;

    SVN_ERR(svn_stream_close(ctx->stream));

    return SVN_NO_ERROR;
}

svn_stream_t *
checksum_stream_create(svn_stream_t *s,
                       svn_checksum_ctx_t *read_ctx,
                       svn_checksum_ctx_t *write_ctx,
                       apr_pool_t *pool)
{
    stream_ctx_t *ctx = apr_pcalloc(pool, sizeof(stream_ctx_t));
    ctx->stream = s;
    ctx->read_ctx = read_ctx;
    ctx->write_ctx = write_ctx;

    svn_stream_t *stream = svn_stream_create(ctx, pool);
    svn_stream_set_read(stream, read_handler);
    svn_stream_set_write(stream, write_handler);
    svn_stream_set_close(stream, close_handler);

    return stream;
}
