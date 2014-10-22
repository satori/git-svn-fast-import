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

#include "options.h"

#include <apr_getopt.h>

static struct apr_getopt_option_t cmdline_options[] = {
    {"stdlayout", 's', 0, ""},
    {"trunk", 'T', 1, ""},
    {"tags", 't', 1, ""},
    {"branches", 'b', 1, ""},
    {"ignore-path", 'I', 1, ""},
    {"verbose", 'v', 0, ""},
    {0, 0, 0, 0}
};

git_svn_status_t
git_svn_parse_options(git_svn_options_t *options, int argc, const char **argv, apr_pool_t *pool)
{
    apr_status_t apr_err;
    apr_getopt_t *arg_parser;

    apr_err = apr_getopt_init(&arg_parser, pool, argc, argv);
    if (apr_err != APR_SUCCESS) {
        return GIT_SVN_FAILURE;
    }

    options->verbose = 0;
    options->trunk = "";
    options->branches = tree_create(pool);
    options->tags = tree_create(pool);
    options->ignore = tree_create(pool);

    while (1) {
        int opt_id;
        const char *opt_arg;
        apr_err = apr_getopt_long(arg_parser, cmdline_options, &opt_id, &opt_arg);

        if (apr_err) {
            if (APR_STATUS_IS_EOF(apr_err)) {
                break;
            }
            return GIT_SVN_FAILURE;
        }

        switch (opt_id) {
        case 's':
            options->trunk = "trunk";
            tree_insert(options->branches, "branches", "branches");
            tree_insert(options->tags, "tags", "tags");
            break;
        case 'T':
            options->trunk = opt_arg;
            break;
        case 't':
            tree_insert(options->tags, opt_arg, opt_arg);
            break;
        case 'b':
            tree_insert(options->branches, opt_arg, opt_arg);
            break;
        case 'I':
            tree_insert(options->ignore, opt_arg, opt_arg);
            break;
        case 'v':
            options->verbose = 1;
            break;
        default:
            return GIT_SVN_FAILURE;
        }
    }

    return GIT_SVN_SUCCESS;
}
