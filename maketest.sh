#!/bin/sh
#
# Run all the Coverage and Valgrind tests
#

make distclean

# Coverage
if ! ./configure --enable-coverage --enable-sde; then
	exit 1
fi

if ! make lcov_reset check lcov_capture lcov_html; then
	exit 1
fi

# Keep a copy of the coverage results
rm -rf cov_release
mv cov cov_release

if ! make distclean; then
	exit 1
fi

# Valgrind
if ! ./configure --enable-valgrind; then
	exit 1
fi

if ! make check distclean; then
	exit 1
fi

# Helgrind
if ! ./configure --enable-helgrind; then
	exit 1
fi

if ! make check distclean; then
	exit 1
fi

# Drd
if ! ./configure --enable-drd; then
	exit 1
fi

if ! make check distclean; then
	exit 1
fi

if ! ./configure ; then
	exit 1
fi

