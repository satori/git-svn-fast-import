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

#include "blob.h"

// blob_t implementation
struct blob_t
{
    mark_t mark;
    size_t size;
    const svn_checksum_t *checksum;
};

mark_t
blob_mark_get(const blob_t *b)
{
    return b->mark;
}

void
blob_mark_set(blob_t *b, mark_t mark)
{
    b->mark = mark;
}

size_t
blob_size_get(const blob_t *b)
{
    return b->size;
}

void
blob_size_set(blob_t *b, size_t size)
{
    b->size = size;
}

const svn_checksum_t *
blob_checksum_get(const blob_t *b)
{
    return b->checksum;
}

// blob_storage_t implementation
struct blob_storage_t
{
    apr_pool_t *pool;
    apr_hash_t *blobs;
};

blob_storage_t *
blob_storage_create(apr_pool_t *pool)
{
    blob_storage_t *bs = apr_pcalloc(pool, sizeof(blob_storage_t));
    bs->pool = pool;
    bs->blobs = apr_hash_make(pool);

    return bs;
}

blob_t *
blob_storage_get(blob_storage_t *bs, const svn_checksum_t *checksum)
{
    blob_t *b = apr_hash_get(bs->blobs, checksum->digest, svn_checksum_size(checksum));
    if (b == NULL) {
        b = apr_pcalloc(bs->pool, sizeof(blob_t));
        b->checksum = svn_checksum_dup(checksum, bs->pool);

        apr_hash_set(bs->blobs, b->checksum->digest, svn_checksum_size(b->checksum), b);
    }

    return b;
}
