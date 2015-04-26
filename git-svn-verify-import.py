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
import os
import subprocess


class SvnRepo(object):
    """Helper class for Subversion repository
    """
    def __init__(self, path):
        self.path = os.path.realpath(path)
        self.repo = self._get_repo_url()

    def _get_repo_url(self):
        url = None
        info = subprocess.check_output(["svn", "info"],
                                       stderr=subprocess.STDOUT,
                                       cwd=self.path)

        for line in info.splitlines():
            if line.startswith("URL: "):
                url = line[len("URL: "):]

        return url

    def _get_rev_url(self, rev):
        return "{}@{}".format(self.repo, rev)

    def checkout(self, rev):
        subprocess.check_output(["svn", "checkout", self._get_rev_url(rev), "."],
                                stderr=subprocess.STDOUT,
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


class GitRepo(object):
    """Helper class for Git repository
    """
    def __init__(self, path):
        self.path = os.path.realpath(path)

    def checkout(self, sha):
        subprocess.check_output(["git", "checkout", sha],
                                stderr=subprocess.STDOUT,
                                cwd=self.path)

def parse_git_marks(path):
    """Parses Git marks.
    """
    marks = {}

    with open(path, "r") as f:
        for line in f.readlines():
            mark, sha = line.split()
            marks[mark] = sha

    return marks


def get_checksum(fname):
    return subprocess.check_output(["crc32", fname],
                                   stderr=subprocess.STDOUT)


def compare_checksum(f1, f2):
    """Compares checksums for two paths.
    """
    return get_checksum(f1) == get_checksum(f2)


def compare_mode(f1, f2):
    """Compares modes for two paths.
    """
    return os.stat(f1).st_mode == os.stat(f2).st_mode


def compare_fs_tree(svn_path, git_path, ignored):
    svn_path = os.path.realpath(svn_path)
    git_path = os.path.realpath(git_path)

    errors = []

    for root, dirs, files in os.walk(svn_path):
        subdir = root[len(svn_path) + 1:]

        # Skip ignored paths
        if subdir in ignored:
            while dirs:
                dirs.pop()
            continue

        # Skip .svn directory tree.
        if ".svn" in dirs:
            dirs.remove(".svn")

        for fname in files:
            f1 = os.path.join(root, fname)
            f2 = os.path.join(git_path, subdir, fname)

            if not os.path.exists(f2):
                errors.append("missing path {}".format(f2))
                continue

            if not compare_mode(f1, f2):
                errors.append("mode mismatch for {} and {}".format(f1, f2))

            if not compare_checksum(f1, f2):
                errors.append("checksum mismatch for {} and {}".format(f1, f2))

    return errors


def compare_repositories(svn_path, git_path, svn_marks, git_marks, ignored):
    svn = SvnRepo(svn_path)
    git = GitRepo(git_path)

    git_marks = parse_git_marks(git_marks)

    counter = 1

    for rev, commits in parse_svn_marks(svn_marks):
        if not commits:
            continue

        # Checkout SVN revision
        svn.checkout(rev)

        for (ref, branch, mark) in commits:
            commit = git_marks[mark]

            # Checkout Git commit
            git.checkout(commit)

            errors = compare_fs_tree(os.path.join(svn_path, branch),
                                     git_path,
                                     ignored)

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
