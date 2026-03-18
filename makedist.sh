#!/bin/sh
#

# Ask root permissions at the start
sudo echo We are root

make distclean

# Reconfigure (with force) to get the latest revision from git
autoreconf -f

if ! ./configure ; then
	exit 1
fi

if ! make dist; then
	exit 1
fi

if ! ./configure.windows-x86; then
	exit 1
fi

if ! make -j4; then
	exit 1
fi

if ! make distwindows-x86 distclean; then
	exit 1
fi

if ! ./configure.windows-x64; then
	exit 1
fi

if ! make -j4; then
	exit 1
fi

if ! make distwindows-x64 distclean; then
	exit 1
fi

sh makeslackdist.sh
sh makearchdist.sh
sudo sh makeslackware.sh
sudo sh makearch.sh
sudo sh makedeb.sh
sudo sh makerpm.sh
