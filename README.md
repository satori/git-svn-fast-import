# git-svn-fast-import

[![Build Status](https://travis-ci.org/satori/git-svn-fast-import.svg?branch=master)](https://travis-ci.org/satori/git-svn-fast-import)

*git-svn-fast-import* is a tool for fast Subversion-to-Git conversion.
It features:
* branch history support
* multi-branch SVN revisions support
* SVN committer to Git author mapping

## Installation

Use the `make` command:

	$ make

## Requirements

* [Apache Portable Runtime](https://apr.apache.org/)
* [Subversion](https://subversion.apache.org/)
* [Git](http://git-scm.com/) >= 2.1.2

## Example

	$ mkdir -p repo.git && cd repo.git
	$ git init
	$ svnadmin dump -r 0:100000 /path/to/repo | git-svn-fast-import --stdlayout
	progress Skipped revision 0
	progress Imported revision 1
	progress Found branch at branches/stable
	progress Imported revision 2
	progress Imported revision 3
	...
	progress Imported revision 99999
	progress Imported revision 100000

## Copyright

Copyright (C) 2014-2015 by Maxim Bublis <b@codemonkey.ru>.

*git-svn-fast-import* released under MIT License.
See [LICENSE](https://github.com/satori/git-svn-fast-import/blob/master/LICENSE) for details.
