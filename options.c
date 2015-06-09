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

#include "options.h"
#include <svn_cmdline.h>

static const char *
format_option(const apr_getopt_option_t *opt,
              apr_pool_t *pool)
{
    char *opts;

    if (opt->optch && opt->name) {
        opts = apr_psprintf(pool, "-%c [--%s]", opt->optch, opt->name);
    } else if (opt->optch) {
        opts = apr_psprintf(pool, "-%c", opt->optch);
    } else {
        opts = apr_psprintf(pool, "--%s", opt->name);
    }

    if (opt->has_arg) {
        opts = apr_pstrcat(pool, opts, " ARG", NULL);
    }

    opts = apr_psprintf(pool, "%-24s %s", opts, opt->description);

    return opts;
}

void
print_usage(const apr_getopt_option_t *options, apr_pool_t *pool)
{
    svn_error_clear(svn_cmdline_printf(pool, "usage: svn-fast-export [OPTIONS] REPO\n"));

    while (options->description) {
        const char *optstr;
        optstr = format_option(options, pool);
        svn_error_clear(svn_cmdline_printf(pool, "\t%s\n", optstr));
        ++options;
    }
}
