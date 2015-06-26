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

#ifndef SVN_FAST_EXPORT_NODE_H_
#define SVN_FAST_EXPORT_NODE_H_

#include "branch.h"
#include <svn_fs.h>

typedef enum
{
    MODE_NORMAL     = 0100644,
    MODE_EXECUTABLE = 0100755,
    MODE_SYMLINK    = 0120000,
    MODE_DIR        = 0040000
} node_mode_t;

svn_error_t *
set_node_mode(node_mode_t *mode,
              svn_fs_root_t *root,
              const char *path,
              apr_pool_t *pool);

typedef struct
{
    svn_fs_path_change_kind_t action;
    svn_node_kind_t kind;
    node_mode_t mode;
    const char *path;
    svn_checksum_t *checksum;
    apr_array_header_t *entries;
} node_t;

// Abstract type for node storage.
typedef struct node_storage_t node_storage_t;

// Returns new node storage.
node_storage_t *
node_storage_create(apr_pool_t *pool);

// Adds new node for branch and returns it.
node_t *
node_storage_add(node_storage_t *ns,
                 const branch_t *branch);

// Returns nodes list. 
apr_array_header_t *
node_storage_list(const node_storage_t *ns,
                  const branch_t *branch);

#endif // SVN_FAST_EXPORT_NODE_H_
