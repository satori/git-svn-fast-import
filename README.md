# git-svn-fast-import

*git-svn-fast-import* is a tool for fast Subversion-to-Git conversion.

## Installation

Use the `make` command:

	$ make

## Requirements

* [Apache Portable Runtime](https://apr.apache.org/)
* [Subversion](https://subversion.apache.org/)

## Example

	$ mkdir -p repo.git && cd repo.git
	$ git init
	$ svnadmin dump /path/to/repo | git-svn-fast-import | git fast-import

## Copyright

Copyright (C) 2014 by Maxim Bublis <b@codemonkey.ru>.

*git-svn-fast-import* released under MIT License.
See [LICENSE](https://github.com/satori/go.uuid/blob/master/LICENSE) for details.
