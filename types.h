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

#ifndef GIT_SVN_FAST_IMPORT_TYPES_H_
#define GIT_SVN_FAST_IMPORT_TYPES_H_

#include "compat.h"


typedef struct
{
    int32_t revnum;
    int64_t timestamp;
    const char *branch;
    const char *author;
    const char *message;
} git_svn_revision_t;


typedef struct
{
    uint32_t mark;
    size_t length;
    const char *checksum;
} git_svn_blob_t;


typedef enum
{
    GIT_SVN_NODE_ADD,
    GIT_SVN_NODE_CHANGE,
    GIT_SVN_NODE_DELETE,
    GIT_SVN_NODE_REPLACE
} git_svn_node_action_t;


typedef enum
{
    GIT_SVN_NODE_UNKNOWN,
    GIT_SVN_NODE_FILE,
    GIT_SVN_NODE_DIR
} git_svn_node_kind_t;


typedef struct
{
    git_svn_node_action_t action;
    git_svn_node_kind_t kind;
    uint32_t mode;
    const char *path;
    git_svn_blob_t *blob;
} git_svn_node_t;


#endif // GIT_SVN_FAST_IMPORT_TYPES_H_