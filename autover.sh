#!/bin/sh
#

if [ -f .version ]; then
    # Get version from the .version file
    VERSION=$(cat .version)
fi

if [ -z "$VERSION" ] && [ -d .git ]; then
    # Get version from git tags, removing the 'v' prefix
    VERSION=$(git describe --match 'v*' 2>/dev/null | sed 's/^v//')
fi

if [ -z "$VERSION" ] && [ -d .git ]; then
    # Fall back to short commit hash
    VERSION=0-$(git rev-parse --short HEAD 2>/dev/null)
fi

if [ -z "$VERSION" ]; then
    # No version, but still use a number
    VERSION="0"
fi

# Apply common rules to all outputs:
# 1. Replace "rc-" with "rc"
# 2. Replace "beta-" with "beta"
# 3. Replace all remaining dashes with "+"
VERSION=$(echo "$VERSION" | sed 's/rc-/rc/g; s/beta-/beta/g; s/-/+/g')

printf '%s' "$VERSION"
