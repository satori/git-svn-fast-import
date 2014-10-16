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
write_commit_header(int fd,
                    const commit_t *commit,
                    const char *author,
                    const char *message,
                    int64_t timestamp)
{
    const char *prefix = "";
    git_svn_status_t err;

    if (commit->branch->is_tag) {
        prefix = "tags/";
    }

    err = io_printf(fd, "commit refs/heads/%s%s\n", prefix, commit->branch->name);
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
        err = io_printf(fd, "from :%d\n", commit->copyfrom->mark);
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
node_modify_checksum(int fd, const node_t *node)
{
    git_svn_status_t err;
    char checksum[CHECKSUM_CHARS_LENGTH + 1];
    checksum[CHECKSUM_CHARS_LENGTH] = '\0';

    err = bytes_to_hex(checksum, node->content.data.checksum, CHECKSUM_BYTES_LENGTH);
    if (err) {
        return err;
    }

    return io_printf(fd, "M %o %s \"%s\"\n", node->mode, checksum, node->path);
}

static git_svn_status_t
node_modify(int fd, const node_t *node)
{
    switch (node->content.kind) {
    case CONTENT_CHECKSUM:
        return node_modify_checksum(fd, node);
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
node_replace(int fd, const node_t *node)
{
    git_svn_status_t err;
    err = node_delete(fd, node);
    if (err) {
        return err;
    }
    return node_modify(fd, node);
}

static git_svn_status_t
handle_node(int fd, const node_t *node)
{
    switch (node->action) {
    case ACTION_ADD:
    case ACTION_CHANGE:
        return node_modify(fd, node);
    case ACTION_DELETE:
        return node_delete(fd, node);
    case ACTION_REPLACE:
        return node_replace(fd, node);
    default:
        return GIT_SVN_SUCCESS;
    }
}

git_svn_status_t
backend_write_commit(backend_t *be,
                     const commit_t *commit,
                     const apr_array_header_t *nodes,
                     const char *author,
                     const char *message,
                     int64_t timestamp)
{
    git_svn_status_t err;

    err = write_commit_header(be->out, commit, author, message, timestamp);
    if (err) {
        return err;
    }

    for (int i = 0; i < nodes->nelts; i++) {
        node_t *node = &APR_ARRAY_IDX(nodes, i, node_t);
        err = handle_node(be->out, node);
        if (err) {
            return err;
        }
    }

    return GIT_SVN_SUCCESS;
}

git_svn_status_t
backend_write_blob_header(backend_t *be, const blob_t *blob)
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
backend_notify_revision_skipped(backend_t *be, revnum_t revnum)
{
    return io_printf(be->out, "progress Skipped revision %d\n", revnum);
}

git_svn_status_t
backend_notify_revision_imported(backend_t *be, revnum_t revnum)
{
    return io_printf(be->out, "progress Imported revision %d\n", revnum);
}

static const char *
branch_type(const branch_t *branch)
{
    if (branch->is_tag) {
        return "tag";
    }

    return "branch";
}

git_svn_status_t
backend_notify_branch_found(backend_t *be, const branch_t *branch)
{
    if (be->verbose) {
        return io_printf(be->out, "progress Found %s \"%s\" (at \"%s\")\n",
                         branch_type(branch), branch->name, branch->path);
    }
    return GIT_SVN_SUCCESS;
}

git_svn_status_t
backend_notify_branch_updated(backend_t *be, const branch_t *branch)
{
    if (be->verbose) {
        return io_printf(be->out, "progress Updating %s \"%s\" (at \"%s\")\n",
                         branch_type(branch), branch->name, branch->path);
    }
    return GIT_SVN_SUCCESS;
}

static git_svn_status_t
parse_checksum(uint8_t *dst, const char *src) {
    const char *next, *prev;

    // Check if path is missing
    if (*src == 'm') {
        handle_warning(src);
        return GIT_SVN_FAILURE;
    }

    next = src;

    for (int i = 0; i < 3; i++) {
        prev = next;
        next = strchr(prev, ' ');
        next++;
    }

    return hex_to_bytes(dst, prev, CHECKSUM_BYTES_LENGTH);
}

git_svn_status_t
backend_get_checksum(backend_t *be,
                     uint8_t *dst,
                     const commit_t *commit,
                     const char *path)
{
    char *line;
    git_svn_status_t err;

    err = io_printf(be->out, "ls :%d \"%s\"\n", commit->mark, path);
    if (err) {
        return err;
    }

    err = io_readline(be->back, &line);
    if (err) {
        return err;
    }

    err = parse_checksum(dst, line);

    free(line);

    return err;
}

git_svn_status_t
backend_finished(backend_t *be)
{
    return io_printf(be->out, "done\n");
}
