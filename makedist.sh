#!/bin/sh
#

# Ask root permissions at the start
sudo echo We are root

make distclean

# Reconfigure (with force) to get the latest revision from git
autoreconf -f

if ! ./configure.windows-x86 --enable-warning-as-error; then
	exit 1
fi

if ! make -j4; then
	exit 1
fi

if ! make distwindows-x86 distclean; then
	exit 1
fi

if ! ./configure.windows-x64 --enable-warning-as-error; then
	exit 1
fi

if ! make -j4; then
	exit 1
fi

if ! make distwindows-x64 distclean; then
	exit 1
fi

if ! ./configure --enable-warning-as-error; then
	exit 1
fi

if ! make dist; then
	exit 1
fi

sh makeslackdist.sh
sh makearchdist.sh
sudo sh makeslackware.sh
sudo sh makearch.sh
sudo sh makedeb.sh
sudo sh makerpm.sh
