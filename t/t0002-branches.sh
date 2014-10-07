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

test_description='Test branch history support'

. ./helpers.sh
. ./lib/test.sh

test_export_import() {
	test_expect_success 'Import dump into Git' '
	svnadmin dump repo >repo.dump &&
		(cd repo.git && git-svn-fast-import --stdlayout <../repo.dump)
	'
}

test_expect_success 'Initialize repositories' '
svnadmin create repo &&
	echo "#!/bin/sh" >repo/hooks/pre-revprop-change &&
	chmod +x repo/hooks/pre-revprop-change &&
	svn checkout file:///$(pwd)/repo repo.svn &&
	git init repo.git
'

test_tick

test_expect_success 'Commit standard directories layout' '
(cd repo.svn &&
	mkdir -p branches tags trunk &&
	svn add branches tags trunk &&
	svn commit -m "Standard project directories initialized." &&
	svn propset svn:date --revprop -r HEAD $COMMIT_DATE &&
	svn propset svn:author --revprop -r HEAD author1)
'

test_export_import

cat >repo.svn/trunk/main.c <<EOF
int main() {
	return 0;
}
EOF

test_tick

test_expect_success 'Commit new file into trunk' '
(cd repo.svn &&
	svn add trunk/main.c &&
	svn commit -m "Initial revision" &&
	svn propset svn:date --revprop -r HEAD $COMMIT_DATE &&
	svn propset svn:author --revprop -r HEAD author1)
'

test_export_import

cat >expect <<EOF
:000000 100644 0000000000000000000000000000000000000000 cb3f7482fa46d2ac25648a694127f23c1976b696 A	main.c
EOF

test_expect_success 'Validate files added' '
(cd repo.git &&
	git diff-tree -M -r master^ master >actual &&
	test_cmp ../expect actual)
'

cat >repo.svn/trunk/main.c <<EOF
#include <stdio.h>

int main() {
	printf("Hello, world\n");
	return 0;
}
EOF

test_tick

test_expect_success 'Commit file modify into trunk' '
(cd repo.svn &&
	svn commit -m "Modify file" &&
	svn propset svn:date --revprop -r HEAD $COMMIT_DATE &&
	svn propset svn:author --revprop -r HEAD author1)
'

test_export_import

cat >expect <<EOF
:100644 100644 cb3f7482fa46d2ac25648a694127f23c1976b696 0e5f181f94f2ff9f984b4807887c4d2c6f642723 M	main.c
EOF

test_expect_success 'Validate file modify' '
(cd repo.git &&
	git diff-tree master^ master >actual &&
	test_cmp ../expect actual)
'

test_tick

test_expect_success 'Commit file mode executable' '
(cd repo.svn &&
	svn propset svn:executable on trunk/main.c &&
	svn commit -m "Change mode to executable" &&
	svn propset svn:date --revprop -r HEAD $COMMIT_DATE &&
	svn propset svn:author --revprop -r HEAD author1)
'

test_export_import

cat >expect <<EOF
:100644 100755 0e5f181f94f2ff9f984b4807887c4d2c6f642723 0e5f181f94f2ff9f984b4807887c4d2c6f642723 M	main.c
EOF

test_expect_failure 'Validate file mode modify' '
(cd repo.git &&
	git diff-tree master^ master >actual &&
	test_cmp ../expect actual)
'

test_tick

test_expect_success 'Commit file mode normal' '
(cd repo.svn &&
	svn propdel svn:executable trunk/main.c &&
	svn commit -m "Change mode to normal" &&
	svn propset svn:date --revprop -r HEAD $COMMIT_DATE &&
	svn propset svn:author --revprop -r HEAD author1)
'

test_export_import

cat >expect <<EOF
:100755 100644 0e5f181f94f2ff9f984b4807887c4d2c6f642723 0e5f181f94f2ff9f984b4807887c4d2c6f642723 M	main.c
EOF

test_expect_failure 'Validate file mode modify' '
(cd repo.git &&
	git diff-tree master^ master >actual &&
	test_cmp ../expect actual)
'

test_tick

mkdir -p repo.svn/trunk/lib
cat >repo.svn/trunk/lib.c <<EOF
void dummy(void) {
	// Do nothing
}
EOF

test_expect_success 'Commit empty dir and new file' '
(cd repo.svn &&
	svn add trunk/lib &&
	svn add trunk/lib.c &&
	svn commit -m "Empty dir added" &&
	svn propset svn:date --revprop -r HEAD $COMMIT_DATE &&
	svn propset svn:author --revprop -r HEAD author1)
'

test_export_import

cat >expect <<EOF
:000000 100644 0000000000000000000000000000000000000000 87734a8c690949471d1836b2e9247ad8f82c9df6 A	lib.c
EOF

test_expect_success 'Validate empty dir was not added' '
(cd repo.git &&
	git diff-tree master^ master >actual &&
	test_cmp ../expect actual)
'

test_tick

test_expect_success 'Commit file move' '
(cd repo.svn &&
	svn mv trunk/lib.c trunk/lib &&
	svn commit -m "File moved to dir" &&
	svn propset svn:date --revprop -r HEAD $COMMIT_DATE &&
	svn propset svn:author --revprop -r HEAD author1)
'

test_export_import

cat >expect <<EOF
:100644 100644 87734a8c690949471d1836b2e9247ad8f82c9df6 87734a8c690949471d1836b2e9247ad8f82c9df6 R100	lib.c	lib/lib.c
EOF

test_expect_success 'Validate file move' '
(cd repo.git &&
	git diff-tree -M -r master^ master >actual &&
	test_cmp ../expect actual)
'

test_tick

test_expect_success 'Commit file copy' '
(cd repo.svn &&
	svn cp trunk/main.c trunk/lib &&
	svn commit -m "File copied to dir" &&
	svn propset svn:date --revprop -r HEAD $COMMIT_DATE &&
	svn propset svn:author --revprop -r HEAD author1)
'

test_export_import

cat >expect <<EOF
:100644 100644 0e5f181f94f2ff9f984b4807887c4d2c6f642723 0e5f181f94f2ff9f984b4807887c4d2c6f642723 C100	main.c	lib/main.c
EOF

test_expect_success 'Validate file copy' '
(cd repo.git &&
	git diff-tree --find-copies-harder -r master^ master >actual &&
	test_cmp ../expect actual)
'

test_tick

test_expect_success 'Commit file delete' '
(cd repo.svn &&
	svn rm trunk/main.c &&
	svn commit -m "File removed" &&
	svn propset svn:date --revprop -r HEAD $COMMIT_DATE &&
	svn propset svn:author --revprop -r HEAD author1)
'

test_export_import

cat >expect <<EOF
:100644 000000 0e5f181f94f2ff9f984b4807887c4d2c6f642723 0000000000000000000000000000000000000000 D	main.c
EOF

test_expect_success 'Validate file remove' '
(cd repo.git &&
	git diff-tree master^ master >actual &&
	test_cmp ../expect actual)
'

test_tick

test_expect_success 'Commit directory move' '
(cd repo.svn &&
	svn update &&
	svn mv trunk/lib trunk/src &&
	svn commit -m "Directory renamed" &&
	svn propset svn:date --revprop -r HEAD $COMMIT_DATE &&
	svn propset svn:author --revprop -r HEAD author1)
'

test_export_import

cat >expect <<EOF
:100644 100644 87734a8c690949471d1836b2e9247ad8f82c9df6 87734a8c690949471d1836b2e9247ad8f82c9df6 R100	lib/lib.c	src/lib.c
:100644 100644 0e5f181f94f2ff9f984b4807887c4d2c6f642723 0e5f181f94f2ff9f984b4807887c4d2c6f642723 R100	lib/main.c	src/main.c
EOF

test_expect_success 'Validate directory move' '
(cd repo.git &&
	git diff-tree -M -r master^ master >actual &&
	test_cmp ../expect actual)
'

test_tick

test_expect_success 'Commit directory copy' '
(cd repo.svn &&
	svn cp trunk/src trunk/lib &&
	svn commit -m "Directory copied" &&
	svn propset svn:date --revprop -r HEAD $COMMIT_DATE &&
	svn propset svn:author --revprop -r HEAD author1)
'

test_export_import

cat >expect <<EOF
:040000 040000 f9e724c547c90dfac10d779fcae9f9bc299245c1 f9e724c547c90dfac10d779fcae9f9bc299245c1 C100	src	lib
EOF

test_expect_success 'Validate directory copy' '
(cd repo.git &&
	git diff-tree --find-copies-harder master^ master >actual &&
	test_cmp ../expect actual)
'

test_tick

test_expect_success 'Commit directory delete' '
(cd repo.svn &&
	svn rm trunk/src &&
	svn commit -m "Directory removed" &&
	svn propset svn:date --revprop -r HEAD $COMMIT_DATE &&
	svn propset svn:author --revprop -r HEAD author1)
'

test_export_import

cat >expect <<EOF
:040000 000000 f9e724c547c90dfac10d779fcae9f9bc299245c1 0000000000000000000000000000000000000000 D	src
EOF

test_expect_success 'Validate directory remove' '
(cd repo.git &&
	git diff-tree master^ master >actual &&
	test_cmp ../expect actual)
'

test_tick

test_expect_success 'Commit new branch' '
(cd repo.svn &&
	svn cp trunk branches/some-feature &&
	svn commit -m "New feature branch created" &&
	svn propset svn:date --revprop -r HEAD $COMMIT_DATE &&
	svn propset svn:author --revprop -r HEAD author1)
'

test_export_import

cat >expect <<EOF
  some-feature
EOF

test_expect_success 'Validate branch create' '
(cd repo.git &&
	git branch --list some-feature >actual &&
	test_cmp ../expect actual)
'

cat >expect <<EOF
6efdf6e New feature branch created
EOF

test_expect_success 'Validate branch last commit' '
(cd repo.git &&
	git log -n 1 --oneline some-feature >actual &&
	test_cmp ../expect actual)
'

test_tick

cat >repo.svn/branches/some-feature/main.c <<EOF
#include <stdio.h>

int main() {
	return 0;
}
EOF

test_expect_success 'Commit new file into branch' '
(cd repo.svn &&
	svn add branches/some-feature/main.c &&
	svn commit -m "Add new file into branch" &&
	svn propset svn:date --revprop -r HEAD $COMMIT_DATE &&
	svn propset svn:author --revprop -r HEAD author1)
'

test_export_import

cat >expect <<EOF
:000000 100644 0000000000000000000000000000000000000000 dc79d60e60844e1b0e66bf7b68e701b1cce6039b A	main.c
EOF

test_expect_success 'Validate new file in branch' '
(cd repo.git &&
	git diff-tree some-feature^ some-feature >actual &&
	test_cmp ../expect actual)
'

test_tick

cat >repo.svn/branches/some-feature/main.c <<EOF
#include <stdio.h>

int main(int argc, char **argv) {
	printf("Hello, world\n");
	return 0;
}
EOF

test_expect_success 'Commit file modify into branch' '
(cd repo.svn &&
	svn commit -m "Modify file in branch" &&
	svn propset svn:date --revprop -r HEAD $COMMIT_DATE &&
	svn propset svn:author --revprop -r HEAD author1)
'

test_export_import

cat >expect <<EOF
:100644 100644 dc79d60e60844e1b0e66bf7b68e701b1cce6039b a0e3cbb39b4f45aaf1a28c2f125a4069593dd511 M	main.c
EOF

test_expect_success 'Validate file modify in branch' '
(cd repo.git &&
	git diff-tree some-feature^ some-feature >actual &&
	test_cmp ../expect actual)
'

test_tick

mkdir -p repo.svn/branches/some-feature/lib2
cat >repo.svn/branches/some-feature/lib2/fun.c <<EOF
#include <string.h>

int fun(const char *str) {
	if (strcmp(str, "teststring") == 0) {
		return 34;
	}
	return 42;
}
EOF

test_expect_success 'Commit new dir with file into branch' '
(cd repo.svn &&
	svn add branches/some-feature/lib2 &&
	svn commit -m "Add new dir and file" &&
	svn propset svn:date --revprop -r HEAD $COMMIT_DATE &&
	svn propset svn:author --revprop -r HEAD author1)
'

test_export_import

cat >expect <<EOF
:000000 040000 0000000000000000000000000000000000000000 16feeafb28986a880f6c361e0891d6f60cb29352 A	lib2
EOF

test_expect_success 'Validate new dir in branch' '
(cd repo.git &&
	git diff-tree some-feature^ some-feature >actual &&
	test_cmp ../expect actual)
'

cat >expect <<EOF
:000000 100644 0000000000000000000000000000000000000000 b6289d9d04ef92ec0efbc839ebb78965906c5d7d A	lib2/fun.c
EOF

test_expect_success 'Validate new file inside new dir in branch' '
(cd repo.git &&
	git diff-tree -r some-feature^ some-feature >actual &&
	test_cmp ../expect actual)
'

test_tick

test_expect_success 'Merge file from branch into trunk' '
(cd repo.svn &&
	svn cp branches/some-feature/main.c trunk &&
	svn commit -m "Merge file addition from branch" &&
	svn propset svn:date --revprop -r HEAD $COMMIT_DATE &&
	svn propset svn:author --revprop -r HEAD author1)
'

test_export_import

cat >expect <<EOF
:000000 100644 0000000000000000000000000000000000000000 a0e3cbb39b4f45aaf1a28c2f125a4069593dd511 A	main.c
EOF

test_expect_success 'Validate file merge into master' '
(cd repo.git &&
	git diff-tree master^ master >actual &&
	test_cmp ../expect actual)
'

test_tick

test_expect_success 'Merge directory from branch into trunk' '
(cd repo.svn &&
	svn cp branches/some-feature/lib2 trunk &&
	svn commit -m "Merge directory addition from branch" &&
	svn propset svn:date --revprop -r HEAD $COMMIT_DATE &&
	svn propset svn:author --revprop -r HEAD author1)
'

test_export_import

cat >expect <<EOF
:000000 040000 0000000000000000000000000000000000000000 16feeafb28986a880f6c361e0891d6f60cb29352 A	lib2
EOF

test_expect_success 'Validate directory merge into master' '
(cd repo.git &&
	git diff-tree master^ master >actual &&
	test_cmp ../expect actual)
'

cat >expect <<EOF
:000000 100644 0000000000000000000000000000000000000000 b6289d9d04ef92ec0efbc839ebb78965906c5d7d A	lib2/fun.c
EOF

test_expect_success 'Validate file merge inside new dir into master' '
(cd repo.git &&
	git diff-tree -r master^ master >actual &&
	test_cmp ../expect actual)
'

test_tick

test_expect_success 'Remove branch' '
(cd repo.svn &&
	svn update &&
	svn rm branches/some-feature &&
	svn commit -m "Remove branch" &&
	svn propset svn:date --revprop -r HEAD $COMMIT_DATE &&
	svn propset svn:author --revprop -r HEAD author1)
'

test_export_import

cat >expect <<EOF
EOF

test_expect_failure 'Validate branch remove' '
(cd repo.git &&
	git branch --list some-feature >actual &&
	test_cmp ../expect actual)
'

test_tick

test_expect_success 'Create new branch' '
(cd repo.svn &&
	svn cp trunk branches/new-feature &&
	svn commit -m "Add new branch" &&
	svn propset svn:date --revprop -r HEAD $COMMIT_DATE &&
	svn propset svn:author --revprop -r HEAD author1)
'

test_export_import

cat >expect <<EOF
  new-feature
EOF

test_expect_success 'Validate branch creation' '
(cd repo.git &&
	git branch --list new-feature >actual &&
	test_cmp ../expect actual)
'

test_tick

test_expect_success 'Create another branch' '
(cd repo.svn &&
	svn cp trunk branches/new-feature-2 &&
	svn commit -m "Add another branch" &&
	svn propset svn:date --revprop -r HEAD $COMMIT_DATE &&
	svn propset svn:author --revprop -r HEAD author1)
'

test_export_import

cat >expect <<EOF
  new-feature-2
EOF

test_expect_success 'Validate branch creation' '
(cd repo.git &&
	git branch --list new-feature-2 >actual &&
	test_cmp ../expect actual)
'

test_tick

test_expect_success 'Create one more branch' '
(cd repo.svn &&
	svn cp branches/new-feature-2 branches/another-feature &&
	svn commit -m "Add one more branch" &&
	svn propset svn:date --revprop -r HEAD $COMMIT_DATE &&
	svn propset svn:author --revprop -r HEAD author1)
'

test_export_import

cat >expect <<EOF
  another-feature
EOF

test_expect_success 'Validate branch creation' '
(cd repo.git &&
	git branch --list another-feature >actual &&
	test_cmp ../expect actual)
'

test_done
