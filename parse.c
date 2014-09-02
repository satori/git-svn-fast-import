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


static svn_error_t *
magic_header_record(int version, void *ctx, apr_pool_t *pool)
{
    return SVN_NO_ERROR;
}


static svn_error_t *
uuid_record(const char *uuid, void *ctx, apr_pool_t *pool)
{
    return SVN_NO_ERROR;
}


static svn_error_t *
new_revision_record(void **ctx, apr_hash_t *headers, void *p_ctx, apr_pool_t *pool)
{
    return SVN_NO_ERROR;
}


static svn_error_t *
new_node_record(void **ctx, apr_hash_t *headers, void *r_ctx, apr_pool_t *pool)
{
    return SVN_NO_ERROR;
}


static svn_error_t *
set_revision_property(void *ctx, const char *name, const svn_string_t *value)
{
    return SVN_NO_ERROR;
}


static svn_error_t *
set_node_property(void *ctx, const char *name, const svn_string_t *value)
{
    return SVN_NO_ERROR;
}


static svn_error_t *
remove_node_props(void *ctx)
{
    return SVN_NO_ERROR;
}


static svn_error_t *
delete_node_property(void *ctx, const char *name)
{
    return SVN_NO_ERROR;
}


static svn_error_t *
set_fulltext(svn_stream_t **stream, void *ctx)
{
    return SVN_NO_ERROR;
}


static svn_error_t *
apply_textdelta(svn_txdelta_window_handler_t *handler, void **handler_baton, void *node_ctx)
{
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
    return SVN_NO_ERROR;
}


git_svn_parser_t *
git_svn_parser_create(apr_pool_t *pool)
{
    git_svn_parser_t *p = apr_pcalloc(pool, sizeof(*p));

    p->magic_header_record = magic_header_record;
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


    err = svn_repos_parse_dumpstream3(input, parser, ctx, FALSE, check_cancel, NULL, pool);
    if (err != NULL) {
        return GIT_SVN_FAILURE;
    }

    return GIT_SVN_SUCCESS;
}
