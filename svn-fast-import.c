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

#include <apr.h>

int
main(int argc, char **argv)
{
    apr_pool_t *pool;
    apr_status_t apr_err;
    git_svn_status_t err;

    apr_initialize();

    apr_err = apr_pool_create_core(&pool);
    if (apr_err != APR_SUCCESS) {
        return apr_err;
    }


    err = git_svn_parse_dumpstream(pool);
    if (err != GIT_SVN_SUCCESS) {
        return err;
    }

    apr_pool_destroy(pool);

    return GIT_SVN_SUCCESS;
}
