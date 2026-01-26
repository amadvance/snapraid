#!/bin/bash
#
# Script to manually build a Debian .deb package for snapraid on Slackware
# (using only dpkg-deb; no dpkg-buildpackage or debhelper needed)
#
# This avoids the /var/lib/dpkg/status issue entirely by:
# - Building the source directly
# - Installing into a temporary package root directory
# - Creating minimal DEBIAN/control manually
# - Packaging with dpkg-deb --root-owner-group
#

set -euo pipefail

VERSION="14.0"
TARBALL="snapraid-${VERSION}.tar.gz"
SOURCE_URL="https://github.com/amadvance/snapraid/releases/download/v${VERSION}/${TARBALL}"
PKG_NAME="snapraid"
PKG_ROOT="${PKG_NAME}-deb-root"
CONTROL_FILE="${PKG_ROOT}/DEBIAN/control"

# Prefer local tarball
if [[ -f "./${TARBALL}" ]]; then
  echo "Using local ${TARBALL}"
else
  echo "Downloading ${TARBALL}..."
  curl -L -O "$SOURCE_URL"
fi

# Clean previous builds
rm -rf "${PKG_ROOT}" snapraid-${VERSION}

# Extract source
echo "Extracting source..."
tar -xzf "${TARBALL}"

# Build the software
echo "Configuring and building snapraid..."
cd snapraid-${VERSION}
./configure --prefix=/usr --mandir=/usr/share/man
make

# Install into package root
echo "Installing into temporary package root..."
mkdir -p "../${PKG_ROOT}/usr"
make install DESTDIR="../${PKG_ROOT}"

# Add example config and docs
mkdir -p "../${PKG_ROOT}/usr/share/doc/snapraid"
cp snapraid.conf.example COPYING AUTHORS HISTORY README "../${PKG_ROOT}/usr/share/doc/snapraid/"

cd ..

# Create DEBIAN directory and control file
echo "Creating DEBIAN/control..."
mkdir -p "${PKG_ROOT}/DEBIAN"
cat > "$CONTROL_FILE" <<EOF
Package: snapraid
Version: ${VERSION}-1
Section: utils
Priority: optional
Architecture: amd64
Maintainer: Andrea Mazzoleni <amadvance@gmail.com>
Description: Disk array backup for many large rarely-changed files
 SnapRAID is a backup program for disk arrays. It stores parity
 information of your data and it's able to recover from up to six disk
 failures. SnapRAID is mainly targeted for a home media center, with a
 lot of big files that rarely change.
Homepage: https://www.snapraid.it/
License: GPL-3.0-or-later
EOF

# Build the .deb package
echo "Building .deb package with dpkg-deb..."
dpkg-deb --root-owner-group --build "${PKG_ROOT}" "${PKG_NAME}_${VERSION}-1_amd64.deb"

# Cleanup
rm -rf "${PKG_ROOT}" snapraid-${VERSION}

echo ""
echo "Build complete!"
echo "Package created: ${PKG_NAME}_${VERSION}-1_amd64.deb"
echo ""
echo "Note: This is a basic binary .deb package (no advanced features like scripts or full dependency checking)."
echo "To install on a Debian/Ubuntu system: sudo dpkg -i ${PKG_NAME}_${VERSION}-1_amd64.deb"
echo "All man pages (including localized ones) and the example config are included."
