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
#include <apr_ring.h>

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

// node_t implementation
struct node_t
{
    node_action_t action;
    node_kind_t kind;
    node_mode_t mode;
    const char *path;
    struct {
        node_content_kind_t kind;
        union {
            const svn_checksum_t *checksum;
            blob_t *blob;
        } data;
    } content;
    // Support for the ring container.
    APR_RING_ENTRY(node_t) link;
};

node_action_t
node_action_get(const node_t *n)
{
    return n->action;
}

void
node_action_set(node_t *n, node_action_t action)
{
    n->action = action;
}

node_mode_t
node_mode_get(const node_t *n)
{
    return n->mode;
}

void
node_mode_set(node_t *n, node_mode_t mode)
{
    n->mode = mode;
}

node_kind_t
node_kind_get(const node_t *n)
{
    return n->kind;
}

void
node_kind_set(node_t *n, node_kind_t kind)
{
    n->kind = kind;
}

const char *
node_path_get(const node_t *n)
{
    return n->path;
}

void
node_path_set(node_t *n, const char *path)
{
    n->path = path;
}

node_content_kind_t
node_content_kind_get(const node_t *n)
{
    return n->content.kind;
}

const svn_checksum_t *
node_content_checksum_get(const node_t *n)
{
    return n->content.data.checksum;
}

void
node_content_checksum_set(node_t *n, const svn_checksum_t *checksum)
{
    n->content.data.checksum = checksum;
    n->content.kind = CONTENT_CHECKSUM;
}

blob_t *
node_content_blob_get(const node_t *n)
{
    return n->content.data.blob;
}

void
node_content_blob_set(node_t *n, blob_t *blob)
{
    n->content.data.blob = blob;
    n->content.kind = CONTENT_BLOB;
}

struct node_list_t {
    size_t size;
    node_t *next;
    node_t *prev;
};

size_t
node_list_count(const node_list_t *nl)
{
    return nl->size;
}

struct node_iter_t
{
    const node_list_t *lst;
    const node_t *node;
};

node_iter_t *
node_iter_first(const node_list_t *lst, apr_pool_t *pool)
{
    node_iter_t *it;
    const node_t *node = APR_RING_FIRST(lst);
    if (node == APR_RING_SENTINEL(lst, node_t, link)) {
        return NULL;
    }

    it = apr_pcalloc(pool, sizeof(node_iter_t));
    it->lst = lst;
    it->node = node;

    return it;
}

node_iter_t *
node_iter_next(node_iter_t *it)
{
    const node_t *node = APR_RING_NEXT(it->node, link);
    if (node == APR_RING_SENTINEL(it->lst, node_t, link)) {
        return NULL;
    }

    it->node = node;

    return it;
}

const node_t *
node_iter_get(const node_iter_t *it)
{
    return it->node;
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
    node_t *node;

    node_list_t *nodes = apr_hash_get(ns->nodes, branch, sizeof(branch_t *));
    if (nodes == NULL) {
        nodes = apr_pcalloc(ns->pool, sizeof(node_list_t));
        APR_RING_INIT(nodes, node_t, link);
        apr_hash_set(ns->nodes, branch, sizeof(branch_t *), nodes);
    }

    nodes->size++;

    node = apr_pcalloc(ns->pool, sizeof(node_t));
    APR_RING_INSERT_TAIL(nodes, node, node_t, link);

    return node;
}

const node_list_t *
node_storage_list(const node_storage_t *ns,
                  const branch_t *branch)
{
    return apr_hash_get(ns->nodes, branch, sizeof(branch_t *));
}
