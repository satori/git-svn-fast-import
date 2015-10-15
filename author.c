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
#include <svn_hash.h>

#define UNKNOWN "unknown"

// author_t implementation.
struct author_t
{
    // SVN name
    const char *svn_name;
    // Commit author name
    const char *name;
    // Commit author email
    const char *email;
};

static struct author_t unknown_author = {
    UNKNOWN, UNKNOWN, UNKNOWN"@"UNKNOWN
};

const char *
author_to_cstring(const author_t *a, apr_pool_t *pool)
{
    return apr_psprintf(pool, "%s <%s>", a->name, a->email);
}

// author_storage_t implementation.
struct author_storage_t
{
    apr_pool_t *pool;
    apr_hash_t *authors;
};

author_storage_t *
author_storage_create(apr_pool_t *pool)
{
    author_storage_t *as = apr_pcalloc(pool, sizeof(author_storage_t));
    as->pool = pool;
    as->authors = apr_hash_make(pool);

    return as;
}

const author_t *
author_storage_lookup(const author_storage_t *as, const char *name)
{
    author_t *author;

    author = svn_hash_gets(as->authors, name);
    if (author == NULL) {
        author = apr_pcalloc(as->pool, sizeof(author_t));
        author->svn_name = apr_pstrdup(as->pool, name);
        author->name = UNKNOWN;
        author->email = apr_psprintf(as->pool, "%s@"UNKNOWN, name);
        svn_hash_sets(as->authors, author->svn_name, author);
    }

    return author;
}

const author_t *
author_storage_default_author(const author_storage_t *as)
{
    return &unknown_author;
}

static svn_error_t *
svn_malformed_file_error(int lineno, const char *line)
{
    return svn_error_createf(SVN_ERR_MALFORMED_FILE, NULL,
                             "line %d: %s",
                             lineno, line);
}

svn_error_t *
author_storage_load(author_storage_t *as,
                    svn_stream_t *src,
                    apr_pool_t *pool)
{
    svn_boolean_t eof;
    int lineno = 0;

    while (TRUE) {
        author_t *author = apr_pcalloc(as->pool, sizeof(author_t));
        const char *next, *prev, *end;
        svn_stringbuf_t *buf;

        SVN_ERR(svn_stream_readline(src, &buf, "\n", &eof, pool));
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
            return svn_malformed_file_error(lineno, buf->data);
        }
        end = cstring_rskip_whitespace(next - 1);
        author->svn_name = apr_pstrndup(as->pool, prev, end - prev + 1);

        // Skip '=' character.
        next++;

        // Parse name.
        prev = cstring_skip_whitespace(next);
        next = strchr(prev, '<');
        if (next == NULL || next == prev) {
            return svn_malformed_file_error(lineno, buf->data);
        }
        end = cstring_rskip_whitespace(next - 1);
        author->name = apr_pstrndup(as->pool, prev, end - prev + 1);

        // Skip '<' character.
        next++;

        // Parse email.
        prev = cstring_skip_whitespace(next);
        next = strchr(prev, '>');
        if (next == NULL || next == prev) {
            return svn_malformed_file_error(lineno, buf->data);
        }
        end = cstring_rskip_whitespace(next - 1);
        author->email = apr_pstrndup(as->pool, prev, end - prev + 1);

        // Save
        svn_hash_sets(as->authors, author->svn_name, author);
    }

    return SVN_NO_ERROR;
}

svn_error_t *
author_storage_load_path(author_storage_t *as,
                         const char *path,
                         apr_pool_t *pool)
{
    svn_error_t *err;
    svn_stream_t *src;

    SVN_ERR(svn_stream_open_readonly(&src, path, pool, pool));
    err = author_storage_load(as, src, pool);
    if (err) {
        return svn_error_quick_wrap(err, "Malformed authors file");
    }
    SVN_ERR(svn_stream_close(src));

    return SVN_NO_ERROR;
}
