#!/bin/sh
#
echo "Generating build information using autoreconf"

# All is done by autoreconf
autoreconf -f -i

# Run configure for this platform
echo "Now you are ready to run ./configure"

# Remove backup files created by autoreconf
rm -f config.guess~ config.h.in~ config.sub~ configure~ install-sh~
