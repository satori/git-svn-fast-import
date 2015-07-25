#!/bin/sh
#
# Copyright (C) 2014-2015 by Maxim Bublis <b@codemonkey.ru>
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
# LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
# OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
# WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

test_description='Test basic support'

. ./helpers.sh
. ./sharness/sharness.sh

init_repos

test_expect_success 'Import dump into Git' '
(cd repo.git &&
	git-svn-fast-import ../repo)
'

cat > authors.txt <<EOF
author1 = A U Thor <author@example.com>
EOF

export_import() {
	(cd repo.git &&
		git-svn-fast-import --quiet --export-marks ../marks.txt --authors-file ../authors.txt ../repo)
}

test_expect_success 'Import dump using authors mapping' '
export_import
'

cat > authors.txt <<EOF
A U Thor <author@example.com>
EOF

test_expect_success 'Failure on malformed authors mapping' '
test_must_fail export_import
'

cat > authors.txt <<EOF
 = A U Thor <author@example.com>
EOF

test_expect_success 'Failure on malformed authors mapping' '
test_must_fail export_import
'


cat > authors.txt <<EOF
author1 = <author@example.com>
EOF

test_expect_success 'Failure on malformed authors mapping' '
test_must_fail export_import
'

cat > authors.txt <<EOF
author1 = A U Thor
EOF

test_expect_success 'Failure on malformed authors mapping' '
test_must_fail export_import
'

cat > authors.txt <<EOF
author1 = A U Thor <>
EOF

test_expect_success 'Failure on malformed authors mapping' '
test_must_fail export_import
'

cat > authors.txt <<EOF
# some comment

author1 = A U Thor <author1@example.com>
EOF

test_expect_success 'Skip comments and empty lines in authors mapping' '
export_import
'

test_done
