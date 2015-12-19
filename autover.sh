#!/bin/sh
#

if [ -d .git ]; then
    # Get version from git tags, removing the 'v' prefix
    VERSION=`git describe --match 'v*' | sed 's/^v//'`
elif [ -f .version ]; then
    # Get version from the .version file
    VERSION=`cat .version`
else
    VERSION="none"
fi

printf '%s' "$VERSION"
