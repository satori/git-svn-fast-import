/* Copyright (C) 2015 by Maxim Bublis <b@codemonkey.ru>
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

#include "author.h"
#include "utils.h"


static svn_error_t *
svn_malformed_file_error(svn_stream_t *src, int lineno, const char *line) {
    return svn_error_createf(SVN_ERR_MALFORMED_FILE, NULL,
                             "line %d: %s",
                             lineno, line);
}


svn_error_t *
git_svn_parse_authors(apr_hash_t *dst,
                      svn_stream_t *src,
                      apr_pool_t *scratch_pool)
{
    apr_pool_t *pool = apr_hash_pool_get(dst);
    svn_boolean_t eof;
    int lineno = 0;

    while (true) {
        author_t *author = apr_pcalloc(pool, sizeof(author_t));
        const char *next, *prev, *end;
        svn_stringbuf_t *buf;

        SVN_ERR(svn_stream_readline(src, &buf, "\n", &eof, scratch_pool));
        if (eof) {
            break;
        }

        // Increase lineno counter
        lineno++;

        // Parse Subversion username.
        prev = cstring_skip_whitespace(buf->data);
        if (*prev == '#' || *prev == '\0') { // Skip comments and empty lines
            continue;
        }
        next = strchr(prev, '=');
        if (next == NULL || next == prev) {
            return svn_malformed_file_error(src, lineno, buf->data);
        }
        end = cstring_rskip_whitespace(next - 1);
        author->svn_name = apr_pstrndup(pool, prev, end - prev + 1);

        // Skip '=' character.
        next++;

        // Parse name.
        prev = cstring_skip_whitespace(next);
        next = strchr(prev, '<');
        if (next == NULL || next == prev) {
            return svn_malformed_file_error(src, lineno, buf->data);
        }
        end = cstring_rskip_whitespace(next - 1);
        author->name = apr_pstrndup(pool, prev, end - prev + 1);

        // Skip '<' character.
        next++;

        // Parse email.
        prev = cstring_skip_whitespace(next);
        next = strchr(prev, '>');
        if (next == NULL || next == prev) {
            return svn_malformed_file_error(src, lineno, buf->data);
        }
        end = cstring_rskip_whitespace(next - 1);
        author->email = apr_pstrndup(pool, prev, end - prev + 1);

        // Save
        apr_hash_set(dst, author->svn_name, APR_HASH_KEY_STRING, author);
    }

    return SVN_NO_ERROR;
}
