# svn-ls-tree

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

* [Apache Portable Runtime](https://apr.apache.org/)
* [Subversion](https://subversion.apache.org/) >= 1.8

## Copyright

Copyright (C) 2015 by Maxim Bublis <b@codemonkey.ru>.

*svn-ls-tree* released under MIT License.
SEE [LICENSE](https://github.com/satori/svn-ls-tree/blob/master/LICENSE) for details.
