#!/bin/sh
#

make distclean

rm -rf cov_release

if ! ./configure --enable-coverage; then
	exit 1
fi

if ! make lcov_reset check lcov_capture lcov_html; then
	exit 1
fi

mv cov cov_release

if ! make distclean; then
	exit 1
fi

if ! ./configure --enable-valgrind; then
	exit 1
fi

if ! make check distclean; then
	exit 1
fi

if ! ./configure ; then
	exit 1
fi

