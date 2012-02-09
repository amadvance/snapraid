#!/bin/sh
#

make distclean

if ! ./configure --enable-coverage; then
	exit 1
fi

if ! make lcov_reset check lcov_capture lcov_html; then
	exit 1
fi


