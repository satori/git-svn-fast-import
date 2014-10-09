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
revision_begin(int fd, revision_t *rev)
{
    git_svn_status_t err;

    err = io_printf(fd, "commit refs/heads/%s\n", rev->branch->name);
    if (err) {
        return err;
    }

    err = io_printf(fd, "mark :%d\n", rev->mark);
    if (err) {
        return err;
    }

    err = io_printf(fd, "committer %s <%s@local> %"PRId64" +0000\n",
                    rev->author, rev->author, rev->timestamp);
    if (err) {
        return err;
    }

    err = io_printf(fd, "data %ld\n", strlen(rev->message));
    if (err) {
        return err;
    }

    err = io_printf(fd, "%s\n", rev->message);
    if (err) {
        return err;
    }

    if (rev->copyfrom != NULL) {
        err = io_printf(fd, "from :%d\n", rev->copyfrom->mark);
        if (err) {
            return err;
        }
    }

    return GIT_SVN_SUCCESS;
}

static git_svn_status_t
revision_end(int fd, revision_t *rev)
{
    return io_printf(fd, "progress Imported revision %d\n", rev->revnum);
}

static git_svn_status_t
node_modify_blob(int fd, node_t *node)
{
    blob_t *blob = node->content.data.blob;

    return io_printf(fd, "M %o :%d \"%s\"\n", node->mode, blob->mark, node->path);
}

static git_svn_status_t
node_modify_checksum(int fd, node_t *node)
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
node_modify(int fd, node_t *node)
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
node_delete(int fd, node_t *node)
{
    return io_printf(fd, "D \"%s\"\n", node->path);
}

static git_svn_status_t
node_replace(int fd, node_t *node)
{
    git_svn_status_t err;
    err = node_delete(fd, node);
    if (err) {
        return err;
    }
    return node_modify(fd, node);
}

static git_svn_status_t
handle_node(int fd, node_t *node)
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
backend_write_revision(backend_t *be, revision_t *rev, apr_array_header_t *nodes)
{
    git_svn_status_t err;

    err = revision_begin(be->out, rev);
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

    err = revision_end(be->out, rev);
    if (err) {
        return err;
    }

    return GIT_SVN_SUCCESS;
}

git_svn_status_t
backend_write_blob_header(backend_t *be, blob_t *blob)
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
backend_notify_skip_revision(backend_t *be, revision_t *rev)
{
    return io_printf(be->out, "progress Skipped revision %d\n", rev->revnum);
}

git_svn_status_t
backend_notify_branch_found(backend_t *be, branch_t *branch)
{
    return io_printf(be->out, "progress Found branch at %s\n", branch->path);
}

static git_svn_status_t
parse_checksum(uint8_t *dst, const char *src) {
    const char *next, *prev;

    // Check if path is missing
    if (*src == 'm') {
        handle_error(src);
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
backend_get_checksum(backend_t *be, uint8_t *sha1, revision_t *rev, const char *path, apr_pool_t *pool)
{
    char *line;
    git_svn_status_t err;

    err = io_printf(be->out, "ls :%d \"%s\"\n", rev->mark, path);
    if (err) {
        return err;
    }

    err = io_readline(be->back, &line);
    if (err) {
        return err;
    }

    err = parse_checksum(sha1, line);

    free(line);

    return err;
}
