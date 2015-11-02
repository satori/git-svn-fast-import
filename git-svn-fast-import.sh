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
git-svn-fast-import [options] <repo>
--
h,help                      show the help
r,revision=                 set revision range
s,stdlayout                 set trunk,tags,branches as the relative paths, which is SVN default
b,branch=path               set repository <path> as a branch
B,branches=path             set repository <path> as a root for branches
t,tag=path                  set repository <path> as a tag
T,tags=path                 set repository <path> as a root for tags
I,ignore-path=path          ignore a relative repository <path>
i,ignore-abspath!=path      ignore repository <path>
no-ignore-abspath=path      do not ignore repository <path>
A,authors-file=file         load from <file> the mapping of SVN committer names to Git commit authors
export-rev-marks=file       dump the SVN revision marks to <file>
export-marks=file           dump Git marks into <file>
import-marks=file           load Git marks from <file>
import-marks-if-exists=file load Git marks from <file>, if exists
c,checksum-cache=file       use <file> as a checksum cache
force                       force updating modified existing branches, even if doing so would cause commits to be lost
quiet                       disable all non-fatal output"

eval "$(echo "$OPTIONS_SPEC" | git rev-parse --parseopt -- "$@" || echo exit $?)"

SVN_FAST_EXPORT_ARGS=
GIT_FAST_IMPORT_ARGS=

while [ "$#" -gt 0 ]; do
case $1 in
    -s|-v)
        SVN_FAST_EXPORT_ARGS="$SVN_FAST_EXPORT_ARGS $1"
        shift
        ;;
    -r|-t|-T|-b|-B|-i|-I|-A|--export-rev-marks|--no-ignore-abspath)
        SVN_FAST_EXPORT_ARGS="$SVN_FAST_EXPORT_ARGS $1 $2"
        shift 2
        ;;
	--force|--quiet)
		GIT_FAST_IMPORT_ARGS="$GIT_FAST_IMPORT_ARGS $1"
		shift
		;;
    --export-marks|--import-marks|--import-marks-if-exists)
        GIT_FAST_IMPORT_ARGS="$GIT_FAST_IMPORT_ARGS $1=$2"
        shift 2
        ;;
    --)
        shift
        ;;
    *)
        SVN_FAST_EXPORT_ARGS="$SVN_FAST_EXPORT_ARGS $1"
        shift
        ;;
esac
done

TMP_PREFIX=/tmp/git-svn-fast-import
TMP_SUFFIX=$$
CHAN=$TMP_PREFIX.chan.$TMP_SUFFIX

# Cleanup on EXIT
on_exit() {
	rm -f $CHAN
}

trap 'on_exit' EXIT

mkfifo $CHAN

git fast-import $GIT_FAST_IMPORT_ARGS --done <$CHAN &
FAST_IMPORT_PID=$!

eval "svn-fast-export $SVN_FAST_EXPORT_ARGS" >$CHAN
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
