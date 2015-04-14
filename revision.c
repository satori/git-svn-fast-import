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

#include "revision.h"
#include <apr_ring.h>

static const int one = 1;
static const void *NOT_NULL = &one;

// revision_t implementation
struct revision_t
{
    // SVN revision number.
    svn_revnum_t revnum;
    // branch_t to commit_t mapping.
    apr_hash_t *commits;
    // branch_t remove set.
    apr_hash_t *removes;
    // Support for ring container.
    APR_RING_ENTRY(revision_t) link;
};

// Type for storing revisions as a doubly linked list.
typedef struct revision_list_t revision_list_t;
APR_RING_HEAD(revision_list_t, revision_t);

typedef struct revision_iter_t
{
    const revision_list_t *lst;
    const revision_t *rev;
} revision_iter_t;

static revision_iter_t *
revision_iter_first(const revision_list_t *lst, apr_pool_t *pool)
{
    revision_iter_t *it;
    const revision_t *rev = APR_RING_FIRST(lst);
    if (rev == APR_RING_SENTINEL(lst, revision_t, link)) {
        return NULL;
    }

    it = apr_pcalloc(pool, sizeof(revision_iter_t));
    it->lst = lst;
    it->rev = rev;

    return it;
}

static revision_iter_t *
revision_iter_next(revision_iter_t *it)
{
    const revision_t *rev = APR_RING_NEXT(it->rev, link);
    if (rev == APR_RING_SENTINEL(it->lst, revision_t, link)) {
        return NULL;
    }

    it->rev = rev;

    return it;
}

svn_revnum_t
revision_revnum_get(const revision_t *rev)
{
    return rev->revnum;
}

commit_t *
revision_commits_get(const revision_t *rev, const branch_t *b)
{
    return apr_hash_get(rev->commits, b, sizeof(branch_t *));
}

commit_t *
revision_commits_add(const revision_t *rev, branch_t *b)
{
    apr_pool_t *pool = apr_hash_pool_get(rev->commits);
    commit_t *commit = commit_create(pool);
    commit_parent_set(commit, branch_head_get(b));

    apr_hash_set(rev->commits, b, sizeof(branch_t *), commit);

    return commit;
}

size_t
revision_commits_count(const revision_t *rev)
{
    return apr_hash_count(rev->commits);
}

svn_error_t *
revision_commits_apply(const revision_t *rev,
                       revision_commit_handler_t apply,
                       void *ctx,
                       apr_pool_t *pool)
{
    apr_hash_index_t *idx;
    ssize_t keylen = sizeof(branch_t *);

    for (idx = apr_hash_first(pool, rev->commits); idx; idx = apr_hash_next(idx)) {
        const void *key;
        commit_t *commit;
        branch_t *branch;
        void *value;

        apr_hash_this(idx, &key, &keylen, &value);
        branch = (branch_t *) key;
        commit = value;

        SVN_ERR(apply(ctx, branch, commit, pool));
    }

    return SVN_NO_ERROR;
}

void
revision_removes_add(const revision_t *rev, const branch_t *b)
{
    apr_hash_set(rev->removes, b, sizeof(branch_t *), NOT_NULL);
}

size_t
revision_removes_count(const revision_t *rev)
{
    return apr_hash_count(rev->removes);
}

svn_error_t *
revision_removes_apply(const revision_t *rev,
                       revision_remove_handler_t apply,
                       void *ctx,
                       apr_pool_t *pool)
{
    apr_hash_index_t *idx;
    ssize_t keylen = sizeof(branch_t *);
    branch_t *branch;

    for (idx = apr_hash_first(pool, rev->removes); idx; idx = apr_hash_next(idx)) {
        const void *key;

        apr_hash_this(idx, &key, &keylen, NULL);
        branch = (branch_t *) key;

        SVN_ERR(apply(ctx, branch, pool));
    }

    return SVN_NO_ERROR;
}

// revision_storage_t implementation
struct revision_storage_t
{
    apr_pool_t *pool;
    revision_list_t *revisions;
    apr_hash_t *revnum_idx;
};

revision_storage_t *
revision_storage_create(apr_pool_t *pool)
{
    revision_storage_t *rs = apr_pcalloc(pool, sizeof(revision_storage_t));
    rs->pool = pool;
    rs->revisions = apr_pcalloc(pool, sizeof(revision_list_t));
    APR_RING_INIT(rs->revisions, revision_t, link);
    rs->revnum_idx = apr_hash_make(pool);

    return rs;
}

revision_t *
revision_storage_add_revision(revision_storage_t *rs, svn_revnum_t revnum)
{
    revision_t *rev = apr_pcalloc(rs->pool, sizeof(revision_t));
    rev->revnum = revnum;
    rev->commits = apr_hash_make(rs->pool);
    rev->removes = apr_hash_make(rs->pool);

    APR_RING_INSERT_TAIL(rs->revisions, rev, revision_t, link);
    apr_hash_set(rs->revnum_idx, &revnum, sizeof(svn_revnum_t), rev);

    return rev;
}

const revision_t *
revision_storage_get_by_revnum(revision_storage_t *rs, svn_revnum_t revnum)
{
    return apr_hash_get(rs->revnum_idx, &revnum, sizeof(svn_revnum_t));
}

static svn_error_t *
dump_commit(void *ctx, branch_t *branch, commit_t *commit, apr_pool_t *pool)
{
    svn_stream_t *dst = ctx;

    SVN_ERR(svn_stream_printf(dst, pool, "%s :%d\n",
                              branch_refname_get(branch),
                              commit_mark_get(commit)));

    return SVN_NO_ERROR;
}

svn_error_t *
revision_storage_dump(const revision_storage_t *rs,
                      svn_stream_t *dst,
                      apr_pool_t *pool)
{
    revision_iter_t *it;

    for (it = revision_iter_first(rs->revisions, pool); it; it = revision_iter_next(it)) {
        const revision_t *rev = it->rev;
        SVN_ERR(svn_stream_printf(dst, pool, "r%ld %d\n",
                                  rev->revnum,
                                  apr_hash_count(rev->commits)));

        SVN_ERR(revision_commits_apply(rev, dump_commit, dst, pool));
    }

    return SVN_NO_ERROR;
}
