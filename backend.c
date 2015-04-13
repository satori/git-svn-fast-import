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
#include "error.h"

#define NULL_SHA1 "0000000000000000000000000000000000000000"

static svn_error_t *
node_modify_blob(svn_stream_t *out, const node_t *node, apr_pool_t *pool)
{
    blob_t *blob = node->content.data.blob;

    SVN_ERR(svn_stream_printf(out, pool, "M %o :%d \"%s\"\n", node->mode, blob->mark, node->path));

    return SVN_NO_ERROR;
}

static svn_error_t *
node_modify_checksum(svn_stream_t *out, const node_t *node, apr_pool_t *pool)
{
    const char *checksum;
    checksum = svn_checksum_to_cstring_display(node->content.data.checksum, pool);

    SVN_ERR(svn_stream_printf(out, pool, "M %o %s \"%s\"\n", node->mode, checksum, node->path));

    return SVN_NO_ERROR;
}

static svn_error_t *
node_modify(svn_stream_t *out, const node_t *node, apr_pool_t *pool)
{
    switch (node->content.kind) {
    case CONTENT_CHECKSUM:
        return node_modify_checksum(out, node, pool);
    case CONTENT_BLOB:
        return node_modify_blob(out, node, pool);
    default:
        return SVN_NO_ERROR;
    }
}

static svn_error_t *
node_delete(svn_stream_t *out, const node_t *node, apr_pool_t *pool)
{
    SVN_ERR(svn_stream_printf(out, pool, "D \"%s\"\n", node->path));

    return SVN_NO_ERROR;
}

svn_error_t *
backend_write_commit(const backend_t *be,
                     const branch_t *branch,
                     const commit_t *commit,
                     const apr_array_header_t *nodes,
                     const author_t *author,
                     const char *message,
                     int64_t timestamp,
                     apr_pool_t *pool)
{
    const commit_t *copyfrom = commit_copyfrom_get(commit);
    svn_stream_t *out = be->out;

    SVN_ERR(svn_stream_printf(out, pool, "commit %s\n",
                              branch_refname_get(branch)));
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
        case ACTION_ADD:
        case ACTION_CHANGE:
            SVN_ERR(node_modify(out, node, pool));
            break;
        case ACTION_DELETE:
            SVN_ERR(node_delete(out, node, pool));
            break;
        case ACTION_REPLACE:
            SVN_ERR(node_delete(out, node, pool));
            SVN_ERR(node_modify(out, node, pool));
            break;
        default:
            break;
        }
    }

    return SVN_NO_ERROR;
}

svn_error_t *
backend_reset_branch(const backend_t *be,
                     const branch_t *branch,
                     const commit_t *commit,
                     apr_pool_t *pool)
{
    svn_stream_t *out = be->out;

    SVN_ERR(svn_stream_printf(out, pool, "reset %s\n",
                              branch_refname_get(branch)));
    SVN_ERR(svn_stream_printf(out, pool, "from :%d\n",
                              commit_mark_get(commit)));

    return SVN_NO_ERROR;
}

svn_error_t *
backend_write_blob_header(const backend_t *be,
                          const blob_t *blob,
                          apr_pool_t *pool)
{
    svn_stream_t *out = be->out;

    SVN_ERR(svn_stream_printf(out, pool, "blob\n"));
    SVN_ERR(svn_stream_printf(out, pool, "mark :%d\n", blob->mark));
    SVN_ERR(svn_stream_printf(out, pool, "data %ld\n", blob->length));

    return SVN_NO_ERROR;
}

svn_error_t *
backend_remove_branch(const backend_t *be,
                      const branch_t *branch,
                      apr_pool_t *pool)
{
    svn_stream_t *out = be->out;

    SVN_ERR(svn_stream_printf(out, pool, "reset %s\n",
                              branch_refname_get(branch)));
    SVN_ERR(svn_stream_printf(out, pool, "from %s\n", NULL_SHA1));

    return SVN_NO_ERROR;
}

svn_error_t *
backend_notify_revision_skipped(const backend_t *be,
                                svn_revnum_t revnum,
                                apr_pool_t *pool)
{
    SVN_ERR(svn_stream_printf(be->out, pool, "progress Skipped revision %ld\n", revnum));

    return SVN_NO_ERROR;
}

svn_error_t *
backend_notify_revision_imported(const backend_t *be,
                                 svn_revnum_t revnum,
                                 apr_pool_t *pool)
{
    SVN_ERR(svn_stream_printf(be->out, pool, "progress Imported revision %ld\n", revnum));

    return SVN_NO_ERROR;
}

svn_error_t *
backend_notify_branch_updated(const backend_t *be,
                              const branch_t *branch,
                              apr_pool_t *pool)
{
    if (be->verbose) {
        SVN_ERR(svn_stream_printf(be->out, pool, "progress Updated %s\n",
                                  branch_refname_get(branch)));
    }

    return SVN_NO_ERROR;
}

svn_error_t *
backend_notify_branch_removed(const backend_t *be,
                              const branch_t *branch,
                              apr_pool_t *pool)
{
    if (be->verbose) {
        SVN_ERR(svn_stream_printf(be->out, pool, "progress Removed %s\n",
                                  branch_refname_get(branch)));
    }

    return SVN_NO_ERROR;
}

static svn_error_t *
parse_checksum(svn_checksum_t **dst,
               const svn_stringbuf_t *src,
               apr_pool_t *pool)
{
    const char *next, *prev;

    // Check if path is missing
    if (*src->data == 'm') {
        handle_warning(src->data);
        return SVN_NO_ERROR;
    }

    next = src->data;

    for (int i = 0; i < 3; i++) {
        prev = next;
        next = strchr(prev, ' ');
        next++;
    }

    SVN_ERR(svn_checksum_parse_hex(dst, svn_checksum_sha1, prev, pool));

    return SVN_NO_ERROR;
}

svn_error_t *
backend_get_checksum(svn_checksum_t **dst,
                     const backend_t *be,
                     const commit_t *commit,
                     const char *path,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
    svn_boolean_t eof;
    svn_stringbuf_t *buf;

    SVN_ERR(svn_stream_printf(be->out, scratch_pool, "ls :%d \"%s\"\n",
                              commit_mark_get(commit),
                              path));
    SVN_ERR(svn_stream_readline(be->back, &buf, "\n", &eof, scratch_pool));
    SVN_ERR(parse_checksum(dst, buf, result_pool));

    return SVN_NO_ERROR;
}

svn_error_t *
backend_finished(const backend_t *be, apr_pool_t *pool)
{
    SVN_ERR(svn_stream_printf(be->out, pool, "done\n"));

    return SVN_NO_ERROR;
}
