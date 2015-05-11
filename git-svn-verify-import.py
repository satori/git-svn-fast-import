#!/usr/bin/env python

# Copyright (C) 2015 by Maxim Bublis <b@codemonkey.ru>
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

import argparse
import difflib
import os
import subprocess


class Repo(object):
    def __init__(self, path):
        self.path = os.path.realpath(path)


class SvnRepo(Repo):
    def get_tree(self, rev, path, ignored):
        cmd_options = ["svn-ls-tree", "-r", "-t",
                       "--root", path]

        for p in ignored:
            cmd_options.append("--ignore-path")
            cmd_options.append(p)

        cmd_options.append(self.path)
        cmd_options.append(rev[1:])

        return subprocess.check_output(cmd_options)


class GitRepo(Repo):
    def get_tree(self, sha):
        return subprocess.check_output(["git", "ls-tree", "-r", "-t", sha],
                                       cwd=self.path)


def parse_svn_marks(path):
    """Parses Subversion marks.
    """
    marks = []

    with open(path, "r") as f:
        num_revisions = int(f.readline())
        for i in xrange(int(num_revisions)):
            line = f.readline()
            rev, num_of_commits = line.split()
            commits = []
            for k in xrange(int(num_of_commits)):
                line = f.readline()
                ref, path, mark = line.split()
                commits.append((ref, path, mark))
            marks.append((rev, commits))

    return marks


def parse_git_marks(path):
    """Parses Git marks.
    """
    marks = {}

    with open(path, "r") as f:
        for line in f.readlines():
            mark, sha = line.split()
            marks[mark] = sha

    return marks


def compare_repositories(svn_path, git_path, svn_marks, git_marks, ignored):
    svn = SvnRepo(svn_path)
    git = GitRepo(git_path)

    git_marks = parse_git_marks(git_marks)

    counter = 1

    for rev, commits in parse_svn_marks(svn_marks):
        if not commits:
            continue

        for (ref, branch, mark) in commits:
            commit = git_marks[mark]

            svn_tree = svn.get_tree(rev, branch, ignored)
            git_tree = git.get_tree(commit)

            errors = []
            diff = difflib.Differ()
            for line in diff.compare(svn_tree.splitlines(True), git_tree.splitlines(True)):
                if line[0] == '-' or line[0] == '+':
                    errors.append(line.strip())

            msg = "compare revision {} and commit {} ({})".format(rev, commit, ref)

            if not errors:
                print "ok {} - {}".format(counter, msg)
            else:
                err = "\n".join("# {}".format(e) for e in errors)
                print "\x1b[31;1mnot ok {} - {}\n{}\x1b[0m".format(counter, msg, err)

            counter += 1


def main():
    parser = argparse.ArgumentParser(description="Verifies Git repository after import")
    parser.add_argument("--svn-path", dest="svn_path", type=str, required=True)
    parser.add_argument("--git-path", dest="git_path", type=str, required=True)
    parser.add_argument("--marks", dest="git_marks", type=str, required=True)
    parser.add_argument("--rev-marks", dest="svn_marks", type=str, required=True)
    parser.add_argument("--ignore-path", dest="ignored", type=str, action="append")

    args = parser.parse_args()

    ignored = set()
    if args.ignored:
        ignored = set(args.ignored)

    compare_repositories(args.svn_path,
                         args.git_path,
                         args.svn_marks,
                         args.git_marks,
                         ignored)


if __name__ == "__main__":
    main()
