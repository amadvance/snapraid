#!/bin/sh
#

rm -r cov-int
rm snapraid.tgz

make distclean

if ! ./configure ; then
	exit 1
fi

export PATH=$PATH:contrib/cov-analysis-linux-7.0.2/bin

if ! cov-build --dir cov-int make; then
	exit 1
fi

tar czf snapraid.tgz cov-int

rm -r cov-int

echo snapraid.tgz ready to upload to https://scan.coverity.com/projects/1986/builds/new



