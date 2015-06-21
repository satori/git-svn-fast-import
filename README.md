# git-svn-fast-import

[![Build Status](https://travis-ci.org/satori/git-svn-fast-import.svg?branch=master)](https://travis-ci.org/satori/git-svn-fast-import)

*git-svn-fast-import* is a toolset for fast Subversion-to-Git conversion.

*svn-fast-export* is Subversion dump utility.

It features:
* branch history support
* multi-branch SVN revisions support
* SVN committer to Git author mapping

*svn-ls-tree* is Subversion equivalent of Git's ls-tree command.

It tries to mimic git ls-tree behaviour:

* computes blob/tree checksums the same way as Git;
* skips empty directories;
* outputs in the same format as Git.

## Installation

Use the `make` command:

	$ export PREFIX=$HOME/opt
	$ make
	$ make install

## Requirements

* [Apache Portable Runtime](https://apr.apache.org/) >= 1.5
* [Subversion](https://subversion.apache.org/) >= 1.8
* [Git](http://git-scm.com/) >= 2.1.2

## Example

	$ mkdir -p repo.git && cd repo.git
	$ git init
	$ git-svn-fast-import --stdlayout -r 0:100000 /path/to/svnrepo
	progress Skipped revision 0
	progress Imported revision 1
	progress Imported revision 2
	progress Imported revision 3
	...
	progress Imported revision 99999
	progress Imported revision 100000

## Copyright

Copyright (C) 2014-2015 by Maxim Bublis <b@codemonkey.ru>.

*git-svn-fast-import* released under MIT License.
See [LICENSE](https://github.com/satori/git-svn-fast-import/blob/master/LICENSE) for details.
