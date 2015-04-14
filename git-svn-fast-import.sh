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

OPTIONS_SPEC="\
git-svn-fast-import [options]
--
h,help                  show the help
s,stdlayout             set trunk,tags,branches as the relative paths, which is SVN default
T,trunk=path            set trunk to a relative repository <path>
t,tags=path             set tags to a relative repository <path>, can be specified multiple times
b,branches=path         set branches to a relative repository <path>, can be specified multiple times
I,ignore-path=path      ignore a relative repository <path>, can be specified multiple times
A,authors-file=file     load from <file> the mapping of SVN committer names to Git commit authors
export-rev-marks=file   dump the SVN revision marks to <file>
export-marks=file       load Git marks from <file>
import-marks=file       dump Git marks into <file>
v,verbose               verbose output mode"

eval "$(echo "$OPTIONS_SPEC" | git rev-parse --parseopt -- $@ || echo exit $?)"

SVN_FAST_EXPORT_ARGS=
GIT_FAST_IMPORT_ARGS=

while [ "$#" -gt 0 ]; do
case $1 in
    -s|-v)
        SVN_FAST_EXPORT_ARGS="$SVN_FAST_EXPORT_ARGS $1"
        shift
        ;;
    -T|-t|-b|-I|-A|--export-rev-marks)
        SVN_FAST_EXPORT_ARGS="$SVN_FAST_EXPORT_ARGS $1 $2"
        shift 2
        ;;
    --export-marks|--import-marks)
        GIT_FAST_IMPORT_ARGS="$GIT_FAST_IMPORT_ARGS $1=$2"
        shift 2
        ;;
    --)
        shift
        ;;
    *)
        echo "Unknown argument: $1"
        exit 1
esac
done

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

git fast-import $GIT_FAST_IMPORT_ARGS --cat-blob-fd=3 --done <$CHAN 3>$BACKCHAN &
FAST_IMPORT_PID=$!

svn-fast-export $SVN_FAST_EXPORT_ARGS >$CHAN 3<$BACKCHAN
RET_CODE=$?

wait $FAST_IMPORT_PID
GIT_RET_CODE=$?

if test $RET_CODE -gt 0; then
	exit $RET_CODE
elif test $GIT_RET_CODE -gt 0; then
	exit $GIT_RET_CODE
else
	exit 0
fi
