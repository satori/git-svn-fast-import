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

#ifndef GIT_SVN_FAST_IMPORT_BLOB_H_
#define GIT_SVN_FAST_IMPORT_BLOB_H_

#include "compat.h"
#include "mark.h"
#include <svn_checksum.h>

// Abstract type for blob.
typedef struct blob_t blob_t;

// Returns blob mark.
mark_t
blob_mark_get(const blob_t *b);

// Set blob mark.
void
blob_mark_set(blob_t *b, mark_t mark);

// Returns blob size.
size_t
blob_size_get(const blob_t *b);

// Set blob size.
void
blob_size_set(blob_t *b, size_t size);

// Returns blob checksum.
const svn_checksum_t *
blob_checksum_get(const blob_t *b);

// Abstract type for blob storage.
typedef struct blob_storage_t blob_storage_t;

// Create new blob storage.
blob_storage_t *
blob_storage_create(apr_pool_t *pool);

// Returns a blob for checksum.
blob_t *
blob_storage_get(blob_storage_t *bs, const svn_checksum_t *checksum);

#endif // GIT_SVN_FAST_IMPORT_BLOB_H_
