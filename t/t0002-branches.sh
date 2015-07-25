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

test_description='Test branch history support'

. ./helpers.sh
. ./sharness/sharness.sh

cat >authors.txt <<EOF
author1 = A U Thor <author@example.com>
author2 = Com Mit Ter <committer@example.com>
EOF

test_export_import() {
	test_expect_success 'Import dump into Git' '
	(cd repo.git && git-svn-fast-import --quiet --stdlayout -B branches-2 -b branches/some-feature/sub-branch --force -c ../cache.txt -I data -A ../authors.txt --export-rev-marks ../rev-marks.txt --export-marks ../marks.txt ../repo)
	'
}

test_branch_exists() {
	test "$#" = 1 ||
		error "FATAL: test_branch_exist requires 1 argument"

	cat >branch-name <<EOF
  $1
EOF

	(cd repo.git &&
		git branch --list $1 >../actual &&
		test_cmp ../branch-name ../actual)
}

test_branch_not_exists() {
	test "$#" = 1 ||
		error "FATAL: test_branch_not_exists requires 1 argument"

	cat >empty <<EOF
EOF

	(cd repo.git &&
		git branch --list $1 >../actual &&
		test_cmp ../empty ../actual)
}

init_repos

test_tick

test_expect_success 'Commit standard directories layout' '
(cd repo.svn &&
	mkdir -p branches tags trunk &&
	svn add branches tags trunk &&
	svn_commit "Standard project directories initialized")
'

test_export_import

test_tick

mkdir -p repo.svn/branches/without_parent
cat >repo.svn/branches/without_parent/dummy.c <<EOF
EOF

test_expect_success 'Create branch without parent' '
(cd repo.svn &&
	svn add branches/without_parent &&
	svn_commit "Add branch without parent")
'

test_export_import

test_expect_success 'Validate branch creation' '
test_branch_exists "branches--without_parent"
'

init_repos

test_tick

test_expect_success 'Commit standard directories layout' '
(cd repo.svn &&
	mkdir -p branches tags trunk &&
	svn add branches tags trunk &&
	svn_commit "Standard project directories initialized")
'

test_export_import

test_tick

cat >repo.svn/trunk/main.c <<EOF
int main() {
	return 0;
}
EOF

test_expect_success 'Commit new file into trunk' '
(cd repo.svn &&
	svn add trunk/main.c &&
	svn_commit "Initial revision")
'

test_export_import

cat >expect <<EOF
2b6b24602064b0569f3b3cd4fe807e306f1cee29
:000000 100644 0000000000000000000000000000000000000000 cb3f7482fa46d2ac25648a694127f23c1976b696 A	main.c
EOF

test_expect_success 'Validate files added' '
(cd repo.git &&
	git diff-tree --root master >actual &&
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
	svn_commit "Modify file")
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
	svn_commit "Change mode to executable")
'

test_export_import

cat >expect <<EOF
:100644 100755 0e5f181f94f2ff9f984b4807887c4d2c6f642723 0e5f181f94f2ff9f984b4807887c4d2c6f642723 M	main.c
EOF

test_expect_success 'Validate file mode modify' '
(cd repo.git &&
	git diff-tree master^ master >actual &&
	test_cmp ../expect actual)
'

test_tick

test_expect_success 'Copy executable' '
(cd repo.svn &&
	svn cp trunk/main.c trunk/main2.c &&
	svn_commit "Copy executable")
'

test_export_import

cat >expect <<EOF
:000000 100755 0000000000000000000000000000000000000000 0e5f181f94f2ff9f984b4807887c4d2c6f642723 A	main2.c
EOF

test_expect_success 'Validate file mode copy' '
(cd repo.git &&
	git diff-tree master^ master >actual &&
	test_cmp ../expect actual)
'

test_tick

test_expect_success 'Commit file mode normal' '
(cd repo.svn &&
	svn propdel svn:executable trunk/main.c &&
	svn_commit "Change mode to normal")
'

test_export_import

cat >expect <<EOF
:100755 100644 0e5f181f94f2ff9f984b4807887c4d2c6f642723 0e5f181f94f2ff9f984b4807887c4d2c6f642723 M	main.c
EOF

test_expect_success 'Validate file mode modify' '
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
	svn_commit "Empty dir added")
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
	svn_commit "File moved to dir")
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
	svn_commit "File copied to dir")
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
	svn_commit "File removed")
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
	svn_commit "Directory renamed")
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
	svn_commit "Directory copied")
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
	svn_commit "Directory removed")
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

mkdir -p repo.svn/trunk/data
dd if=/dev/urandom of=repo.svn/trunk/data/bigfile bs=1024 count=10k 2>/dev/null

test_expect_success 'Commit new large blob' '
(cd repo.svn &&
	svn add trunk/data &&
	svn_commit "Added large blob")
'

(cd repo.git &&
	git describe --always > ../expect)

test_export_import

test_expect_success 'Validate ignored path skipped' '
(cd repo.git &&
	git describe --always >actual &&
	test_cmp ../expect actual)
'

test_tick

dd if=/dev/urandom of=repo.svn/trunk/data/bigfile2 bs=1024 count=10k 2>/dev/null
cat >repo.svn/trunk/lib/main.c <<EOF
#include <stdio.h>

int main() {
	printf("Hello, cruel world\n");
	return 0;
}
EOF

test_expect_success 'Commit new large blob with modification' '
(cd repo.svn &&
	svn add trunk/data/bigfile2 &&
	svn_commit "Added large blob")
'

test_export_import

cat >expect <<EOF
:100644 100644 0e5f181f94f2ff9f984b4807887c4d2c6f642723 90c933208dc8c6b307c53005493008bad1945e65 M	lib/main.c
EOF

test_expect_success 'Validate ignored path skipped' '
(cd repo.git &&
	git diff-tree -r master^ master >actual &&
	test_cmp ../expect actual)
'

test_tick

test_expect_success 'Commit new tag from trunk' '
(cd repo.svn &&
	svn cp trunk tags/release-1.0 &&
	svn_commit "New tag")
'

test_export_import

test_expect_success 'Validate tag create' '
test_branch_exists "tags--release-1.0"
'

test_expect_failure 'Compare source and target commit-ish' '
(cd repo.git &&
	git describe --always master >expect &&
	git describe --always tags--release-1.0 >actual &&
	test_cmp expect actual)
'

test_tick

test_expect_success 'Commit new branch' '
(cd repo.svn &&
	svn cp trunk branches/some-feature &&
	svn_commit "New feature branch created")
'

test_export_import

test_expect_success 'Validate branch create' '
test_branch_exists "branches--some-feature"
'

test_expect_failure 'Compare source and target commit-ish' '
(cd repo.git &&
	git describe --always master >expect &&
	git describe --always branches--some-feature >actual &&
	test_cmp expect actual)
'

test_tick

test_expect_success 'Create sub-branch' '
(cd repo.svn &&
	svn cp trunk branches/some-feature/sub-branch &&
	svn_commit "Create sub-branch")
'

test_export_import

test_expect_success 'Validate branch create' '
test_branch_exists "branches--some-feature--sub-branch"
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
	svn_commit "Add new file into branch")
'

test_export_import

cat >expect <<EOF
:000000 100644 0000000000000000000000000000000000000000 dc79d60e60844e1b0e66bf7b68e701b1cce6039b A	main.c
EOF

test_expect_success 'Validate new file in branch' '
(cd repo.git &&
	git diff-tree branches--some-feature^ branches--some-feature >actual &&
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
	svn_commit "Modify file in branch")
'

test_export_import

cat >expect <<EOF
:100644 100644 dc79d60e60844e1b0e66bf7b68e701b1cce6039b a0e3cbb39b4f45aaf1a28c2f125a4069593dd511 M	main.c
EOF

test_expect_success 'Validate file modify in branch' '
(cd repo.git &&
	git diff-tree branches--some-feature^ branches--some-feature >actual &&
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
	svn_commit "Add new dir and file")
'

test_export_import

cat >expect <<EOF
:000000 040000 0000000000000000000000000000000000000000 16feeafb28986a880f6c361e0891d6f60cb29352 A	lib2
EOF

test_expect_success 'Validate new dir in branch' '
(cd repo.git &&
	git diff-tree branches--some-feature^ branches--some-feature >actual &&
	test_cmp ../expect actual)
'

cat >expect <<EOF
:000000 100644 0000000000000000000000000000000000000000 b6289d9d04ef92ec0efbc839ebb78965906c5d7d A	lib2/fun.c
EOF

test_expect_success 'Validate new file inside new dir in branch' '
(cd repo.git &&
	git diff-tree -r branches--some-feature^ branches--some-feature >actual &&
	test_cmp ../expect actual)
'

test_tick

test_expect_success 'Merge file from branch into trunk' '
(cd repo.svn &&
	svn cp branches/some-feature/main.c trunk &&
	svn_commit "Merge file addition from branch")
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
	svn_commit "Merge directory addition from branch")
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

test_expect_success 'Validate tag create from branch' '
(cd repo.svn &&
	svn cp branches/some-feature tags/some-feature-before-remove &&
	svn_commit "Create another tag")
'

test_export_import

test_expect_success 'Validate tag create' '
test_branch_exists "tags--some-feature-before-remove"
'

test_tick

test_expect_success 'Remove branch' '
(cd repo.svn &&
	svn update &&
	svn rm branches/some-feature &&
	svn_commit "Remove branch")
'

test_export_import

test_expect_success 'Validate branch remove' '
test_branch_not_exists "branches--some-feature"
'

test_expect_success 'Validate sub-branch remove' '
test_branch_not_exists "branches--some-feature--sub-branch"
'

test_tick

test_expect_success 'Create new branch' '
(cd repo.svn &&
	svn cp trunk branches/new-feature &&
	svn_commit "Add new branch")
'

test_export_import

test_expect_success 'Validate branch creation' '
test_branch_exists "branches--new-feature"
'

test_expect_success 'Compare source and target commit-ish' '
(cd repo.git &&
	git describe --always master >expect &&
	git describe --always branches--new-feature >actual &&
	test_cmp expect actual)
'

test_tick

test_expect_success 'Create another branch' '
(cd repo.svn &&
	svn cp trunk branches/new-feature-2 &&
	svn_commit "Add another branch")
'

test_export_import

test_expect_success 'Validate branch creation' '
test_branch_exists "branches--new-feature-2"
'

test_expect_success 'Compare source and target commit-ish' '
(cd repo.git &&
	git describe --always master >expect &&
	git describe --always branches--new-feature-2 >actual &&
	test_cmp expect actual)
'

test_tick

test_expect_success 'Create one more branch' '
(cd repo.svn &&
	svn cp branches/new-feature-2 branches/another-feature &&
	svn_commit "Add one more branch")
'

test_export_import

test_expect_success 'Validate branch creation' '
test_branch_exists "branches--another-feature"
'

test_expect_success 'Compare source and target commit-ish' '
(cd repo.git &&
	git describe --always branches--new-feature-2 >expect &&
	git describe --always branches--another-feature >actual &&
	test_cmp expect actual)
'

test_tick

mkdir -p repo.svn/garbage

test_expect_success 'Commit out-of-branches garbage' '
(cd repo.svn &&
	svn add garbage &&
	svn_commit "Add garbage commit")
'

test_export_import

test_tick

test_expect_success 'Commit garbage remove' '
(cd repo.svn &&
	svn rm garbage &&
	svn_commit "Remove garbage")
'

test_export_import

test_tick

test_expect_success 'Create branch after garbage removal' '
(cd repo.svn &&
	svn update &&
	svn cp branches/another-feature branches/no-features &&
	svn_commit "Create branch after garbage removal")
'

test_export_import

test_expect_success 'Validate branch creation' '
test_branch_exists "branches--no-features"
'

test_expect_success 'Compare source and target commit-ish' '
(cd repo.git &&
	git describe --always branches--another-feature >expect &&
	git describe --always branches--no-features >actual &&
	test_cmp expect actual)
'

test_tick

cat >repo.svn/branches/another-feature/doc.txt << EOF
another-feature documentation
EOF

cat >repo.svn/branches/new-feature/README << EOF
new-feature README
EOF

test_expect_success 'Commit into multiple branches' '
(cd repo.svn &&
	svn add branches/another-feature/doc.txt &&
	svn add branches/new-feature/README &&
	svn_commit "Commit into multiple branches")
'

test_export_import

cat >expect <<EOF
:000000 100644 0000000000000000000000000000000000000000 c16dbc56913951c9ba0cf7248809aa2c546ba51b A	doc.txt
EOF

test_expect_success 'Validate commit into first branch' '
(cd repo.git &&
	git diff-tree branches--another-feature^ branches--another-feature >actual &&
	test_cmp ../expect actual)
'

cat >expect <<EOF
:000000 100644 0000000000000000000000000000000000000000 8533c3e2e2490184d7d6e7833ac818317bb28529 A	README
EOF

test_expect_success 'Validate commit into second branch' '
(cd repo.git &&
	git diff-tree branches--new-feature^ branches--new-feature >actual &&
	test_cmp ../expect actual)
'

test_tick

test_expect_success 'Commit branch rename' '
(cd repo.svn &&
	svn mv branches/no-features branches/mega-features &&
	svn_commit "Rename branch no-features into mega-features")
'

test_export_import

test_expect_success 'Validate old branch name disappeared' '
test_branch_not_exists "branches--no-features"
'

test_expect_success 'Validate branch with new name appeared' '
test_branch_exists "branches--mega-features"
'

test_tick

(cd repo.git &&
	git describe --always branches--new-feature-2 >branch-before-remove)

test_expect_success 'Remove branch' '
(cd repo.svn &&
	svn rm branches/new-feature-2 &&
	svn_commit "Remove branch new-feature-2")
'

test_export_import

test_expect_success 'Validate branch removed' '
test_branch_not_exists "branches--new-feature-2"
'

test_tick

test_expect_success 'Restore branch using svn merge' '
(cd repo.svn &&
	svn update &&
	svn merge -r COMMITTED:PREV . &&
	svn_commit "Revert previous commit")
'

test_export_import

test_expect_success 'Validate branch restored' '
test_branch_exists "branches--new-feature-2"
'

test_expect_success 'Compare branch commit-ish before and after restoration' '
(cd repo.git &&
	git describe --always branches--new-feature-2 >actual &&
	test_cmp branch-before-remove actual)
'

test_tick

(cd repo.git &&
	git describe --always master >master-before-remove)

test_expect_success 'Remove trunk' '
(cd repo.svn &&
	svn rm trunk &&
	svn_commit "Remove trunk")
'

test_export_import

test_expect_success 'Validate master branch removed' '
test_branch_not_exists "master"
'

test_tick

test_expect_success 'Restore trunk using svn merge' '
(cd repo.svn &&
	svn update &&
	svn merge -r COMMITTED:PREV . &&
	svn_commit "Revert previous commit")
'

test_export_import

cat >expect <<EOF
* master
EOF

test_expect_success 'Validate master branch restored' '
(cd repo.git &&
	git branch --list master >actual &&
	test_cmp ../expect actual)
'

test_expect_success 'Compare master commit-ish before and after restoration' '
(cd repo.git &&
	git describe --always master >actual &&
	test_cmp master-before-remove actual)
'

test_tick

SAVED_REVISION=$(cd repo.svn && svn -q update && svn info | grep Revision | cut -d " " -f 2)
export SAVED_REVISION

test_expect_success 'Remove trunk' '
(cd repo.svn &&
	svn rm trunk &&
	svn_commit "Remove trunk")
'

test_export_import

test_expect_success 'Validate master branch removed' '
test_branch_not_exists "master"
'

test_tick

mkdir -p repo.svn/trunk

test_expect_success 'Commit new empty trunk' '
(cd repo.svn &&
	svn add trunk &&
	svn_commit "Add new empty trunk")
'

test_export_import

test_tick

test_expect_success 'Restore trunk using svn cp' '
(cd repo.svn &&
	svn update &&
	svn rm trunk &&
	svn cp trunk@$SAVED_REVISION . &&
	svn_commit "Restore trunk")
'

test_export_import

cat >expect <<EOF
* master
EOF

test_expect_success 'Validate master branch restored' '
(cd repo.git &&
	git branch --list master >actual &&
	test_cmp ../expect actual)
'

test_expect_success 'Compare master commit-ish before and after restoration' '
(cd repo.git &&
	git describe --always master >actual &&
	test_cmp master-before-remove actual)
'

test_tick

ln -s lib/lib.c repo.svn/trunk/lib.c

test_expect_success 'Add symlink' '
(cd repo.svn &&
	svn add trunk/lib.c &&
	svn_commit "Added symlink")
'

test_export_import

cat >expect <<EOF
:000000 120000 0000000000000000000000000000000000000000 8eb927bb6649093b461f131f45b249c2104f0061 A	lib.c
EOF

test_expect_success 'Validate symlink added' '
(cd repo.git &&
	git diff-tree -r master^ master >actual &&
	test_cmp ../expect actual)
'

test_tick

ln -s ../main.c repo.svn/trunk/lib/sym.c

test_expect_success 'Add another symlink' '
(cd repo.svn &&
	svn add trunk/lib/sym.c &&
	svn_commit "Added another symlink")
'

test_export_import

cat >expect <<EOF
:000000 120000 0000000000000000000000000000000000000000 60166b0ccc37228e4ea93d89247bccd0992be4fd A	lib/sym.c
EOF

test_expect_success 'Validate symlink added' '
(cd repo.git &&
	git diff-tree -r master^ master >actual &&
	test_cmp ../expect actual)
'

test_expect_success 'Remove revision author property' '
(cd repo.svn &&
	svn propdel svn:author --revprop -r HEAD)
'

test_export_import

test_expect_success 'Restore revision author property' '
(cd repo.svn &&
	svn propset svn:author --revprop -r HEAD author1)
'

test_tick

test_expect_success 'Copy branches recursively' '
(cd repo.svn &&
    svn cp branches branches-2 &&
    svn_commit "Copied branches recursively")
'

test_export_import

test_expect_failure 'Validate branches copied recursively' '
(cd repo.git &&
    git branch --list branches--* | sed s/branches/branches-2/ >expect &&
    git branch --list branches-2* >actual &&
    test_cmp expect actual)
'

test_tick

test_expect_success 'Remove all branches and tags' '
(cd repo.svn &&
    svn rm branches &&
    svn rm branches-2 &&
    svn rm tags &&
    svn_commit "Removed all branches and tags")
'

test_export_import

cat >expect <<EOF
* master
EOF

test_expect_success 'Validate branches and tags removed' '
(cd repo.git &&
    git branch --list >actual &&
    test_cmp ../expect actual)
'

test_done
