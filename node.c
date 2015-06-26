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

#include "node.h"
#include <svn_hash.h>
#include <svn_props.h>

node_mode_t
node_mode_parse(const char *src, size_t len)
{
    if (strncmp(src, "100644", len) == 0) {
        return MODE_NORMAL;
    } else if (strncmp(src, "100755", len) == 0) {
        return MODE_EXECUTABLE;
    } else if (strncmp(src, "120000", len) == 0) {
        return MODE_SYMLINK;
    } else if (strncmp(src, "040000", len) == 0) {
        return MODE_DIR;
    }

    return MODE_NORMAL;
}

svn_error_t *
set_node_mode(node_mode_t *mode,
              svn_fs_root_t *root,
              const char *path,
              apr_pool_t *pool)
{
    apr_hash_t *props;
    svn_node_kind_t kind;

    SVN_ERR(svn_fs_check_path(&kind, root, path, pool));

    if (kind == svn_node_dir) {
        *mode = MODE_DIR;
        return SVN_NO_ERROR;
    }

    SVN_ERR(svn_fs_node_proplist(&props, root, path, pool));

    if (svn_hash_gets(props, SVN_PROP_EXECUTABLE)) {
        *mode = MODE_EXECUTABLE;
    } else if (svn_hash_gets(props, SVN_PROP_SPECIAL)) {
        *mode = MODE_SYMLINK;
    } else {
        *mode = MODE_NORMAL;
    }

    return SVN_NO_ERROR;
}

struct node_storage_t
{
    apr_pool_t *pool;
    apr_hash_t *nodes;
};

node_storage_t *
node_storage_create(apr_pool_t *pool)
{
    node_storage_t *ns = apr_pcalloc(pool, sizeof(node_storage_t));
    ns->pool = pool;
    ns->nodes = apr_hash_make(pool);

    return ns;
}

node_t *
node_storage_add(node_storage_t *ns,
                 const branch_t *branch)
{
    apr_array_header_t *nodes;
    node_t *node;

    nodes = apr_hash_get(ns->nodes, branch, sizeof(branch_t *));
    if (nodes == NULL) {
        nodes = apr_array_make(ns->pool, 0, sizeof(node_t));
        apr_hash_set(ns->nodes, branch, sizeof(branch_t *), nodes);
    }

    node = apr_array_push(nodes);

    return node;
}

apr_array_header_t *
node_storage_list(const node_storage_t *ns,
                  const branch_t *branch)
{
    return apr_hash_get(ns->nodes, branch, sizeof(branch_t *));
}
