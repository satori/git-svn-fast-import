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

uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')

ifeq ($(uname_S),Darwin)
	ifndef NO_DARWIN_PORTS
		ifeq ($(shell test -d /opt/local/lib && echo y),y)
			LDFLAGS +=-L/opt/local/lib
		endif
		ifeq ($(shell test -d /opt/local/include && echo y),y)
			CPPFLAGS +=-I/opt/local/include
			ifeq ($(shell test -d /opt/local/include/subversion-1 && echo y),y)
				CPPFLAGS +=-I/opt/local/include/subversion-1
			endif
		endif
	endif
endif

ifeq ($(uname_S),Linux)
	ifeq ($(shell test -d /usr/include/subversion-1 && echo y),y)
		CPPFLAGS +=-I/usr/include/subversion-1
	endif
endif
