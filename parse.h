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

#ifndef GIT_SVN_FAST_IMPORT_PARSE_H_
#define GIT_SVN_FAST_IMPORT_PARSE_H_

#include "error.h"
#include "types.h"

#include <svn_repos.h>


typedef svn_repos_parse_fns3_t git_svn_parser_t;

git_svn_parser_t *
git_svn_parser_create(apr_pool_t *);

git_svn_status_t
git_svn_parser_parse(git_svn_parser_t *, apr_pool_t *);


typedef struct
{
    apr_pool_t *pool;
    apr_hash_t *blobs;
    svn_stream_t *output;
    uint32_t last_mark;
} git_svn_parser_ctx_t;


typedef struct
{
    apr_pool_t *pool;
    git_svn_revision_t *rev;
    apr_array_header_t *nodes;
    git_svn_parser_ctx_t *parser_ctx;
} git_svn_revision_ctx_t;


typedef struct
{
    apr_pool_t *pool;
    git_svn_node_t *node;
    git_svn_revision_ctx_t *rev_ctx;
} git_svn_node_ctx_t;


#endif // GIT_SVN_FAST_IMPORT_PARSE_H_
