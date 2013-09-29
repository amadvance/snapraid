#!/bin/sh
#

make distclean

if ! ./configure.windows-x86; then
	exit 1
fi

if ! test "x$1" = "x-f"; then
	if ! make check; then
		exit 1
	fi
fi

if ! make distwindows-x86 distclean; then
	exit 1
fi

if ! ./configure.windows-x64; then
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

