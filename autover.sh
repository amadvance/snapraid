#!/bin/sh
#

SRCDIR="${srcdir:-$(dirname "$0")}"

if [ -f "$SRCDIR/.version" ]; then
    # Get version from the .version file
    VERSION=$(cat "$SRCDIR/.version")
fi

if [ -z "$VERSION" ] && [ -e "$SRCDIR/.git" ]; then
    # Get version from git tags, removing the 'v' prefix
    VERSION=$(git -C "$SRCDIR" describe --match 'v*' 2>/dev/null | sed 's/^v//')
fi

if [ -z "$VERSION" ] && [ -e "$SRCDIR/.git" ]; then
    # Fall back to short commit hash
    VERSION=0-$(git -C "$SRCDIR" rev-parse --short HEAD 2>/dev/null)
fi

if [ -z "$VERSION" ]; then
    # No version, but still use a number
    VERSION="0"
fi

# Apply common rules to all outputs:
# - Remove "-" at the end
# - Replace "rc-" with "rc"
# - Replace "beta-" with "beta"
# - Replace all remaining dashes with "."
VERSION=$(echo "$VERSION" | sed 's/-$//; s/rc-/rc/g; s/beta-/beta/g; s/-/./g')

printf '%s' "$VERSION"
