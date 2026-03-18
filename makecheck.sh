#!/bin/sh
#
# Run a check in a temporary dir
#

# Source directory
SOURCE=`pwd`

# Dest directory
DEST=`mktemp -d`

make distclean

cd $DEST

if ! $SOURCE/configure; then
	exit 1
fi

if ! make -j4; then
	exit 1
fi

if ! make check; then
	exit 1
fi

if ! make distclean; then
	exit 1
fi

if ! $SOURCE/configure --host=x86_64-w64-mingw32.static --build=`$SOURCE/config.guess`; then
	exit 1
fi

if ! make -j4; then
	exit 1
fi

if ! make check; then
	exit 1
fi

if ! make distclean; then
	exit 1
fi
