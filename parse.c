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

#include "parse.h"

#include "dump.h"

#include <svn_hash.h>
#include <svn_props.h>
#include <svn_time.h>

#define GIT_SVN_NODE_MODE_NORMAL     0100644
#define GIT_SVN_NODE_MODE_EXECUTABLE 0100755
#define GIT_SVN_NODE_MODE_SYMLINK    0120000


#if (SVN_VER_MAJOR == 1 && SVN_VER_MINOR > 7)
static svn_error_t *
magic_header_record(int version, void *ctx, apr_pool_t *pool)
{
    return SVN_NO_ERROR;
}
#endif


static svn_error_t *
uuid_record(const char *uuid, void *ctx, apr_pool_t *pool)
{
    return SVN_NO_ERROR;
}


static svn_error_t *
new_revision_record(void **ctx, apr_hash_t *headers, void *p_ctx, apr_pool_t *pool)
{
    const char *value;
    git_svn_parser_ctx_t *parser_ctx = p_ctx;
    git_svn_revision_ctx_t *rev_ctx = apr_pcalloc(pool, sizeof(*rev_ctx));
    git_svn_revision_t *rev = apr_pcalloc(pool, sizeof(*rev));

    rev->revnum = SVN_INVALID_REVNUM;
    rev->branch = "master";

    value = apr_hash_get(headers, SVN_REPOS_DUMPFILE_REVISION_NUMBER, APR_HASH_KEY_STRING);
    if (value != NULL) {
        rev->revnum = SVN_STR_TO_REV(value);
    }

    rev_ctx->rev = rev;
    rev_ctx->nodes = apr_array_make(pool, 8, sizeof(git_svn_node_t));
    rev_ctx->pool = pool;
    rev_ctx->parser_ctx = parser_ctx;

    *ctx = rev_ctx;

    return SVN_NO_ERROR;
}


static svn_error_t *
new_node_record(void **ctx, apr_hash_t *headers, void *r_ctx, apr_pool_t *pool)
{
    const char *value;
    git_svn_revision_ctx_t *rev_ctx = r_ctx;
    git_svn_parser_ctx_t *parser_ctx = rev_ctx->parser_ctx;
    git_svn_node_ctx_t *node_ctx = apr_pcalloc(pool, sizeof(*node_ctx));
    git_svn_node_t *node = &APR_ARRAY_PUSH(rev_ctx->nodes, git_svn_node_t);

    node->mode = GIT_SVN_NODE_MODE_NORMAL;
    node->kind = GIT_SVN_NODE_UNKNOWN;

    value = apr_hash_get(headers, SVN_REPOS_DUMPFILE_NODE_PATH, APR_HASH_KEY_STRING);
    if (value != NULL) {
        node->path = apr_pstrdup(rev_ctx->pool, value);
    }

    value = apr_hash_get(headers, SVN_REPOS_DUMPFILE_NODE_KIND, APR_HASH_KEY_STRING);
    if (value != NULL) {
        if (strcmp(value, "file") == 0) {
            node->kind = GIT_SVN_NODE_FILE;
        }
        else if (strcmp(value, "dir") == 0) {
            node->kind = GIT_SVN_NODE_DIR;
        }
    }

    value = apr_hash_get(headers, SVN_REPOS_DUMPFILE_NODE_ACTION, APR_HASH_KEY_STRING);
    if (value != NULL) {
        if (strcmp(value, "change") == 0) {
            node->action = GIT_SVN_NODE_CHANGE;
        }
        else if (strcmp(value, "add") == 0) {
            node->action = GIT_SVN_NODE_ADD;
        }
        else if (strcmp(value, "delete") == 0) {
            node->action = GIT_SVN_NODE_DELETE;
        }
        else if (strcmp(value, "replace") == 0) {
            node->action = GIT_SVN_NODE_REPLACE;
        }
    }

    value = apr_hash_get(headers, SVN_REPOS_DUMPFILE_TEXT_CONTENT_SHA1, APR_HASH_KEY_STRING);
    if (value == NULL) {
        value = apr_hash_get(headers, SVN_REPOS_DUMPFILE_TEXT_COPY_SOURCE_SHA1, APR_HASH_KEY_STRING);
    }

    if (value != NULL) {
        git_svn_blob_t *blob = apr_hash_get(parser_ctx->blobs, value, APR_HASH_KEY_STRING);

        if (blob == NULL) {
            blob = apr_pcalloc(parser_ctx->pool, sizeof(*blob));
            blob->mark = parser_ctx->last_mark++;
            blob->checksum = apr_pstrdup(parser_ctx->pool, value);

            value = apr_hash_get(headers, SVN_REPOS_DUMPFILE_TEXT_CONTENT_LENGTH, APR_HASH_KEY_STRING);
            if (value != NULL) {
                blob->length = svn__atoui64(value);
            }

            apr_hash_set(parser_ctx->blobs, blob->checksum, APR_HASH_KEY_STRING, blob);
        }

        node->blob = blob;
    }

    node_ctx->node = node;
    node_ctx->pool = pool;
    node_ctx->rev_ctx = rev_ctx;

    *ctx = node_ctx;

    return SVN_NO_ERROR;
}


static svn_error_t *
set_revision_property(void *ctx, const char *name, const svn_string_t *value)
{
    git_svn_revision_ctx_t *rev_ctx = ctx;
    git_svn_revision_t *rev = rev_ctx->rev;
    apr_pool_t *pool = rev_ctx->pool;

    // It is safe to ignore revision 0 properties
    if (rev->revnum == 0) {
        return SVN_NO_ERROR;
    }

    if (strcmp(name, SVN_PROP_REVISION_AUTHOR) == 0) {
        rev->author = apr_pstrdup(pool, value->data);
    }
    else if (strcmp(name, SVN_PROP_REVISION_DATE) == 0) {
        apr_time_t timestamp;
        svn_time_from_cstring(&timestamp, value->data, pool);
        rev->timestamp = apr_time_sec(timestamp);
    }
    else if (strcmp(name, SVN_PROP_REVISION_LOG) == 0) {
        rev->message = apr_pstrdup(pool, value->data);
    }

    return SVN_NO_ERROR;
}


static svn_error_t *
set_node_property(void *ctx, const char *name, const svn_string_t *value)
{
    git_svn_node_ctx_t *node_ctx = ctx;
    git_svn_node_t *node = node_ctx->node;

    if (strcmp(name, SVN_PROP_EXECUTABLE) == 0) {
        node->mode = GIT_SVN_NODE_MODE_EXECUTABLE;
    }
    else if (strcmp(name, SVN_PROP_SPECIAL) == 0) {
        node->mode = GIT_SVN_NODE_MODE_SYMLINK;
    }

    return SVN_NO_ERROR;
}


static svn_error_t *
remove_node_props(void *ctx)
{
    git_svn_node_ctx_t *node_ctx = ctx;
    git_svn_node_t *node = node_ctx->node;

    node->mode = GIT_SVN_NODE_MODE_NORMAL;

    return SVN_NO_ERROR;
}


static svn_error_t *
delete_node_property(void *ctx, const char *name)
{
    git_svn_node_ctx_t *node_ctx = ctx;
    git_svn_node_t *node = node_ctx->node;

    if (strcmp(name, SVN_PROP_EXECUTABLE) == 0 || strcmp(name, SVN_PROP_SPECIAL) == 0) {
        node->mode = GIT_SVN_NODE_MODE_NORMAL;
    }

    return SVN_NO_ERROR;
}


static svn_error_t *
set_fulltext(svn_stream_t **stream, void *ctx)
{
    git_svn_node_ctx_t *node_ctx = ctx;
    git_svn_node_t *node = node_ctx->node;
    git_svn_blob_t *blob = node->blob;
    apr_pool_t *pool = node_ctx->pool;

    SVN_ERR(svn_stream_for_stdout(stream, pool));
    SVN_ERR(git_svn_dump_blob_header(*stream, blob, pool));

    return SVN_NO_ERROR;
}


static svn_error_t *
apply_textdelta(svn_txdelta_window_handler_t *handler, void **handler_baton, void *node_ctx)
{
    // TODO
    return SVN_NO_ERROR;
}


static svn_error_t *
close_node(void *ctx)
{
    return SVN_NO_ERROR;
}


static svn_error_t *
close_revision(void *ctx)
{
    git_svn_revision_ctx_t *rev_ctx = ctx;
    git_svn_revision_t *rev = rev_ctx->rev;
    apr_pool_t *pool = rev_ctx->pool;
    svn_stream_t *out = rev_ctx->parser_ctx->output;

    if (rev->revnum == 0) {
        SVN_ERR(git_svn_dump_revision_noop(out, rev, pool));
        return SVN_NO_ERROR;
    }

    SVN_ERR(git_svn_dump_revision_begin(out, rev, pool));

    for (int i = 0; i < rev_ctx->nodes->nelts; i++) {
        git_svn_node_t *node = &APR_ARRAY_IDX(rev_ctx->nodes, i, git_svn_node_t);
        SVN_ERR(git_svn_dump_node(out, node, pool));
    }

    SVN_ERR(git_svn_dump_revision_end(out, rev, pool));

    return SVN_NO_ERROR;
}


git_svn_parser_t *
git_svn_parser_create(apr_pool_t *pool)
{
    git_svn_parser_t *p = apr_pcalloc(pool, sizeof(*p));

#if (SVN_VER_MAJOR == 1 && SVN_VER_MINOR > 7)
    p->magic_header_record = magic_header_record;
#endif
    p->uuid_record = uuid_record;
    p->new_revision_record = new_revision_record;
    p->new_node_record = new_node_record;
    p->set_revision_property = set_revision_property;
    p->set_node_property = set_node_property;
    p->remove_node_props = remove_node_props;
    p->delete_node_property = delete_node_property;
    p->set_fulltext = set_fulltext;
    p->apply_textdelta = apply_textdelta;
    p->close_node = close_node;
    p->close_revision = close_revision;

    return p;
}


static svn_error_t *
check_cancel(void *ctx)
{
    return SVN_NO_ERROR;
}


git_svn_status_t
git_svn_parser_parse(git_svn_parser_t *parser, apr_pool_t *pool)
{
    svn_error_t *err;

    svn_stream_t *input;
    // Read the input from stdin
    err = svn_stream_for_stdin(&input, pool);
    if (err != NULL) {
        return GIT_SVN_FAILURE;
    }

    git_svn_parser_ctx_t *ctx = apr_pcalloc(pool, sizeof(*ctx));
    ctx->pool = pool;
    ctx->blobs = apr_hash_make(pool);
    ctx->last_mark = 1;

    // Write to stdout
    err = svn_stream_for_stdout(&ctx->output, pool);
    if (err != NULL) {
        return GIT_SVN_FAILURE;
    }

#if (SVN_VER_MAJOR == 1 && SVN_VER_MINOR > 7)
    err = svn_repos_parse_dumpstream3(input, parser, ctx, FALSE, check_cancel, NULL, pool);
#else
    err = svn_repos_parse_dumpstream2(input, parser, ctx, check_cancel, NULL, pool);
#endif
    if (err != NULL) {
        return GIT_SVN_FAILURE;
    }

    return GIT_SVN_SUCCESS;
}
