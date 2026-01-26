#!/bin/bash
#
# Script to build a Slackware .txz package for snapraid
# using the snapraid.SlackBuild and related files in the current directory.
#
# This version:
# - Prefers a source tarball already present in the current directory
# - Only downloads it if not found locally
# - Runs the SlackBuild (as root recommended for makepkg)
#
# Requirements:
# - Run as root (or with sudo) for proper ownership/permissions in package

set -euo pipefail

VERSION="14.0"
TARBALL="snapraid-${VERSION}.tar.gz"
SOURCE_URL="https://github.com/amadvance/snapraid/releases/download/v${VERSION}/${TARBALL}"

if [[ ! -f "snapraid.SlackBuild" ]]; then
  echo "Error: snapraid.SlackBuild not found in the current directory."
  exit 1
fi

if [[ ! -f "slack-desc" ]]; then
  echo "Error: slack-desc not found in the current directory."
  exit 1
fi

# Prefer local tarball
if [[ -f "./${TARBALL}" ]]; then
  echo "Found ${TARBALL} locally."
else
  echo "Downloading ${TARBALL}..."
  curl -L -O "$SOURCE_URL"
fi

# Set environment variables for SlackBuild
export VERSION BUILD=1 TAG=_AM OUTPUT=$(pwd)  # Package output in current dir

echo "Running SlackBuild..."
bash snapraid.SlackBuild

echo ""
echo "Build complete!"
echo "The .txz package should be in the current directory:"
ls snapraid-${VERSION}-*-*.txz

echo ""
echo "You can install it with:"
echo "  installpkg snapraid-${VERSION}-*-*.txz"
echo "  or upgradepkg if replacing an older version."
