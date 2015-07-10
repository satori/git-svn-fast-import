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

#include "checksum.h"
#include "export.h"
#include "options.h"
#include "tree.h"
#include "types.h"
#include <svn_cmdline.h>
#include <svn_dirent_uri.h>
#include <svn_opt.h>
#include <svn_pools.h>
#include <svn_repos.h>
#include <svn_utf.h>

static struct apr_getopt_option_t cmdline_options[] = {
    {"help", 'h', 0, "Print this message and exit"},
    {"revision", 'r', 1, ""},
    {"stdlayout", 's', 0, ""},
    {"trunk", 'T', 1, ""},
    {"tags", 't', 1, ""},
    {"branches", 'b', 1, ""},
    {"ignore-path", 'I', 1, ""},
    {"authors-file", 'A', 1, ""},
    {"export-rev-marks", 'e', 1, ""},
    {"checksum-cache", 'c', 1, "Use checksum cache."},
    {0, 0, 0, 0}
};

// Set revnum to revision specified by revision arg (or to
// SVN_INVALID_REVNUM if that has the type 'unspecified'),
// possibly making use of the youngest revision number in repos.
static svn_error_t *
get_revnum(svn_revnum_t *revnum,
           const svn_opt_revision_t *revision,
           svn_revnum_t youngest,
           svn_repos_t *repos,
           apr_pool_t *pool)
{
    if (revision->kind == svn_opt_revision_number) {
        *revnum = revision->value.number;
    } else if (revision->kind == svn_opt_revision_head) {
        *revnum = youngest;
    } else if (revision->kind == svn_opt_revision_date) {
        SVN_ERR(svn_repos_dated_revision(revnum,
                                         repos,
                                         revision->value.date,
                                         pool));
    } else if (revision->kind == svn_opt_revision_unspecified) {
        *revnum = SVN_INVALID_REVNUM;
    } else {
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                "Invalid revision specifier");
    }

    if (*revnum > youngest) {
        return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 "Revisions must not be greater than the youngest revision (%ld)",
                                 youngest);
    }

    return SVN_NO_ERROR;
}

static svn_error_t *
load_authors(author_storage_t *authors,
             const char *path,
             apr_pool_t *pool)
{
    svn_stream_t *input;
    svn_error_t *err;

    SVN_ERR(svn_stream_open_readonly(&input, path, pool, pool));
    err = author_storage_load(authors, input, pool);
    if (err) {
        return svn_error_quick_wrap(err, "Malformed authors file");
    }
    SVN_ERR(svn_stream_close(input));

    return SVN_NO_ERROR;
}

static svn_error_t *
dump_marks(revision_storage_t *revisions,
           const char *path,
           apr_pool_t *pool)
{
    apr_file_t *fd;
    svn_stream_t *output;

    SVN_ERR(svn_io_file_open(&fd, path,
                             APR_CREATE | APR_TRUNCATE | APR_BUFFERED | APR_WRITE,
                             APR_OS_DEFAULT, pool));

    output = svn_stream_from_aprfile2(fd, FALSE, pool);
    SVN_ERR(revision_storage_dump(revisions, output, pool));
    SVN_ERR(svn_stream_close(output));

    return SVN_NO_ERROR;
}

static svn_error_t *
dump_checksum_cache(checksum_cache_t *cache,
                    const char *path,
                    apr_pool_t *pool)
{
    apr_file_t *fd;
    svn_stream_t *output;

    SVN_ERR(svn_io_file_open(&fd, path,
                             APR_CREATE | APR_TRUNCATE | APR_BUFFERED | APR_WRITE,
                             APR_OS_DEFAULT, pool));

    output = svn_stream_from_aprfile2(fd, FALSE, pool);
    SVN_ERR(checksum_cache_dump(cache, output, pool));
    SVN_ERR(svn_stream_close(output));

    return SVN_NO_ERROR;
}

static svn_error_t *
do_main(int *exit_code, int argc, const char **argv, apr_pool_t *pool)
{
    apr_getopt_t *opt_parser;
    apr_status_t apr_err;
    const char *repo_path = NULL;
    svn_opt_revision_t start_revision, end_revision;
    svn_fs_t *fs;
    svn_stream_t *output;
    svn_revnum_t lower = SVN_INVALID_REVNUM, upper = SVN_INVALID_REVNUM;
    svn_revnum_t youngest;
    svn_repos_t *repo;

    // Trunk path prefix;
    const char *trunk_path = "";
    // Branches and tags prefixes.
    tree_t *branches_pfx = tree_create(pool);
    tree_t *tags_pfx = tree_create(pool);
    // Ignore path prefixes.
    tree_t *ignores = tree_create(pool);
    // Path to a file containing mapping of
    // Subversion committers to Git authors.
    const char *authors_path = NULL;
    // Path to a file where marks should be exported.
    const char *export_marks_path = NULL;
    const char *checksum_cache_path = NULL;

    // Author storage.
    author_storage_t *authors;
    // Branch storage.
    branch_storage_t *branches;
    // Revision storage.
    revision_storage_t *revisions;
    // Checksum cache.
    checksum_cache_t *cache;

    // Initialize the FS library.
    SVN_ERR(svn_fs_initialize(pool));

    start_revision.kind = svn_opt_revision_unspecified;
    end_revision.kind = svn_opt_revision_unspecified;

    // Parse options.
    apr_err = apr_getopt_init(&opt_parser, pool, argc, argv);
    if (apr_err != APR_SUCCESS) {
        return svn_error_wrap_apr(apr_err, NULL);
    }

    while (TRUE) {
        int opt_id;
        const char *opt_arg;

        // Parse the next option.
        apr_err = apr_getopt_long(opt_parser, cmdline_options, &opt_id, &opt_arg);
        if (APR_STATUS_IS_EOF(apr_err)) {
            break;
        } else if (apr_err) {
            return svn_error_wrap_apr(apr_err, NULL);
        }

        switch (opt_id) {
        case 'r':
            if (start_revision.kind != svn_opt_revision_unspecified) {
                return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                        "Multiple revision arguments encountered");
            }
            if (svn_opt_parse_revision(&start_revision, &end_revision, opt_arg, pool) != 0) {
                return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                        "Syntax error in revision argument");
            }
            break;
        case 's':
            trunk_path = "trunk";
            tree_insert(branches_pfx, "branches", "branches");
            tree_insert(tags_pfx, "tags", "tags");
            break;
        case 'T':
            trunk_path = opt_arg;
            break;
        case 't':
            tree_insert(tags_pfx, opt_arg, opt_arg);
            break;
        case 'b':
            tree_insert(branches_pfx, opt_arg, opt_arg);
            break;
        case 'I':
            tree_insert(ignores, opt_arg, opt_arg);
            break;
        case 'A':
            authors_path = opt_arg;
            break;
        case 'e':
            export_marks_path = opt_arg;
            break;
        case 'c':
            checksum_cache_path = opt_arg;
            break;
        case 'h':
            print_usage(cmdline_options, pool);
            *exit_code = EXIT_FAILURE;
            return SVN_NO_ERROR;
        }
    }

    // Get the repository path.
    if (opt_parser->ind < opt_parser->argc) {
        SVN_ERR(svn_utf_cstring_to_utf8(&repo_path, opt_parser->argv[opt_parser->ind++], pool));
        repo_path = svn_dirent_internal_style(repo_path, pool);
    }

    if (repo_path == NULL) {
        svn_error_clear(svn_cmdline_fprintf(stderr, pool, "Repository path required\n"));
        *exit_code = EXIT_FAILURE;
        return SVN_NO_ERROR;
    }

    SVN_ERR(svn_repos_open2(&repo, repo_path, NULL, pool));
    fs = svn_repos_fs(repo);

    SVN_ERR(svn_fs_youngest_rev(&youngest, fs, pool));

    // Find the revision numbers at which to start and end.
    SVN_ERR(get_revnum(&lower, &start_revision, youngest, repo, pool));
    SVN_ERR(get_revnum(&upper, &end_revision, youngest, repo, pool));

    // Fill in implied revisions if necessary
    if (lower == SVN_INVALID_REVNUM) {
        lower = 0;
        upper = youngest;
    } else if (upper == SVN_INVALID_REVNUM) {
        upper = lower;
    }

    if (lower > upper) {
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                "First revision cannot be higher than second");
    }

    authors = author_storage_create(pool);
    branches = branch_storage_create(pool, branches_pfx, tags_pfx);
    revisions = revision_storage_create(pool);
    cache = checksum_cache_create(pool);

    if (authors_path != NULL) {
        SVN_ERR(load_authors(authors, authors_path, pool));
    }

    if (checksum_cache_path != NULL) {
        SVN_ERR(checksum_cache_load_path(cache, checksum_cache_path, pool));
    }

    branch_storage_add_branch(branches, "refs/heads/master", trunk_path);

    SVN_ERR(svn_stream_for_stdout(&output, pool));

    SVN_ERR(export_revision_range(output, fs, lower, upper, branches, revisions, authors, cache, ignores, pool));

    if (export_marks_path != NULL) {
        SVN_ERR(dump_marks(revisions, export_marks_path, pool));
    }

    if (checksum_cache_path != NULL) {
        SVN_ERR(dump_checksum_cache(cache, checksum_cache_path, pool));
    }

    return SVN_NO_ERROR;
}

int
main(int argc, const char **argv)
{
    apr_pool_t *pool;
    svn_error_t *err;
    int exit_code = EXIT_SUCCESS;

    // Initialize the app.
    if (svn_cmdline_init("svn-fast-export", stderr) != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    // Create top-level pool. Use a separate mutexless allocator,
    // as this app is single-threaded.
    pool = apr_allocator_owner_get(svn_pool_create_allocator(FALSE));

    err = do_main(&exit_code, argc, argv, pool);
    if (err) {
        exit_code = EXIT_FAILURE;
        svn_cmdline_handle_exit_error(err, NULL, "svn-fast-export: ");
    }

    svn_pool_destroy(pool);

    return exit_code;
}
