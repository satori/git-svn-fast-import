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

#include "io.h"

#include <unistd.h>

#define BUFSIZE 1024

git_svn_status_t
io_printf(int fd, const char *fmt, ...)
{
    git_svn_status_t err;
    va_list args;

    va_start(args, fmt);
    vdprintf(fd, fmt, args);
    err = errno;
    va_end(args);

    if (err && err != EILSEQ) {
        handle_errno(err);
        return GIT_SVN_FAILURE;
    }

    return GIT_SVN_SUCCESS;
}

git_svn_status_t
io_readline(int fd, char **dst)
{
    size_t capacity = BUFSIZE, len = 0;
    char *buf = calloc(capacity, sizeof(char));

    while (1) {
        char c;
        ssize_t n;

        n = read(fd, &c, 1);
        if (n != 1) {
            goto error;
        }
        if (c == '\n') {
            break;
        }
        buf[len++] = c;

        if (len == capacity) {
            char *newbuf;
            newbuf = realloc(buf, capacity << 1);
            if (newbuf == NULL) {
                goto error;
            }
            buf = newbuf;
            capacity = capacity << 1;
        }
    }

    *dst = buf;
    return GIT_SVN_SUCCESS;

error:
    if (errno) {
        handle_errno(errno);
    }
    free(buf);
    return GIT_SVN_FAILURE;
}
