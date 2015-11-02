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

#include "checksum.h"
#include "node.h"
#include "options.h"
#include "tree.h"
#include <svn_cmdline.h>
#include <svn_dirent_uri.h>
#include <svn_hash.h>
#include <svn_path.h>
#include <svn_pools.h>
#include <svn_repos.h>
#include <svn_utf.h>

static struct apr_getopt_option_t cmdline_options[] = {
    {"help", 'h', 0, "Print this message and exit"},
    {NULL, 'd', 0, "Show only the named tree entry itself, not its children"},
    {NULL, 'r', 0, "Recurse into sub-trees"},
    {NULL, 't', 0, "Show tree entries even when going to recurse them. Has no effect if -r was not passed. -d implies -t."},
    {"root", 'R', 1, "Set path as root directory."},
    {"ignore-path", 'I', 1, "Ignore path relative to root."},
    {"checksum-cache", 'c', 1, "Use checksum cache."},
    {0, 0, 0, 0}
};

static svn_error_t *
print_entries(apr_array_header_t *entries,
              const char *root_path,
              svn_boolean_t trees_only,
              svn_boolean_t recurse,
              svn_boolean_t show_trees,
              apr_pool_t *pool)
{
    for (int i = 0; i < entries->nelts; i++) {
        node_t *node = &APR_ARRAY_IDX(entries, i, node_t);

        if (node->kind == svn_node_dir) {
            // Skip empty directories.
            if (node->entries->nelts == 0) {
                continue;
            }
            if (show_trees) {
                SVN_ERR(svn_cmdline_printf(pool, "%06o tree %s\t%s\n",
                                           node->mode,
                                           svn_checksum_to_cstring_display(node->checksum, pool),
                                           node->path));
            }
            if (recurse) {
                SVN_ERR(print_entries(node->entries, root_path, trees_only,
                                      recurse, show_trees, pool));
            }
        } else if (!trees_only) {
            SVN_ERR(svn_cmdline_printf(pool, "%06o blob %s\t%s\n",
                                       node->mode,
                                       svn_checksum_to_cstring_display(node->checksum, pool),
                                       node->path));
        }
    }

    return SVN_NO_ERROR;
}

static svn_error_t *
print_tree(svn_fs_root_t *root,
           const char *root_path,
           const char *path,
           svn_boolean_t trees_only,
           svn_boolean_t recurse,
           svn_boolean_t show_trees,
           checksum_cache_t *cache,
           tree_t *ignores,
           apr_pool_t *pool)
{
    apr_array_header_t *entries;
    const char *abspath = svn_relpath_join(root_path, path, pool);
    svn_boolean_t from_cache;
    svn_checksum_t *checksum;
    svn_stream_t *dummy = svn_stream_empty(pool);

    SVN_ERR(set_tree_checksum(&checksum, &from_cache, &entries,
                              dummy, cache, root, abspath,
                              root_path, NULL, ignores,
                              pool, pool));
    SVN_ERR(print_entries(entries, root_path, trees_only, recurse, show_trees, pool));

    return SVN_NO_ERROR;
}

static svn_error_t *
do_main(int *exit_code, int argc, const char **argv, apr_pool_t *pool)
{
    apr_array_header_t *relative_ignores;
    tree_t *ignores;
    checksum_cache_t *cache;
    apr_getopt_t *opt_parser;
    apr_status_t apr_err;
    const char *path = NULL, *repo_path = NULL, *root_path = "";
    const char *checksum_cache_path = NULL;
    svn_boolean_t trees_only = FALSE, recurse = FALSE, show_trees = FALSE;
    svn_fs_t *fs;
    svn_fs_root_t *root;
    svn_repos_t *repo;
    svn_revnum_t revnum = SVN_INVALID_REVNUM;

    relative_ignores = apr_array_make(pool, 0, sizeof(const char *));
    ignores = tree_create(pool);
    cache = checksum_cache_create(pool);

    // Initialize the FS library.
    SVN_ERR(svn_fs_initialize(pool));

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
        case 'd':
            trees_only = TRUE;
            break;
        case 'r':
            recurse = TRUE;
            break;
        case 't':
            show_trees = TRUE;
            break;
        case 'R':
            root_path = opt_arg;
            break;
        case 'I':
            APR_ARRAY_PUSH(relative_ignores, const char *) = opt_arg;
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

    for (int i = 0; i < relative_ignores->nelts; i++) {
        const char *ignored_path = APR_ARRAY_IDX(relative_ignores, i, const char *);
        ignored_path = svn_relpath_join(root_path, ignored_path, pool);
        tree_insert(ignores, ignored_path, ignored_path, pool);
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
    } else if (svn_path_is_url(repo_path)) {
        svn_error_clear(svn_cmdline_fprintf(stderr, pool, "'%s' is an URL when it should be path\n", repo_path));
        *exit_code = EXIT_FAILURE;
        return SVN_NO_ERROR;
    }

    SVN_ERR(svn_repos_open3(&repo, repo_path, NULL, pool, pool));
    fs = svn_repos_fs(repo);

    // Get the revision.
    if (opt_parser->ind < opt_parser->argc) {
        const char *opt_arg = opt_parser->argv[opt_parser->ind++];
        if (strcmp(opt_arg, "HEAD") == 0) {
            SVN_ERR(svn_fs_youngest_rev(&revnum, fs, pool));
        } else {
            char *end = NULL;
            revnum = strtol(opt_arg, &end, 10);
            if (!SVN_IS_VALID_REVNUM(revnum) || !end || *end) {
                return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR,
                                        NULL,
                                        "Invalid revision number supplied");
            }
        }
    }

    SVN_ERR(svn_fs_revision_root(&root, fs, revnum, pool));

    // Get the path.
    if (opt_parser->ind < opt_parser->argc) {
        SVN_ERR(svn_utf_cstring_to_utf8(&path, opt_parser->argv[opt_parser->ind++], pool));
        path = svn_dirent_internal_style(path, pool);
    }

    // If the path is not provided start with root directory.
    if (path == NULL) {
        path = "";
    }

    if (checksum_cache_path != NULL) {
        SVN_ERR(checksum_cache_load_path(cache, checksum_cache_path, pool));
    }

    SVN_ERR(print_tree(root, root_path, path, trees_only, recurse,
                       (!recurse || trees_only || show_trees),
                       cache, ignores, pool));

    return SVN_NO_ERROR;
}

int
main(int argc, const char **argv)
{
    apr_pool_t *pool;
    svn_error_t *err;
    int exit_code = EXIT_SUCCESS;

    // Initialize the app.
    if (svn_cmdline_init("svn-ls-tree", stderr) != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    // Create top-level pool. Use a separate mutexless allocator,
    // as this app is single-threaded.
    pool = apr_allocator_owner_get(svn_pool_create_allocator(FALSE));

    err = do_main(&exit_code, argc, argv, pool);
    if (err) {
        exit_code = EXIT_FAILURE;
        svn_cmdline_handle_exit_error(err, NULL, "svn-ls-tree: ");
    }

    svn_pool_destroy(pool);

    return exit_code;
}
