#!/bin/sh
#
# Copyright (C) 2014 by Maxim Bublis <b@codemonkey.ru>
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

test_description='Test plain history support'

. ./helpers.sh
. ./lib/test.sh

test_export_import() {
	test_expect_success 'Export Subversion repository' 'svnadmin dump repo >repo.dump'
	test_expect_success 'Import dump into Git' '(cd repo.git && git-svn-fast-import <../repo.dump)'
}

test_expect_success 'Initialize Subversion repository' '
svnadmin create repo &&
	svn checkout file:///$(pwd)/repo repo.svn
'

cat >repo/hooks/pre-revprop-change <<EOF
#!/bin/sh
exit 0
EOF

chmod +x repo/hooks/pre-revprop-change

test_expect_success 'Initialize Git repository' '
git init repo.git
'

cat >repo.svn/main.c <<EOF
int main() {
	return 0;
}
EOF

test_tick

test_expect_success 'Commit new file' '
(cd repo.svn &&
	svn add main.c &&
	svn commit -m "Initial revision" &&
	svn propset svn:date --revprop -r HEAD $COMMIT_DATE)
'

test_export_import

cat >expect <<EOF
a45b1471385a5361a222215c9381435eab089293
:000000 100644 0000000000000000000000000000000000000000 cb3f7482fa46d2ac25648a694127f23c1976b696 A	main.c
EOF

test_expect_success 'Validate files added' '
(cd repo.git &&
	git diff-tree -M -r --root master >actual &&
	test_cmp ../expect actual)
'

cat >repo.svn/main.c <<EOF
#include <stdio.h>

int main() {
	printf("Hello, world\n");
	return 0;
}
EOF

test_tick

test_expect_success 'Commit file modification' '
(cd repo.svn &&
	svn commit -m "Some modification" &&
	svn propset svn:date --revprop -r HEAD $COMMIT_DATE)
'

test_export_import

test_done
