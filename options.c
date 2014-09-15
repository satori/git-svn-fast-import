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

#include <stdio.h>
#include <apr_getopt.h>

static struct apr_getopt_option_t cmdline_options[] = {
    {"help", 'h', 0, "\tprint this help message and exit."},
    {"stdlayout", 's', 0, "shorthand way of setting trunk,tags,branches as the relative paths, which is Subversion default."},
    {"trunk", 'T', 1, "set trunk to a relative repository path."},
    {"tags", 't', 1, "set tags to a relative repository path, can be specified multiple times."},
    {"branches", 'b', 1, "set branches to a relative repository path, can be specified multiple times."},
    {0, 0, 0, 0}
};

static void
print_usage(apr_getopt_option_t *opts)
{
    fprintf(stdout, "usage: git-svn-fast-import [options]\n");
    for (int i = 0; ; i++) {
        if (opts[i].optch == 0) {
            break;
        }
        if (opts[i].has_arg) {
            fprintf(stdout, "\t-%c ARG, --%s ARG \t%s\n", opts[i].optch, opts[i].name, opts[i].description);
        }
        else {
            fprintf(stdout, "\t-%c, --%s \t%s\n", opts[i].optch, opts[i].name, opts[i].description);
        }
    }
    fprintf(stdout, "\n");
}

git_svn_status_t
git_svn_parse_options(git_svn_options_t *options, int argc, const char **argv, apr_pool_t *pool)
{
    apr_status_t apr_err;
    apr_getopt_t *arg_parser;

    apr_err = apr_getopt_init(&arg_parser, pool, argc, argv);
    if (apr_err != APR_SUCCESS) {
        return GIT_SVN_FAILURE;
    }

    options->trunk = "";
    options->branches = "";
    options->tags = "";

    while (1) {
        int opt_id;
        const char *opt_arg;
        apr_err = apr_getopt_long(arg_parser, cmdline_options, &opt_id, &opt_arg);
        if (APR_STATUS_IS_EOF(apr_err)) {
            break;
        }
        else if (apr_err) {
            print_usage(cmdline_options);
            return GIT_SVN_FAILURE;
        }

        switch (opt_id) {
        case 's':
            options->trunk = "trunk";
            options->branches = "branches";
            options->tags = "tags";
            break;
        case 'T':
            options->trunk = opt_arg;
            break;
        case 't':
            options->tags = opt_arg;
            break;
        case 'b':
            options->branches = opt_arg;
            break;
        case 'h':
            print_usage(cmdline_options);
            return GIT_SVN_FAILURE;
        default:
            return GIT_SVN_FAILURE;
        }
    }

    return GIT_SVN_SUCCESS;
}
