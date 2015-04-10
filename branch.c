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

#include "branch.h"
#include "utils.h"
#include <apr_strings.h>

// branch_t implementation.
struct branch_t
{
    const char *refname;
    const char *path;
    const commit_t *head;
};

const char *
branch_refname_get(const branch_t *b)
{
    return b->refname;
}

const char *
branch_skip_prefix(const branch_t *b, const char *path)
{
    return cstring_skip_prefix(path, b->path);
}

const commit_t *
branch_head_get(const branch_t *b)
{
    return b->head;
}

void
branch_head_set(branch_t *b, const commit_t *c)
{
    b->head = c;
}

bool
branch_path_is_root(const branch_t *b, const char *path)
{
    return strcmp(b->path, path) == 0;
}

// branch_storage_t implementation.
struct branch_storage_t
{
    apr_pool_t *pool;
    tree_t *branches;
    // Branch path prefixes.
    tree_t *bpfx;
    // Tag path prefixes.
    tree_t *tpfx;
};

branch_storage_t *
branch_storage_create(apr_pool_t *pool, tree_t *bpfx, tree_t *tpfx)
{
    branch_storage_t *bs = apr_pcalloc(pool, sizeof(branch_storage_t));

    bs->pool = pool;
    bs->branches = tree_create(pool);
    bs->bpfx = bpfx;
    bs->tpfx = tpfx;

    return bs;
}

branch_t *
branch_storage_add_branch(branch_storage_t *bs, const char *refname, const char *path)
{
    branch_t *b = apr_pcalloc(bs->pool, sizeof(branch_t));
    b->refname = refname;
    b->path = path;

    tree_insert(bs->branches, b->path, b);

    return b;
}

branch_t *
branch_storage_lookup_path(branch_storage_t *bs, const char *path)
{
    branch_t *branch;
    const char *prefix, *root, *subpath;

    branch = (branch_t *) tree_find_longest_prefix(bs->branches, path);
    if (branch != NULL) {
        return branch;
    }

    prefix = tree_find_longest_prefix(bs->bpfx, path);
    if (prefix == NULL) {
        prefix = tree_find_longest_prefix(bs->tpfx, path);
    }

    if (prefix == NULL) {
        return NULL;
    }

    subpath = cstring_skip_prefix(path, prefix);
    if (subpath == NULL) {
        return NULL;
    }

    // Skip starting directory separator
    if (*subpath == '/') {
        subpath++;
    }

    if (*subpath == '\0') {
        return NULL;
    }

    branch = apr_pcalloc(bs->pool, sizeof(branch_t));

    root = strchr(subpath, '/');
    if (root == NULL) {
        branch->path = apr_pstrdup(bs->pool, path);
    } else {
        branch->path = apr_pstrndup(bs->pool, path, root - path);
    }

    branch->refname = apr_psprintf(bs->pool, "refs/heads/%s", branch->path);

    tree_insert(bs->branches, branch->path, branch);

    return branch;
}
