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

#include "options.h"
#include "sorts.h"
#include <svn_cmdline.h>
#include <svn_dirent_uri.h>
#include <svn_fs.h>
#include <svn_hash.h>
#include <svn_path.h>
#include <svn_pools.h>
#include <svn_props.h>
#include <svn_repos.h>
#include <svn_utf.h>

#define SYMLINK_CONTENT_PREFIX "link"

typedef enum
{
    MODE_NORMAL     = 0100644,
    MODE_EXECUTABLE = 0100755,
    MODE_SYMLINK    = 0120000,
    MODE_DIR        = 0040000
} node_mode_t;

typedef enum
{
    NODE_TREE,
    NODE_BLOB
} node_kind_t;

typedef struct
{
    node_mode_t mode;
    node_kind_t kind;
    const svn_checksum_t *checksum;
    svn_filesize_t size;
    const char *path;
    const char *basename;
} entry_t;

static struct apr_getopt_option_t cmdline_options[] = {
    {"help", 'h', 0, "Print this message and exit"},
    {NULL, 'r', 0, "Recurse into sub-trees"},
    {0, 0, 0, 0}
};

static svn_error_t *
calculate_blob_checksum(svn_checksum_t **checksum,
                        svn_fs_root_t *root,
                        const char *path,
                        apr_pool_t *pool)
{
    const char *hdr;
    apr_hash_t *props;
    svn_checksum_ctx_t *ctx;
    svn_filesize_t size;
    svn_stream_t *content;

    SVN_ERR(svn_fs_node_proplist(&props, root, path, pool));
    SVN_ERR(svn_fs_file_length(&size, root, path, pool));
    SVN_ERR(svn_fs_file_contents(&content, root, path, pool));

    // We need to skip a symlink marker from the beginning of a content
    // and subtract a symlink marker length from the blob size.
    if (svn_hash_gets(props, SVN_PROP_SPECIAL)) {
        apr_size_t bufsize = sizeof(SYMLINK_CONTENT_PREFIX);
        char buf[bufsize];
        SVN_ERR(svn_stream_read(content, buf, &bufsize));
        size -= bufsize;
    }

    hdr = apr_psprintf(pool, "blob %ld", size);
    ctx = svn_checksum_ctx_create(svn_checksum_sha1, pool);

    SVN_ERR(svn_checksum_update(ctx, hdr, strlen(hdr) + 1));

    while (size > 0) {
        apr_size_t bufsize = 1024;
        char buf[bufsize];

        SVN_ERR(svn_stream_read(content, buf, &bufsize));
        SVN_ERR(svn_checksum_update(ctx, buf, bufsize));

        size -= bufsize;
    }

    SVN_ERR(svn_checksum_final(checksum, ctx, pool));

    return SVN_NO_ERROR;
}

static svn_error_t *
calculate_tree_checksum(svn_checksum_t **checksum,
                        apr_array_header_t *entries,
                        apr_pool_t *pool)
{
    const char *hdr;
    svn_checksum_ctx_t *ctx;
    svn_stringbuf_t *buf;
    ctx = svn_checksum_ctx_create(svn_checksum_sha1, pool);
    buf = svn_stringbuf_create_empty(pool);

    for (int i = 0; i < entries->nelts; i++) {
        entry_t *entry = &APR_ARRAY_IDX(entries, i, entry_t);
        const char *s = apr_psprintf(pool, "%o %s",
                                     entry->mode,
                                     entry->basename);

        svn_stringbuf_appendbytes(buf, s, strlen(s) + 1);
        svn_stringbuf_appendbytes(buf, (const char *)entry->checksum->digest,
                                  svn_checksum_size(entry->checksum));
    }

    hdr = apr_psprintf(pool, "tree %ld", buf->len);

    SVN_ERR(svn_checksum_update(ctx, hdr, strlen(hdr) + 1));
    SVN_ERR(svn_checksum_update(ctx, buf->data, buf->len));
    SVN_ERR(svn_checksum_final(checksum, ctx, pool));

    return SVN_NO_ERROR;
}

static svn_error_t *
traverse_tree(apr_array_header_t **entries,
              svn_fs_root_t *root,
              const char *path,
              svn_boolean_t recurse,
              apr_pool_t *pool)
{
    apr_array_header_t *sorted_entries, *result;
    apr_hash_t *dir_entries;
    result = apr_array_make(pool, 0, sizeof(entry_t));

    SVN_ERR(svn_fs_dir_entries(&dir_entries, root, path, pool));

    sorted_entries = svn_sort__hash(dir_entries,
                                    svn_sort_compare_items_gitlike,
                                    pool);

    for (int i = 0; i < sorted_entries->nelts; i++) {
        svn_sort__item_t item = APR_ARRAY_IDX(sorted_entries, i,
                                              svn_sort__item_t);

        svn_checksum_t *checksum;
        svn_fs_dirent_t *e = item.value;
        const char *fname = svn_relpath_join(path, e->name, pool);

        if (e->kind == svn_node_dir) {
            apr_array_header_t *subentries;
            SVN_ERR(traverse_tree(&subentries, root,
                                  svn_relpath_join(path, e->name, pool),
                                  recurse, pool));

            SVN_ERR(calculate_tree_checksum(&checksum, subentries, pool));

            if (recurse) {
                result = apr_array_append(pool, result, subentries);
            } else {
                entry_t *entry = apr_array_push(result);
                entry->mode = MODE_DIR;
                entry->kind = NODE_TREE;
                entry->checksum = checksum;
                entry->size = 0;
                entry->path = fname;
                entry->basename = e->name;
            }
        } else {
            entry_t *entry = apr_array_push(result);
            apr_hash_t *props;
            entry->mode = MODE_NORMAL;
            entry->kind = NODE_BLOB;

            SVN_ERR(svn_fs_node_proplist(&props, root, fname, pool));
            if (svn_hash_gets(props, SVN_PROP_EXECUTABLE) != NULL) {
                entry->mode = MODE_EXECUTABLE;
            }

            if (svn_hash_gets(props, SVN_PROP_SPECIAL)) {
                entry->mode = MODE_SYMLINK;
            }

            SVN_ERR(calculate_blob_checksum(&checksum, root, fname, pool));

            SVN_ERR(svn_fs_file_length(&entry->size, root, fname, pool));
            entry->checksum = checksum;
            entry->path = fname;
            entry->basename = e->name;
        }
    }

    *entries = result;

    return SVN_NO_ERROR;
}

static svn_error_t *
print_dir(entry_t *entry, apr_pool_t *pool)
{
    SVN_ERR(svn_cmdline_printf(pool, "%06o tree %s\t%s\n",
                               entry->mode,
                               svn_checksum_to_cstring_display(entry->checksum, pool),
                               entry->path));

    return SVN_NO_ERROR;
}

static svn_error_t *
print_blob(entry_t *entry, apr_pool_t *pool)
{
    SVN_ERR(svn_cmdline_printf(pool, "%06o blob %s\t%s\n",
                               entry->mode,
                               svn_checksum_to_cstring_display(entry->checksum, pool),
                               entry->path));

    return SVN_NO_ERROR;
}

static svn_error_t *
print_entry(entry_t *entry, apr_pool_t *pool)
{
    switch (entry->kind) {
    case NODE_TREE:
        SVN_ERR(print_dir(entry, pool));
        break;
    case NODE_BLOB:
        SVN_ERR(print_blob(entry, pool));
        break;
    }

    return SVN_NO_ERROR;
}

static svn_error_t *
print_tree(svn_fs_root_t *root,
           const char *path,
           svn_boolean_t recurse,
           apr_pool_t *pool)
{
    apr_array_header_t *entries;

    SVN_ERR(traverse_tree(&entries, root, path, recurse, pool));

    for (int i = 0; i < entries->nelts; i++) {
        entry_t *entry = &APR_ARRAY_IDX(entries, i, entry_t);
        SVN_ERR(print_entry(entry, pool));
    }

    return SVN_NO_ERROR;
}

static svn_error_t *
do_main(int *exit_code, int argc, const char **argv, apr_pool_t *pool)
{
    apr_getopt_t *opt_parser;
    apr_status_t apr_err;
    const char *repo_path = NULL;
    const char *path = NULL;
    svn_boolean_t recurse = FALSE;
    svn_fs_t *fs;
    svn_fs_root_t *root;
    svn_repos_t *repo;
    svn_revnum_t rev;

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
        case 'r':
            recurse = TRUE;
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
    } else if (svn_path_is_url(repo_path)) {
        svn_error_clear(svn_cmdline_fprintf(stderr, pool, "'%s' is an URL when it should be path\n", repo_path));
        *exit_code = EXIT_FAILURE;
        return SVN_NO_ERROR;
    }

    // Get the path.
    if (opt_parser->ind < opt_parser->argc) {
        SVN_ERR(svn_utf_cstring_to_utf8(&path, opt_parser->argv[opt_parser->ind++], pool));
        path = svn_dirent_internal_style(path, pool);
    }

    // If the path is not provided start with root directory.
    if (path == NULL) {
        path = "";
    }

    SVN_ERR(svn_repos_open2(&repo, repo_path, NULL, pool));

    fs = svn_repos_fs(repo);

    SVN_ERR(svn_fs_youngest_rev(&rev, fs, pool));
    SVN_ERR(svn_fs_revision_root(&root, fs, rev, pool));
    SVN_ERR(print_tree(root, path, recurse, pool));

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
