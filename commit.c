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

typedef struct
{
    svn_revnum_t revnum;
    branch_t *branch;
} cache_key_t;

commit_cache_t *
commit_cache_create(apr_pool_t *pool)
{
    commit_cache_t *c = apr_pcalloc(pool, sizeof(commit_cache_t));
    c->pool = pool;
    c->commits = apr_array_make(pool, 0, sizeof(commit_t));
    c->idx = apr_hash_make(pool);
    c->marks = apr_array_make(pool, 0, sizeof(commit_t *));

    return c;
}

commit_t *
commit_cache_get(commit_cache_t *c, svn_revnum_t revnum, branch_t *branch)
{
    while (revnum > 0) {
        cache_key_t key = {revnum, branch};
        commit_t *commit = apr_hash_get(c->idx, &key, sizeof(cache_key_t));
        if (commit != NULL) {
            return commit;
        }

        --revnum;
    }

    return NULL;
}

commit_t *
commit_cache_get_by_mark(commit_cache_t *c, mark_t mark)
{
    return APR_ARRAY_IDX(c->marks, mark - 1, commit_t *);
}

commit_t *
commit_cache_add(commit_cache_t *c, svn_revnum_t revnum, branch_t *branch)
{
    commit_t *commit = apr_array_push(c->commits);
    commit->revnum = revnum;
    commit->branch = branch;
    commit->merges = apr_array_make(c->pool, 0, sizeof(mark_t));

    apr_hash_set(c->idx, commit, sizeof(cache_key_t), commit);

    return commit;
}

void
commit_cache_set_mark(commit_cache_t *c, commit_t *commit)
{
    APR_ARRAY_PUSH(c->marks, commit_t *) = commit;
    commit->mark = c->marks->nelts;
}

// This function implements simple depth-first search for determining
// if other commit has already been merged into first commit.
// It uses apr_array_header_t as a LIFO queue for marks.
// XXX: It is possible that breadth-first search is better-suited for
// that problem, in that case should use FIFO queue.
static svn_boolean_t
commit_is_merged(commit_cache_t *c, commit_t *commit, mark_t other,
                 apr_pool_t *scratch_pool)
{
    apr_array_header_t *queue;

    // Copy merge commits into queue.
    queue = apr_array_copy(scratch_pool, commit->merges);
    // Add parent commit into queue.
    if (commit->parent) {
        APR_ARRAY_PUSH(queue, mark_t) = commit->parent;
    }

    // It is a simple depth-first search.
    while (queue->nelts) {
        commit_t *merged_commit;
        mark_t *merged = apr_array_pop(queue);
        if (*merged == other) {
            return TRUE;
        }

        merged_commit = commit_cache_get_by_mark(c, *merged);

        // Copy merge commits into queue.
        apr_array_cat(queue, merged_commit->merges);
        // Add parent commit into queue.
        if (merged_commit->parent) {
            APR_ARRAY_PUSH(queue, mark_t) = merged_commit->parent;
        }
    }

    return FALSE;
}

void
commit_cache_add_merge(commit_cache_t *c,
                       commit_t *commit,
                       commit_t *other,
                       apr_pool_t *scratch_pool)
{
    apr_array_header_t *new_merges;

    if (commit_is_merged(c, commit, other->mark, scratch_pool)) {
        // Commit is already merged, nothing to do.
        return;
    }

    new_merges = apr_array_make(scratch_pool, 1, sizeof(mark_t));
    APR_ARRAY_PUSH(new_merges, mark_t) = other->mark;

    // Filter old merges by removing merged one into just added merge commit.
    for (int i = 0; i < commit->merges->nelts; i++) {
        mark_t merged = APR_ARRAY_IDX(commit->merges, i, mark_t);

        if (!commit_is_merged(c, other, merged, scratch_pool)) {
            APR_ARRAY_PUSH(new_merges, mark_t) = merged;
        }
    }

    // Set new merges for commit.
    apr_array_clear(commit->merges);
    apr_array_cat(commit->merges, new_merges);
}

svn_error_t *
commit_cache_dump(commit_cache_t *c, svn_stream_t *dst, apr_pool_t *pool)
{
    for (int i = 0; i < c->commits->nelts; i++) {
        commit_t *commit = &APR_ARRAY_IDX(c->commits, i, commit_t);
        SVN_ERR(svn_stream_printf(dst, pool, "%ld %s %s :%d\n",
                                  commit->revnum,
                                  commit->branch->refname,
                                  commit->branch->path,
                                  commit->mark));
    }

    return SVN_NO_ERROR;
}

svn_error_t *
commit_cache_dump_path(commit_cache_t *c, const char *path, apr_pool_t *pool)
{
    apr_file_t *fd;
    svn_stream_t *dst;

    SVN_ERR(svn_io_file_open(&fd, path,
                             APR_CREATE | APR_TRUNCATE | APR_BUFFERED | APR_WRITE,
                             APR_OS_DEFAULT, pool));

    dst = svn_stream_from_aprfile2(fd, FALSE, pool);
    SVN_ERR(commit_cache_dump(c, dst, pool));
    SVN_ERR(svn_stream_close(dst));

    return SVN_NO_ERROR;
}
