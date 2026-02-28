#!/bin/sh
#
# Run the Coverage test
#

make distclean

if ! ./configure --enable-coverage --enable-sde; then
	exit 1
fi

if ! make lcov_reset; then
	exit 1
fi

if ! make check; then
	exit 1
fi

# Run commands on a real array to test smartctl
sudo ./snapraid probe
sudo ./snapraid up
sudo ./snapraid smart
sudo ./snapraid probe
sudo ./snapraid devices
sudo ./snapraid down

if ! make lcov_capture lcov_html; then
	exit 1
fi
