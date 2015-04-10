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

#ifndef GIT_SVN_FAST_IMPORT_TYPES_H_
#define GIT_SVN_FAST_IMPORT_TYPES_H_

typedef int32_t revnum_t;

#include "compat.h"
#include <svn_checksum.h>

typedef struct
{
    mark_t mark;
    size_t length;
    svn_checksum_t *checksum;
} blob_t;

typedef enum
{
    ACTION_NOOP,
    ACTION_ADD,
    ACTION_CHANGE,
    ACTION_DELETE,
    ACTION_REPLACE
} node_action_t;

typedef enum
{
    KIND_UNKNOWN,
    KIND_FILE,
    KIND_DIR
} node_kind_t;

typedef enum
{
    CONTENT_UNKNOWN,
    CONTENT_CHECKSUM,
    CONTENT_BLOB
} content_kind_t;

typedef struct
{
    node_action_t action;
    node_kind_t kind;
    uint32_t mode;
    const char *path;
    struct {
        content_kind_t kind;
        union {
            svn_checksum_t *checksum;
            blob_t *blob;
        } data;
    } content;
} node_t;

#endif // GIT_SVN_FAST_IMPORT_TYPES_H_
