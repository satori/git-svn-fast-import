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
#include <svn_dirent_uri.h>
#include <svn_hash.h>
#include <svn_string.h>

svn_boolean_t
branch_path_is_root(const branch_t *b, const char *path)
{
    return strcmp(b->path, path) == 0;
}

const char *
branch_refname_from_path(const char *path, apr_pool_t *pool)
{
    apr_array_header_t *strings = svn_cstring_split(path, "/ ", FALSE, pool);
    svn_stringbuf_t *buf = svn_stringbuf_create_empty(pool);

    for (int i = 0; i < strings->nelts; i++) {
        const char *str = APR_ARRAY_IDX(strings, i, const char *);
        if (buf->len > 0) {
            svn_stringbuf_appendcstr(buf, "--");
        }
        svn_stringbuf_appendcstr(buf, str);
    }

    return apr_psprintf(pool, "refs/heads/%s", buf->data);
}

branch_storage_t *
branch_storage_create(apr_pool_t *pool)
{
    branch_storage_t *bs = apr_pcalloc(pool, sizeof(branch_storage_t));

    bs->pool = pool;
    bs->tree = tree_create(pool);
    bs->pfx = tree_create(pool);
    bs->refnames = apr_hash_make(pool);

    return bs;
}

void
branch_storage_add_prefix(branch_storage_t *bs,
                          const char *pfx,
                          svn_boolean_t is_tag,
                          apr_pool_t *pool)
{
    tree_insert(bs->pfx, pfx, pfx, pool);
}

branch_t *
branch_storage_add_branch(branch_storage_t *bs,
                          const char *refname,
                          const char *path,
                          apr_pool_t *pool)
{
    branch_t *b = apr_pcalloc(bs->pool, sizeof(branch_t));
    b->refname = apr_pstrdup(bs->pool, refname);
    b->path = apr_pstrdup(bs->pool, path);
    b->dirty = TRUE;

    tree_insert(bs->tree, b->path, b, pool);
    svn_hash_sets(bs->refnames, b->refname, b);

    return b;
}

branch_t *
branch_storage_lookup_refname(branch_storage_t *bs, const char *refname)
{
    return svn_hash_gets(bs->refnames, refname);
}

branch_t *
branch_storage_lookup_path(branch_storage_t *bs, const char *path, apr_pool_t *pool)
{
    branch_t *branch;
    const char *branch_path, *prefix, *refname, *root, *subpath;

    branch = (branch_t *) tree_match(bs->tree, path, pool);
    if (branch != NULL) {
        return branch;
    }

    prefix = tree_match(bs->pfx, path, pool);
    if (prefix == NULL) {
        return NULL;
    }

    subpath = svn_dirent_skip_ancestor(prefix, path);
    if (subpath == NULL) {
        return NULL;
    }

    if (*subpath == '\0') {
        return NULL;
    }

    root = strchr(subpath, '/');
    if (root == NULL) {
        branch_path = path;
    } else {
        branch_path = apr_pstrndup(pool, path, root - path);
    }

    refname = branch_refname_from_path(branch_path, pool);

    return branch_storage_add_branch(bs, refname, branch_path, pool);
}

svn_error_t *
branch_storage_dump(branch_storage_t *bs, svn_stream_t *dst, apr_pool_t *pool)
{
    apr_array_header_t *values;
    values = tree_values(bs->tree, "", pool, pool);

    for (int i = 0; i < values->nelts; i++) {
        const branch_t *branch = APR_ARRAY_IDX(values, i, branch_t *);
        SVN_ERR(svn_stream_printf(dst, pool, "%s \"%s\" %d\n",
                                  branch->refname, branch->path, branch->dirty));
    }

    return SVN_NO_ERROR;
}

svn_error_t *
branch_storage_dump_path(branch_storage_t *bs, const char *path, apr_pool_t *pool)
{
    apr_file_t *fd;
    svn_stream_t *dst;

    SVN_ERR(svn_io_file_open(&fd, path,
                             APR_CREATE | APR_TRUNCATE | APR_BUFFERED | APR_WRITE,
                             APR_OS_DEFAULT, pool));
    dst = svn_stream_from_aprfile2(fd, FALSE, pool);
    SVN_ERR(branch_storage_dump(bs, dst, pool));
    SVN_ERR(svn_stream_close(dst));

    return SVN_NO_ERROR;
}

static svn_error_t *
svn_malformed_file_error(int lineno, const char *line)
{
    return svn_error_createf(SVN_ERR_MALFORMED_FILE, NULL,
                             "line %d: %s", lineno, line);
}

svn_error_t *
branch_storage_load(branch_storage_t *bs, svn_stream_t *src, apr_pool_t *pool)
{
    svn_boolean_t eof;
    int lineno = 0;

    while (TRUE) {
        const char *prev, *next;
        const char *refname, *path;
        branch_t *branch;
        svn_boolean_t dirty = TRUE;
        svn_stringbuf_t *buf;

        SVN_ERR(svn_stream_readline(src, &buf, "\n", &eof, pool));
        if (eof) {
            break;
        }

        lineno++;

        prev = buf->data;
        next = strchr(buf->data, ' ');
        if (next == NULL) {
            return svn_malformed_file_error(lineno, buf->data);
        }
        refname = apr_pstrndup(pool, prev, next - prev);

        // Skip whitespace.
        next++;

        next = strchr(next, '"');
        if (next == NULL) {
            return svn_malformed_file_error(lineno, buf->data);
        }
        next++;

        prev = next;
        next = strchr(next, '"');
        if (next == NULL) {
            return svn_malformed_file_error(lineno, buf->data);
        }

        path = apr_pstrndup(pool, prev, next - prev);

        next++;

        next = strchr(next, ' ');
        if (next == NULL) {
            return svn_malformed_file_error(lineno, buf->data);
        }
        next++;

        if (*next == '0') {
            dirty = FALSE;
        }

        branch = branch_storage_lookup_refname(bs, refname);
        if (branch == NULL) {
            branch = branch_storage_add_branch(bs, refname, path, pool);
        }
        branch->dirty = dirty;
    }

    return SVN_NO_ERROR;
}

svn_error_t *
branch_storage_load_path(branch_storage_t *bs, const char *path, apr_pool_t *pool)
{
    svn_error_t *err;
    svn_stream_t *src;
    svn_node_kind_t kind;

    SVN_ERR(svn_io_check_path(path, &kind, pool));
    if (kind == svn_node_none) {
        return SVN_NO_ERROR;
    }

    SVN_ERR(svn_stream_open_readonly(&src, path, pool, pool));
    err = branch_storage_load(bs, src, pool);
    if (err) {
        return svn_error_quick_wrap(err, "Malformed branches file");
    }
    SVN_ERR(svn_stream_close(src));

    return SVN_NO_ERROR;
}
