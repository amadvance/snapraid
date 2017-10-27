#!/bin/sh
#
# Run all the Sanitizers available
#

# Compiler to use
COMPILER=clang

# Options for configure
# --disable-asm
#   Inline assembly is not supported by the Sanitizers
# --without-blkid
#   External libraries are not supported by the Sanitizers
OPTIONS="--disable-asm --without-blkid"

# Source directory
SOURCE=`pwd`

# Dest directory
DEST=`mktemp -d`

make distclean

cd $DEST

# AddressSanitizer
if ! $SOURCE/configure --enable-asan $OPTIONS CC=$COMPILER; then
	exit 1
fi

if ! make check distclean; then
	exit 1
fi

# UndefinedBehaviourSanitizer
if ! $SOURCE/configure --enable-ubsan $OPTIONS CC=$COMPILER; then
	exit 1
fi

if ! make check distclean; then
	exit 1
fi

# MemorySanitizer
if ! $SOURCE/configure --enable-msan $OPTIONS CC=$COMPILER; then
	exit 1
fi

if ! make check distclean; then
	exit 1
fi

# ThreadSanitizer
if ! $SOURCE/configure --enable-tsan $OPTIONS CC=$COMPILER; then
	exit 1
fi

if ! make check distclean; then
	exit 1
fi

cd $SOURCE

if ! ./configure; then
	exit 1
fi

