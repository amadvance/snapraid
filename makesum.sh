#!/bin/sh

echo sha256 > CHECKSUMS
cd archive && sha256sum * | sort -k 2 -V >> ../CHECKSUMS

