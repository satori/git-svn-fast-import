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

#include "io.h"
#include "utils.h"

#define NULL_SHA1 "0000000000000000000000000000000000000000"

static git_svn_status_t
write_commit_header(int fd,
                    const branch_t *branch,
                    const commit_t *commit,
                    const char *author,
                    const char *message,
                    int64_t timestamp)
{
    git_svn_status_t err;

    err = io_printf(fd, "commit %s\n", branch->refname);
    if (err) {
        return err;
    }

    err = io_printf(fd, "mark :%d\n", commit->mark);
    if (err) {
        return err;
    }

    err = io_printf(fd, "committer %s <%s@local> %"PRId64" +0000\n",
                    author, author, timestamp);
    if (err) {
        return err;
    }

    err = io_printf(fd, "data %ld\n", strlen(message));
    if (err) {
        return err;
    }

    err = io_printf(fd, "%s\n", message);
    if (err) {
        return err;
    }

    if (commit->copyfrom != NULL) {
        const commit_t *copyfrom = commit_get_effective_commit(commit->copyfrom);
        err = io_printf(fd, "from :%d\n", copyfrom->mark);
        if (err) {
            return err;
        }
    }

    return GIT_SVN_SUCCESS;
}

static git_svn_status_t
node_modify_blob(int fd, const node_t *node)
{
    blob_t *blob = node->content.data.blob;

    return io_printf(fd, "M %o :%d \"%s\"\n", node->mode, blob->mark, node->path);
}

static git_svn_status_t
node_modify_checksum(int fd, const node_t *node, apr_pool_t *pool)
{
    const char *checksum;
    checksum = svn_checksum_to_cstring_display(node->content.data.checksum, pool);

    return io_printf(fd, "M %o %s \"%s\"\n", node->mode, checksum, node->path);
}

static git_svn_status_t
node_modify(int fd, const node_t *node, apr_pool_t *pool)
{
    switch (node->content.kind) {
    case CONTENT_CHECKSUM:
        return node_modify_checksum(fd, node, pool);
    case CONTENT_BLOB:
        return node_modify_blob(fd, node);
    default:
        return GIT_SVN_SUCCESS;
    }
}

static git_svn_status_t
node_delete(int fd, const node_t *node)
{
    return io_printf(fd, "D \"%s\"\n", node->path);
}

static git_svn_status_t
node_replace(int fd, const node_t *node, apr_pool_t *pool)
{
    git_svn_status_t err;
    err = node_delete(fd, node);
    if (err) {
        return err;
    }
    return node_modify(fd, node, pool);
}

static git_svn_status_t
handle_node(int fd, const node_t *node, apr_pool_t *pool)
{
    switch (node->action) {
    case ACTION_ADD:
    case ACTION_CHANGE:
        return node_modify(fd, node, pool);
    case ACTION_DELETE:
        return node_delete(fd, node);
    case ACTION_REPLACE:
        return node_replace(fd, node, pool);
    default:
        return GIT_SVN_SUCCESS;
    }
}

git_svn_status_t
backend_write_commit(const backend_t *be,
                     const branch_t *branch,
                     const commit_t *commit,
                     const apr_array_header_t *nodes,
                     const char *author,
                     const char *message,
                     int64_t timestamp,
                     apr_pool_t *pool)
{
    git_svn_status_t err;

    err = write_commit_header(be->out, branch, commit, author, message, timestamp);
    if (err) {
        return err;
    }

    for (int i = 0; i < nodes->nelts; i++) {
        const node_t *node = &APR_ARRAY_IDX(nodes, i, node_t);
        err = handle_node(be->out, node, pool);
        if (err) {
            return err;
        }
    }

    return GIT_SVN_SUCCESS;
}

git_svn_status_t
backend_reset_branch(const backend_t *be,
                     const branch_t *branch,
                     const commit_t *commit)
{
    git_svn_status_t err;

    err = io_printf(be->out, "reset %s\n", branch->refname);
    if (err) {
        return err;
    }

    commit = commit_get_effective_commit(commit);

    return io_printf(be->out, "from :%d\n", commit->mark);
}

git_svn_status_t
backend_write_blob_header(const backend_t *be, const blob_t *blob)
{
    git_svn_status_t err;

    err = io_printf(be->out, "blob\n");
    if (err) {
        return err;
    }

    err = io_printf(be->out, "mark :%d\n", blob->mark);
    if (err) {
        return err;
    }

    err = io_printf(be->out, "data %ld\n", blob->length);
    if (err) {
        return err;
    }

    return GIT_SVN_SUCCESS;
}

git_svn_status_t
backend_remove_branch(const backend_t *be, const branch_t *branch)
{
    git_svn_status_t err;

    err = io_printf(be->out, "reset %s\n", branch->refname);
    if (err) {
        return err;
    }

    err = io_printf(be->out, "from %s\n", NULL_SHA1);
    if (err) {
        return err;
    }

    return GIT_SVN_SUCCESS;
}

git_svn_status_t
backend_notify_revision_skipped(const backend_t *be, revnum_t revnum)
{
    return io_printf(be->out, "progress Skipped revision %d\n", revnum);
}

git_svn_status_t
backend_notify_revision_imported(const backend_t *be, revnum_t revnum)
{
    return io_printf(be->out, "progress Imported revision %d\n", revnum);
}

git_svn_status_t
backend_notify_branch_found(const backend_t *be, const branch_t *branch)
{
    if (be->verbose) {
        return io_printf(be->out, "progress Found %s\n", branch->refname);
    }
    return GIT_SVN_SUCCESS;
}

git_svn_status_t
backend_notify_branch_updated(const backend_t *be, const branch_t *branch)
{
    if (be->verbose) {
        return io_printf(be->out, "progress Updated %s\n", branch->refname);
    }
    return GIT_SVN_SUCCESS;
}

git_svn_status_t
backend_notify_branch_removed(const backend_t *be, const branch_t *branch)
{
    if (be->verbose) {
        return io_printf(be->out, "progress Removed %s\n", branch->refname);
    }
    return GIT_SVN_SUCCESS;
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

git_svn_status_t
backend_get_checksum(svn_checksum_t **dst,
                     const backend_t *be,
                     const commit_t *commit,
                     const char *path,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
    git_svn_status_t err;
    svn_boolean_t eof;
    svn_error_t *svn_err;
    svn_stringbuf_t *buf;

    commit = commit_get_effective_commit(commit);

    err = io_printf(be->out, "ls :%d \"%s\"\n", commit->mark, path);
    if (err) {
        return err;
    }

    svn_err = svn_stream_readline(be->back, &buf, "\n", &eof, scratch_pool);
    if (svn_err) {
        handle_svn_error(svn_err);
        return GIT_SVN_FAILURE;
    }

    svn_err = parse_checksum(dst, buf, result_pool);
    if (svn_err) {
        handle_svn_error(svn_err);
        return GIT_SVN_FAILURE;
    }

    return GIT_SVN_SUCCESS;
}

git_svn_status_t
backend_finished(const backend_t *be)
{
    return io_printf(be->out, "done\n");
}
