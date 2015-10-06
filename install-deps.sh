#!/bin/sh
#
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

set -e

export PATH=$HOME/workspace/opt/bin:$PATH

# Installing APR 1.5
git clone --depth=50 --branch=1.5.x https://github.com/apache/apr.git $HOME/workspace/deps/apr
cd $HOME/workspace/deps/apr &&
	./buildconf &&
	./configure --prefix=$HOME/workspace/opt &&
	make &&
	make install

# Installing APR-util 1.5
git clone --depth=50 --branch=1.5.x https://github.com/apache/apr-util.git $HOME/workspace/deps/apr-util
cd $HOME/workspace/deps/apr-util &&
	./buildconf &&
	./configure --prefix=$HOME/workspace/opt --with-apr=$HOME/workspace/opt &&
	make &&
	make install

# Install SQLite amalgamation
cd $HOME/workspace/deps &&
	curl -o sqlite.tar.gz http://sqlite.org/2015/sqlite-autoconf-3081101.tar.gz &&
	tar xvf sqlite.tar.gz

cd $HOME/workspace/deps/sqlite-autoconf-3081101 &&
	./configure --prefix=$HOME/workspace/opt &&
	make &&
	make install

# Installing Subersion 1.9
git clone --depth=50 --branch=1.9.x https://github.com/apache/subversion.git $HOME/workspace/deps/subversion
cd $HOME/workspace/deps/subversion &&
	./autogen.sh &&
	./configure --prefix=$HOME/workspace/opt --with-sqlite=$HOME/workspace/opt &&
	make &&
	make install

# Installing Git 2.1.2
git clone --no-checkout https://github.com/git/git.git $HOME/workspace/deps/git
cd $HOME/workspace/deps/git &&
	git checkout v2.1.2 &&
	make configure &&
	./configure --prefix=$HOME/workspace/opt &&
	make &&
	make install
