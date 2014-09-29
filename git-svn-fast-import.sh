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
TMP_PREFIX=/tmp/git-svn-fast-import
TMP_SUFFIX=$$
CHAN=$TMP_PREFIX.chan.$TMP_SUFFIX
BACKCHAN=$TMP_PREFIX.back.$TMP_SUFFIX

# Cleanup on EXIT
on_exit() {
	rm -f $CHAN $BACKCHAN
}

trap 'on_exit' EXIT

mkfifo $CHAN $BACKCHAN

git fast-import --cat-blob-fd=3 <$CHAN 3>$BACKCHAN &
FAST_IMPORT_PID=$!

svn-fast-export $@ >$CHAN 3<$BACKCHAN
RET_CODE=$?

wait $FAST_IMPORT_PID

exit $RET_CODE
