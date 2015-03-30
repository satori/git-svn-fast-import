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

test_description='Test plain history support'

. ./helpers.sh
. ./lib/test.sh

test_export_import() {
	test_expect_success 'Import dump into Git' '
	svnadmin dump repo >repo.dump &&
		(cd repo.git && git-svn-fast-import -I data <../repo.dump)
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

cat >repo.svn/main.c <<EOF
int main() {
	return 0;
}
EOF

test_expect_success 'Commit new file' '
(cd repo.svn &&
	svn add main.c &&
	svn_commit "Initial revision")
'

test_export_import

cat >expect <<EOF
bcd5f99c825242e10b409f125f49d7cc931d55bc
:000000 100644 0000000000000000000000000000000000000000 cb3f7482fa46d2ac25648a694127f23c1976b696 A	main.c
EOF

test_expect_success 'Validate files added' '
(cd repo.git &&
	git diff-tree --root master >actual &&
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

test_expect_success 'Commit file content modification' '
(cd repo.svn &&
	svn_commit "Some modification")
'

test_export_import

cat >expect <<EOF
:100644 100644 cb3f7482fa46d2ac25648a694127f23c1976b696 0e5f181f94f2ff9f984b4807887c4d2c6f642723 M	main.c
EOF

test_expect_success 'Validate file content modify' '
(cd repo.git &&
	git diff-tree master^ master >actual &&
	test_cmp ../expect actual)
'

test_tick

test_expect_success 'Commit file mode executable' '
(cd repo.svn &&
	svn propset svn:executable on main.c &&
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

test_expect_success 'Commit file mode normal' '
(cd repo.svn &&
	svn propdel svn:executable main.c &&
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

mkdir -p repo.svn/lib
cat >repo.svn/lib.c <<EOF
void dummy(void) {
	// Do nothing
}
EOF

test_expect_success 'Commit empty dir and new file' '
(cd repo.svn &&
	svn add lib &&
	svn add lib.c &&
	svn_commit "Empty dir added")
'

test_export_import

cat >expect <<EOF
:000000 100644 0000000000000000000000000000000000000000 87734a8c690949471d1836b2e9247ad8f82c9df6 A	lib.c
EOF

test_expect_success 'Validate empty dir was not added' '
(cd repo.git &&
	git diff-tree -M -r master^ master >actual &&
	test_cmp ../expect actual)
'

test_tick

test_expect_success 'Commit file move' '
(cd repo.svn &&
	svn mv lib.c lib &&
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
	svn cp main.c lib &&
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
	svn rm main.c &&
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
	svn mv lib src &&
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
	svn cp src lib &&
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
	svn rm src &&
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

mkdir -p repo.svn/data
dd if=/dev/urandom of=repo.svn/data/bigfile bs=1024 count=10k 2>/dev/null

test_expect_success 'Commit new large blob' '
(cd repo.svn &&
	svn add data &&
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

dd if=/dev/urandom of=repo.svn/data/bigfile2 bs=1024 count=10k 2>/dev/null
cat >repo.svn/lib/main.c <<EOF
#include <stdio.h>

int main() {
	printf("Hello, cruel world\n");
	return 0;
}
EOF

test_expect_success 'Commit new large blob with modification' '
(cd repo.svn &&
	svn add data/bigfile2 &&
	svn_commit "Added large blob with file modification")
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

ln -s lib/main.c repo.svn/main.c

test_expect_success 'Add symlink' '
(cd repo.svn &&
	svn add main.c &&
	svn_commit "Added symlink")
'

test_export_import

cat >expect <<EOF
:000000 120000 0000000000000000000000000000000000000000 a9f09b0144c9a5e76fcf1d1ed40c7b918928afd8 A	main.c
EOF

test_expect_success 'Validate symlink added' '
(cd repo.git &&
	git diff-tree -r master^ master >actual &&
	test_cmp ../expect actual)
'

test_tick

ln -s ../main.c repo.svn/lib/sym.c

test_expect_success 'Add another symlink' '
(cd repo.svn &&
	svn add lib/sym.c &&
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

test_done
