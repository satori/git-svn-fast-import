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
};

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

        SVN_ERR(apply(ctx, branch, commit));
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

        SVN_ERR(apply(ctx, branch));
    }

    return SVN_NO_ERROR;
}

// revision_storage_t implementation
struct revision_storage_t
{
    apr_pool_t *pool;
    apr_hash_t *revisions;
};

revision_storage_t *
revision_storage_create(apr_pool_t *pool)
{
    revision_storage_t *rs = apr_pcalloc(pool, sizeof(revision_storage_t));
    rs->pool = pool;
    rs->revisions = apr_hash_make(pool);

    return rs;
}

revision_t *
revision_storage_add_revision(revision_storage_t *rs, svn_revnum_t revnum)
{
    revision_t *rev = apr_pcalloc(rs->pool, sizeof(revision_t));
    rev->revnum = revnum;
    rev->commits = apr_hash_make(rs->pool);
    rev->removes = apr_hash_make(rs->pool);

    return rev;
}

const revision_t *
revision_storage_get_by_revnum(revision_storage_t *rs, svn_revnum_t revnum)
{
    return apr_hash_get(rs->revisions, &revnum, sizeof(svn_revnum_t));
}
