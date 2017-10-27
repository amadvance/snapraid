#!/bin/sh
#
# Run all the Coverage and Valgrind tests
#

# Source directory
SOURCE=`pwd`

# Dest directory
DEST=`mktemp -d`

make distclean

cd $DEST

# Coverage
if ! $SOURCE/configure --enable-coverage --enable-sde; then
	exit 1
fi

if ! make lcov_reset check lcov_capture lcov_html; then
	exit 1
fi

if ! make distclean; then
	exit 1
fi

# Valgrind
if ! $SOURCE/configure --enable-valgrind; then
	exit 1
fi

if ! make check distclean; then
	exit 1
fi

# Helgrind
if ! $SOURCE/configure --enable-helgrind; then
	exit 1
fi

if ! make check distclean; then
	exit 1
fi

# Drd
if ! $SOURCE/configure --enable-drd; then
	exit 1
fi

if ! make check distclean; then
	exit 1
fi

cd $SOURCE

if ! ./configure; then
	exit 1
fi

