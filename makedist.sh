#!/bin/sh
#

CHECK=check
DEBUG=

if test "x$1" = "x-f"; then
CHECK=all
fi

if test "x$1" = "x-d"; then
DEBUG=--enable-debug
fi

make distclean

# Reconfigure (with force) to get the latest revision from git
autoreconf -f

if ! ./configure.windows-x86 $DEBUG; then
	exit 1
fi

if ! make -j4; then
	exit 1
fi

if ! make distwindows-x86 distclean; then
	exit 1
fi

if ! ./configure.windows-x64 $DEBUG; then
	exit 1
fi

if ! make -j4 $CHECK; then
	exit 1
fi

if ! make distwindows-x64 distclean; then
	exit 1
fi

if ! ./configure ; then
	exit 1
fi

if ! make dist; then
	exit 1
fi
