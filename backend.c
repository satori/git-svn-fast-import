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

#include "backend.h"

#define NULL_SHA1 "0000000000000000000000000000000000000000"

static svn_error_t *
node_modify(svn_stream_t *out, const node_t *node, apr_pool_t *pool)
{
    const char *checksum;
    checksum = svn_checksum_to_cstring_display(node->checksum, pool);

    SVN_ERR(svn_stream_printf(out, pool, "M %o %s \"%s\"\n",
                              node->mode, checksum, node->path));

    return SVN_NO_ERROR;
}

static svn_error_t *
node_delete(svn_stream_t *out, const node_t *node, apr_pool_t *pool)
{
    SVN_ERR(svn_stream_printf(out, pool, "D \"%s\"\n", node->path));

    return SVN_NO_ERROR;
}

svn_error_t *
backend_write_commit(svn_stream_t *out,
                     const branch_t *branch,
                     const commit_t *commit,
                     apr_array_header_t *nodes,
                     const author_t *author,
                     const char *message,
                     int64_t timestamp,
                     apr_pool_t *pool)
{
    const commit_t *copyfrom = commit_copyfrom_get(commit);

    SVN_ERR(svn_stream_printf(out, pool, "commit %s\n", branch->refname));
    SVN_ERR(svn_stream_printf(out, pool, "mark :%d\n",
                              commit_mark_get(commit)));
    SVN_ERR(svn_stream_printf(out, pool, "committer %s %"PRId64" +0000\n",
                              author_to_cstring(author, pool), timestamp));
    SVN_ERR(svn_stream_printf(out, pool, "data %ld\n", strlen(message)));
    SVN_ERR(svn_stream_printf(out, pool, "%s\n", message));

    if (copyfrom != NULL) {
        SVN_ERR(svn_stream_printf(out, pool, "from :%d\n",
                                  commit_mark_get(copyfrom)));
    }

    for (int i = 0; i < nodes->nelts; i++) {
        const node_t *node = &APR_ARRAY_IDX(nodes, i, node_t);
        switch (node->action) {
        case svn_fs_path_change_add:
        case svn_fs_path_change_modify:
            SVN_ERR(node_modify(out, node, pool));
            break;
        case svn_fs_path_change_delete:
            SVN_ERR(node_delete(out, node, pool));
            break;
        case svn_fs_path_change_replace:
            SVN_ERR(node_delete(out, node, pool));
            SVN_ERR(node_modify(out, node, pool));
            break;
        default:
            break;
        }
    }

    return SVN_NO_ERROR;
}
