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

#ifndef GIT_SVN_FAST_IMPORT_AUTHOR_H_
#define GIT_SVN_FAST_IMPORT_AUTHOR_H_

#include <svn_io.h>

// Abstract type for author_t.
typedef struct author_t author_t;

// Returns C string author representation.
// Use pool for memory allocation.
const char *
author_to_cstring(const author_t *a, apr_pool_t *pool);

typedef struct author_storage_t author_storage_t;

// Creates new author storage.
author_storage_t *
author_storage_create(apr_pool_t *pool);

// Lookups author by SVN committer name.
const author_t *
author_storage_lookup(const author_storage_t *as, const char *name);

// Returns default author.
const author_t *
author_storage_default_author(const author_storage_t *as);

// Loads author storage from stream.
// Uses pool for temporary allocations.
svn_error_t *
author_storage_load(const author_storage_t *as,
                    svn_stream_t *src,
                    apr_pool_t *pool);

#endif // GIT_SVN_FAST_IMPORT_AUTHOR_H_
