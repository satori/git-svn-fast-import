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
#include <svn_hash.h>
#include <svn_props.h>

#define SYMLINK_CONTENT_PREFIX "link"

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

// Creates a stream that updates checksum context for all data
// read and written.
static svn_stream_t *
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

svn_error_t *
set_content_checksum(svn_checksum_t **checksum,
                     svn_stream_t *output,
                     checksum_cache_t *cache,
                     svn_fs_root_t *root,
                     const char *path,
                     apr_pool_t *pool)
{
    const char *hdr;
    apr_hash_t *props;
    svn_checksum_t *svn_checksum, *git_checksum;
    svn_checksum_ctx_t *ctx;
    svn_filesize_t size;
    svn_stream_t *content;

    SVN_ERR(svn_fs_file_checksum(&svn_checksum, svn_checksum_sha1,
                                 root, path, FALSE, pool));

    git_checksum = checksum_cache_get(cache, svn_checksum);
    if (git_checksum != NULL) {
        *checksum = git_checksum;
        return SVN_NO_ERROR;
    }

    SVN_ERR(svn_fs_node_proplist(&props, root, path, pool));
    SVN_ERR(svn_fs_file_length(&size, root, path, pool));
    SVN_ERR(svn_fs_file_contents(&content, root, path, pool));

    // We need to strip a symlink marker from the beginning of a content
    // and subtract a symlink marker length from the blob size.
    if (svn_hash_gets(props, SVN_PROP_SPECIAL)) {
        apr_size_t skip = sizeof(SYMLINK_CONTENT_PREFIX);
        SVN_ERR(svn_stream_skip(content, skip));
        size -= skip;
    }

    hdr = apr_psprintf(pool, "blob %ld", size);
    ctx = svn_checksum_ctx_create(svn_checksum_sha1, pool);
    SVN_ERR(svn_checksum_update(ctx, hdr, strlen(hdr) + 1));

    SVN_ERR(svn_stream_printf(output, pool, "blob\n"));
    SVN_ERR(svn_stream_printf(output, pool, "data %ld\n", size));

    output = checksum_stream_create(output, NULL, ctx, pool);

    SVN_ERR(svn_stream_copy3(content, output, NULL, NULL, pool));

    SVN_ERR(svn_checksum_final(&git_checksum, ctx, pool));

    checksum_cache_set(cache, svn_checksum, git_checksum);
    *checksum = git_checksum;

    return SVN_NO_ERROR;
}
