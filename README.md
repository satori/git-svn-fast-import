# git-svn-fast-import

*git-svn-fast-import* is a tool for fast Subversion-to-Git conversion.
It features branches support.

## Installation

Use the `make` command:

	$ make

## Requirements

* [Apache Portable Runtime](https://apr.apache.org/)
* [Subversion](https://subversion.apache.org/)

## Example

	$ mkdir -p repo.git && cd repo.git
	$ git init
	$ svnadmin dump -r 0:100000 /path/to/repo | git-svn-fast-import --stdlayout | git fast-import
	progress Skipped revision 0
	progress Imported revision 1
	progress Found branch at branches/stable
	progress Imported revision 2
	progress Imported revision 3
	...
	progress Imported revision 99999
	progress Imported revision 100000

## Copyright

Copyright (C) 2014 by Maxim Bublis <b@codemonkey.ru>.

*git-svn-fast-import* released under MIT License.
See [LICENSE](https://github.com/satori/go.uuid/blob/master/LICENSE) for details.
