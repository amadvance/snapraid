#!/bin/sh
#
echo "Generating build information using aclocal, automake and autoconf"
echo "This may take a while ..."

# Touch the timestamps on all the files since CVS messes them up
touch configure.ac

# Touch documentation to avoid it's recreation
touch snapraid.1
touch snapraid.txt

# Regenerate configuration files
aclocal
automake --add-missing --force-missing
autoconf
autoheader && touch config.h

# Run configure for this platform
echo "Now you are ready to run ./configure"

