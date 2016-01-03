#!/bin/sh
#

rm -r cov-int

make distclean

# Reconfigure (with force) to get the latest revision from git
autoreconf -f

if ! ./configure ; then
	exit 1
fi

export PATH=$PATH:contrib/cov-analysis-linux-7.7.0.4/bin

if ! cov-build --dir cov-int make; then
	exit 1
fi

REVISION=`git describe`

tar czf snapraid-$REVISION.tgz cov-int

rm -r cov-int

echo snapraid-$REVISION.tgz ready to upload to https://scan.coverity.com/projects/1986/builds/new

