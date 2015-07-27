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

#include "node.h"
#include <svn_hash.h>
#include <svn_props.h>

svn_error_t *
set_node_mode(node_mode_t *mode,
              svn_fs_root_t *root,
              const char *path,
              apr_pool_t *pool)
{
    apr_hash_t *props;
    svn_node_kind_t kind;

    SVN_ERR(svn_fs_check_path(&kind, root, path, pool));

    if (kind == svn_node_dir) {
        *mode = MODE_DIR;
        return SVN_NO_ERROR;
    }

    SVN_ERR(svn_fs_node_proplist(&props, root, path, pool));

    if (svn_hash_gets(props, SVN_PROP_EXECUTABLE)) {
        *mode = MODE_EXECUTABLE;
    } else if (svn_hash_gets(props, SVN_PROP_SPECIAL)) {
        *mode = MODE_SYMLINK;
    } else {
        *mode = MODE_NORMAL;
    }

    return SVN_NO_ERROR;
}
