# Copyright (C) 2014 by Maxim Bublis <b@codemonkey.ru>
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

CC ?= cc
RM = rm -f
CFLAGS = -g -O2 -Wall
LDFLAGS =
EXTLIBS = -lapr-1 -lsvn_subr-1 -lsvn_repos-1

uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')

-include config.mak

ifeq ($(uname_S),Darwin)
	ifndef NO_DARWIN_PORTS
		ifeq ($(shell test -d /opt/local/lib && echo y),y)
			CFLAGS += -I/opt/local/include
			LDFLAGS += -L/opt/local/lib
		endif
	endif
endif


GIT_SVN_FAST_IMPORT = git-svn-fast-import
OBJECTS = svn-fast-import.o parse.o


all: $(GIT_SVN_FAST_IMPORT)

$(GIT_SVN_FAST_IMPORT): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(EXTLIBS) -o $@ $(OBJECTS)

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

clean:
	$(RM) $(GIT_SVN_FAST_IMPORT) $(OBJECTS)

.PHONY: all clean
