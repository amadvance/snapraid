#!/bin/sh

echo sha256 > CHECKSUMS
cd archive && sha256sum * >> ../CHECKSUMS

