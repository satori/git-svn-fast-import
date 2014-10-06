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

#include "backend.h"

#include "io.h"
#include "utils.h"

static git_svn_status_t
revision_noop(svn_stream_t *out, revision_t *rev, apr_pool_t *pool)
{
    return io_printf(out, pool, "progress Skipped revision %d\n", rev->revnum);
}

static git_svn_status_t
revision_begin(svn_stream_t *out, revision_t *rev, apr_pool_t *pool)
{
    git_svn_status_t err;

    err = io_printf(out, pool, "commit refs/heads/%s\n", rev->branch->name);
    if (err) {
        return err;
    }

    err = io_printf(out, pool, "mark :%d\n", rev->mark);
    if (err) {
        return err;
    }

    err = io_printf(out, pool, "committer %s <%s@local> %"PRId64" +0000\n",
                    rev->author, rev->author, rev->timestamp);
    if (err) {
        return err;
    }

    err = io_printf(out, pool, "data %ld\n", strlen(rev->message));
    if (err) {
        return err;
    }

    err = io_printf(out, pool, "%s\n", rev->message);
    if (err) {
        return err;
    }

    if (rev->copyfrom != NULL) {
        err = io_printf(out, pool, "from :%d\n", rev->copyfrom->mark);
        if (err) {
            return err;
        }
    }

    return GIT_SVN_SUCCESS;
}

static git_svn_status_t
revision_end(svn_stream_t *out, revision_t *rev, apr_pool_t *pool)
{
    return io_printf(out, pool, "progress Imported revision %d\n", rev->revnum);
}

static git_svn_status_t
node_modify_blob(svn_stream_t *out, node_t *node, apr_pool_t *pool)
{
    blob_t *blob = node->content.data.blob;

    return io_printf(out, pool, "M %o :%d \"%s\"\n", node->mode, blob->mark, node->path);
}

static git_svn_status_t
node_modify_checksum(svn_stream_t *out, node_t *node, apr_pool_t *pool)
{
    git_svn_status_t err;
    char checksum[CHECKSUM_CHARS_LENGTH + 1];
    checksum[CHECKSUM_CHARS_LENGTH] = '\0';

    err = bytes_to_hex(checksum, node->content.data.checksum, CHECKSUM_BYTES_LENGTH);
    if (err) {
        return err;
    }

    return io_printf(out, pool, "M %o %s \"%s\"\n", node->mode, checksum, node->path);
}

static git_svn_status_t
node_modify(svn_stream_t *out, node_t *node, apr_pool_t *pool)
{
    switch (node->content.kind) {
    case CONTENT_CHECKSUM:
        return node_modify_checksum(out, node, pool);
    case CONTENT_BLOB:
        return node_modify_blob(out, node, pool);
    default:
        return GIT_SVN_SUCCESS;
    }
}

static git_svn_status_t
node_delete(svn_stream_t *out, node_t *node, apr_pool_t *pool)
{
    return io_printf(out, pool, "D \"%s\"\n", node->path);
}

static git_svn_status_t
node_replace(svn_stream_t *out, node_t *node, apr_pool_t *pool)
{
    git_svn_status_t err;
    err = node_delete(out, node, pool);
    if (err) {
        return err;
    }
    return node_modify(out, node, pool);
}

static git_svn_status_t
handle_node(svn_stream_t *out, node_t *node, apr_pool_t *pool)
{
    switch (node->action) {
    case ACTION_ADD:
    case ACTION_CHANGE:
        return node_modify(out, node, pool);
    case ACTION_DELETE:
        return node_delete(out, node, pool);
    case ACTION_REPLACE:
        return node_replace(out, node, pool);
    default:
        return GIT_SVN_SUCCESS;
    }
}

git_svn_status_t
backend_write_revision(backend_t *be, revision_t *rev, apr_array_header_t *nodes, apr_pool_t *pool)
{
    git_svn_status_t err;

    if (rev->revnum == 0 || rev->branch == NULL) {
        return revision_noop(be->out, rev, pool);
    }

    err = revision_begin(be->out, rev, pool);
    if (err) {
        return err;
    }

    for (int i = 0; i < nodes->nelts; i++) {
        node_t *node = &APR_ARRAY_IDX(nodes, i, node_t);
        err = handle_node(be->out, node, pool);
        if (err) {
            return err;
        }
    }

    err = revision_end(be->out, rev, pool);
    if (err) {
        return err;
    }

    return GIT_SVN_SUCCESS;
}

git_svn_status_t
backend_write_blob_header(backend_t *be, blob_t *blob, apr_pool_t *pool)
{
    git_svn_status_t err;

    err = io_printf(be->out, pool, "blob\n");
    if (err) {
        return err;
    }

    err = io_printf(be->out, pool, "mark :%d\n", blob->mark);
    if (err) {
        return err;
    }

    err = io_printf(be->out, pool, "data %ld\n", blob->length);
    if (err) {
        return err;
    }

    return GIT_SVN_SUCCESS;
}

git_svn_status_t
backend_notify_branch_found(backend_t *be, branch_t *branch, apr_pool_t *pool)
{
    return io_printf(be->out, pool, "progress Found branch at %s\n", branch->path);
}

git_svn_status_t
backend_get_checksum(backend_t *be, uint8_t *sha1, revision_t *rev, const char *path, apr_pool_t *pool)
{
    const char *next, *prev;
    git_svn_status_t err;

    err = io_printf(be->out, pool, "ls :%d \"%s\"\n", rev->mark, path);
    if (err) {
        return err;
    }

    err = io_readline(be->back, &next, pool);
    if (err) {
        return err;
    }

    if (*next == 'm') { // missing path
        fprintf(stderr, "%s\n", next);
        return GIT_SVN_FAILURE;
    }

    for (int i = 0; i < 3; i++) {
        prev = next;
        next = strchr(prev, ' ');
        next++;
    }

    return hex_to_bytes(sha1, prev, CHECKSUM_BYTES_LENGTH);
}
