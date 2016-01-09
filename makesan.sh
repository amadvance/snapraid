#!/bin/sh
#
# Run all the Sanitizers available
#

# Compiler to use
COMPILER=clang-3.6

# Options for configure
# --disable-asm
#   Inline assembly is not supported by the Sanitizers
# --without-blkid
#   External libraries are not supported by the Sanitizers
OPTIONS="--disable-asm --without-blkid"

make distclean

# AddressSanitizer
if ! ./configure --enable-asan $OPTIONS CC=$COMPILER; then
	exit 1
fi

if ! make check distclean; then
	exit 1
fi

# UndefinedBehaviourSanitizer
if ! ./configure --enable-ubsan $OPTIONS CC=$COMPILER; then
	exit 1
fi

if ! make check distclean; then
	exit 1
fi

# MemorySanitizer
if ! ./configure --enable-msan $OPTIONS CC=$COMPILER; then
	exit 1
fi

if ! make check distclean; then
	exit 1
fi

# ThreadSanitizer
if ! ./configure --enable-tsan $OPTIONS CC=$COMPILER; then
	exit 1
fi

if ! make check distclean; then
	exit 1
fi

if ! ./configure ; then
	exit 1
fi

