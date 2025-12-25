#!/bin/bash
# build-snapraid-rpm.sh
#
# Script to build both binary and source RPMs for snapraid
# using the snapraid-rpm.spec file in the current directory.
#
# This version:
# - Works without rpmdevtools
# - Prefers a source tarball already present in the current directory
# - Only downloads it if not found locally
#
# Requirements:
# - rpmbuild available
# - gcc and make installed (for building snapraid)
#
# Usage:
#   1. Place this script and your snapraid-rpm.spec (and optionally the tarball) in the same directory.
#   2. Make it executable: chmod +x build-snapraid-rpm.sh
#   3. Run it: ./build-snapraid-rpm.sh

set -euo pipefail

SPEC_FILE="snapraid-rpm.spec"

if [[ ! -f "$SPEC_FILE" ]]; then
  echo "Error: $SPEC_FILE not found in the current directory."
  echo "Please run this script from the directory containing your $SPEC_FILE"
  exit 1
fi

echo "Reading Version from $SPEC_FILE..."
VERSION=$(grep '^Version:' "$SPEC_FILE" | awk '{print $2}' | tr -d ' ')
if [[ -z "$VERSION" ]]; then
  echo "Error: Could not determine Version from $SPEC_FILE"
  exit 1
fi

echo "SnapRAID version: $VERSION"

TARBALL="snapraid-${VERSION}.tar.gz"
SOURCE_URL="https://github.com/amadvance/snapraid/releases/download/v${VERSION}/${TARBALL}"

# Set up rpmbuild tree manually in ~/rpmbuild if it doesn't exist
RPMBUILD_DIR="$HOME/rpmbuild"
if [[ ! -d "$RPMBUILD_DIR/SPECS" ]]; then
  echo "Setting up rpmbuild directory structure in $RPMBUILD_DIR..."
  mkdir -p "$RPMBUILD_DIR"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
else
  echo "rpmbuild directory structure already exists in $RPMBUILD_DIR"
fi

# Look for the tarball first in the current directory, then in ~/rpmbuild/SOURCES
if [[ -f "./$TARBALL" ]]; then
  echo "Found $TARBALL in the current directory. Copying to ~/rpmbuild/SOURCES..."
  cp "./$TARBALL" "$RPMBUILD_DIR/SOURCES/"
elif [[ ! -f "$RPMBUILD_DIR/SOURCES/$TARBALL" ]]; then
  echo "Downloading $TARBALL (not found locally or in ~/rpmbuild/SOURCES)..."
  curl -L -o "$RPMBUILD_DIR/SOURCES/$TARBALL" "$SOURCE_URL"
else
  echo "$TARBALL already present in ~/rpmbuild/SOURCES"
fi

# Copy spec file to SPECS directory (preserving the original filename)
cp "$SPEC_FILE" "$RPMBUILD_DIR/SPECS/"

echo "Building RPMs using rpmbuild -ba..."
rpmbuild -ba --nodeps --nocheck "$RPMBUILD_DIR/SPECS/$SPEC_FILE"

echo ""
echo "Build complete!"
echo "Binary RPM(s) can be found in:"
find "$RPMBUILD_DIR/RPMS" -name "snapraid-${VERSION}*.rpm" -exec echo "  {}" \;
echo ""
echo "Source RPM can be found in:"
find "$RPMBUILD_DIR/SRPMS" -name "snapraid-${VERSION}*.src.rpm" -exec echo "  {}" \;

echo ""
echo "You can install the binary RPM with:"
echo "  sudo dnf install $RPMBUILD_DIR/RPMS/*/snapraid-${VERSION}*.rpm   # Fedora"
echo "  sudo rpm -ivh $RPMBUILD_DIR/RPMS/*/snapraid-${VERSION}*.rpm      # Others"
