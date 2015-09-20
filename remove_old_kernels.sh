#!/bin/sh
VERSIONS_AVAILABLE=`dpkg -l linux-image* | sed -n 's/^ii  linux-image-\([0-9.-]*\)-generic.*$/\1/p' | sort -n`
VERSIONS_TO_REMOVE=`echo "$VERSIONS_AVAILABLE" | head -n '-1'`

echo "Available versions:"
echo "$VERSIONS_AVAILABLE"

PKG_NAMES=''
for v in $VERSIONS_TO_REMOVE; do
  PKG_NAMES="$PKG_NAMES linux-image-$v-generic linux-image-extra-$v-generic linux-headers-$v linux-headers-$v-generic"
done
sudo apt-get remove $PKG_NAMES
