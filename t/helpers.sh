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

uname_S=$(uname -s)

test_tick() {
	if test -z "${test_tick+set}"; then
		test_tick=1112911993
	else
		test_tick=$(($test_tick + 60))
	fi

	case "$uname_S" in
	Linux)
		COMMIT_DATE=$(date -u -d "@$test_tick" "+%Y-%m-%dT%H:%M:%S.000000Z")
		;;
	Darwin|FreeBSD)
		COMMIT_DATE=$(date -ju -f "%s" $test_tick "+%Y-%m-%dT%H:%M:%S.000000Z")
		;;
	*)
		error "Unsupported OS: $uname_S"
		;;
	esac

	export COMMIT_DATE
}

svn_commit() {
	test "$#" = 1 ||
		error "bug in the test script: not 1 parameter to svn_commit"

	svn commit -m "$1" &&
		svn propset svn:date --revprop -r HEAD $COMMIT_DATE &&
		svn propset svn:author --revprop -r HEAD author1
}
