#!/bin/sh
#
# Run a check in a temporary dir
#

# Source directory
SOURCE=`pwd`

# Dest directory
DEST=`mktemp -d`

cd $DEST

make distclean

if ! $SOURCE/configure; then
	exit 1
fi

if ! time make check; then
	exit 1
fi
