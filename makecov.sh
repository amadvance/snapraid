#!/bin/sh
#
# Run the Coverage test
#

# Ask root permissions at the start
sudo echo We are root

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

# Root test for Btrfs
sudo make btrfscheck

# Run commands on a real array to test smartctl
sudo ./snapraid probe
sudo ./snapraid up
sudo ./snapraid smart
sudo ./snapraid probe
sudo ./snapraid devices
sudo ./snapraid down
sudo ./snapraid test-downifup

if ! make lcov_capture lcov_html; then
	exit 1
fi

# Remove root generated files
sudo make clean
