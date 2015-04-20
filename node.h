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

#ifndef GIT_SVN_FAST_IMPORT_NODE_H_
#define GIT_SVN_FAST_IMPORT_NODE_H_

#include "blob.h"
#include "branch.h"

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
    MODE_NORMAL     = 0100644,
    MODE_EXECUTABLE = 0100755,
    MODE_SYMLINK    = 0120000,
    MODE_DIR        = 0040000
} node_mode_t;

typedef enum
{
    CONTENT_UNKNOWN,
    CONTENT_CHECKSUM,
    CONTENT_BLOB
} node_content_kind_t;

// Abstract type for node.
typedef struct node_t node_t;

typedef svn_error_t * (*node_handler_t)(void *ctx, node_t *node, apr_pool_t *pool);

// Returns node action.
node_action_t
node_action_get(const node_t *n);

// Sets node action.
void
node_action_set(node_t *n, node_action_t action);

// Returns node mode.
node_mode_t
node_mode_get(const node_t *n);

// Sets node mode.
void
node_mode_set(node_t *n, node_mode_t mode);

// Returns node kind.
node_kind_t
node_kind_get(const node_t *n);

// Sets node kind.
void
node_kind_set(node_t *n, node_kind_t kind);

// Returns node path.
const char *
node_path_get(const node_t *n);

// Sets node path.
void
node_path_set(node_t *n, const char *path);

// Returns content kind.
node_content_kind_t
node_content_kind_get(const node_t *n);

// Returns content checksum.
const svn_checksum_t *
node_content_checksum_get(const node_t *n);

// Sets content checksum.
void
node_content_checksum_set(node_t *n, const svn_checksum_t *checksum);

// Returns node content blob.
blob_t *
node_content_blob_get(const node_t *n);

void
node_content_blob_set(node_t *n, blob_t *blob);

typedef struct node_list_t node_list_t;

size_t
node_list_count(const node_list_t *nl);

typedef struct node_iter_t node_iter_t;

node_iter_t *
node_iter_first(const node_list_t *lst, apr_pool_t *pool);

node_iter_t *
node_iter_next(node_iter_t *it);

const node_t *
node_iter_get(const node_iter_t *it);

// Abstract type for node storage.
typedef struct node_storage_t node_storage_t;

// Returns new node storage.
node_storage_t *
node_storage_create(apr_pool_t *pool);

// Adds new node for branch and returns it.
node_t *
node_storage_add(node_storage_t *ns,
                 const branch_t *branch);

// Applies function for each node entry.
svn_error_t *
node_storage_apply(const node_storage_t *ns,
                   node_handler_t apply,
                   void *ctx,
                   apr_pool_t *pool);

// Returns nodes list. 
const node_list_t *
node_storage_list(const node_storage_t *ns,
                  const branch_t *branch);

#endif // GIT_SVN_FAST_IMPORT_NODE_H_
