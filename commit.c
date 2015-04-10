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

#include "commit.h"

// commit_t implementation.
struct commit_t
{
    mark_t mark;
    bool dummy;
    const commit_t *parent;
    const commit_t *copyfrom;
};

static const commit_t *
get_effective_commit(const commit_t *commit)
{
    while (commit != NULL && commit->dummy) {
        commit = commit->parent;
    }

    return commit;
}

commit_t *
commit_create(apr_pool_t *pool)
{
    return apr_pcalloc(pool, sizeof(commit_t));
}

mark_t
commit_mark_get(const commit_t *c)
{
    const commit_t *commit = get_effective_commit(c);

    return commit->mark;
}

void
commit_mark_set(commit_t *c, mark_t mark)
{
    c->mark = mark;
}

const commit_t *
commit_copyfrom_get(const commit_t *c)
{
    return c->copyfrom;
}

void
commit_copyfrom_set(commit_t *c, const commit_t *copyfrom)
{
    c->copyfrom = copyfrom;
}

const commit_t *
commit_parent_get(const commit_t *c)
{
    return c->parent;
}

void
commit_parent_set(commit_t *c, const commit_t *p)
{
    c->parent = p;
}

void
commit_dummy_set(commit_t *c)
{
    c->dummy = true;
}
