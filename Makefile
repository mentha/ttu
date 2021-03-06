# ttu: A program to silently convert TCP sockets to Unix sockets
# Copyright (c) 2014 Cyphar

# Permission is hereby granted, free of charge, to any person obtaining a copy of
# this software and associated documentation files (the "Software"), to deal in
# the Software without restriction, including without limitation the rights to
# use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
# the Software, and to permit persons to whom the Software is furnished to do so,
# subject to the following conditions:

# 1. The above copyright notice and this permission notice shall be included in
#    all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

CC			?= gcc
LNAME		?= libttu.so
WNAME		?= ttu

CLFLAGS		?= -std=c11 -O2 -g -pipe -shared -fPIC
LLFLAGS		?= -ldl

CWFLAGS		?= -std=c11 -O2 -g
LWFLAGS		?=

WARNINGS	?= -Wall -Wextra -Werror

DSRC		?= src
DINCLUDE	?= include

WSRC		?= ttu.c
LSRC		?= $(wildcard $(DSRC)/*.c)
INCLUDE		?= $(wildcard $(DINCLUDE)/*.h)

# Install directories.

IDLIB		?= /usr/lib
IDBIN		?= /usr/bin
INSTALL         ?= install

.PHONY: all debug clean

all: clean $(LNAME) $(WNAME)
debug: clean debug-lib debug-wrap


# Build normal libttu.so binary.
$(LNAME): $(LSRC) $(INCLUDE)
	$(CC) $(CLFLAGS) $(LSRC) $(LLFLAGS) -I$(DINCLUDE)/ -o $(LNAME) $(WARNINGS)

# Build normal ttu binary.
$(WNAME): $(WSRC) $(INCLUDE)
	$(CC) $(CWFLAGS) $(WSRC) $(LWFLAGS) -I$(DINCLUDE)/ -o $(WNAME) $(WARNINGS)

# Install binaries.
install: $(LNAME) $(WNAME)
	$(INSTALL) -D $(LNAME) $(DESTDIR)$(IDLIB)/$(LNAME)
	$(INSTALL) -D $(WNAME) $(DESTDIR)$(IDBIN)/$(WNAME)

# Uninstall binaries.
uninstall:
	rm -fv $(DESTDIR)$(IDLIB)/$(LNAME) $(DESTDIR)$(IDBIN)/$(WNAME)

clean:
	rm -f $(LNAME) $(WNAME)
