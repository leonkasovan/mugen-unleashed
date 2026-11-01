#!/bin/sh
# Fetches the pinned vitaGL source for the PS Vita port.
#
# The upstream tree is not committed (see third_party/vitagl/VENDORED.md); this
# restores it at the exact commit the port was validated against, so a build is
# reproducible rather than "whatever master was that day".
#
# Usage:  tools/psvita/fetch_vitagl.sh [--force]
set -eu

PIN=9bf3e3f4e55efd1f4a191b179144f7f9a98cd925
URL=https://github.com/Rinnegatamante/vitaGL.git

here=$(cd "$(dirname "$0")/../.." && pwd)
dest="$here/third_party/vitagl/src"

if [ -e "$dest/source/vgl.c" ] && [ "${1:-}" != "--force" ]; then
    echo "vitaGL already present at $dest (use --force to refetch)"
    exit 0
fi

tmp="$here/build/psvita/vitagl-fetch"
rm -rf "$tmp"
mkdir -p "$(dirname "$tmp")"

echo "fetching vitaGL $PIN"
git clone --quiet "$URL" "$tmp"
git -C "$tmp" checkout --quiet "$PIN"

rm -rf "$dest"
mkdir -p "$(dirname "$dest")"
mv "$tmp" "$dest"
rm -rf "$dest/.git" "$dest/samples" "$dest/.github"

echo "vitaGL source ready at $dest"
echo "  commit: $PIN"
echo "  licence: LGPL, see $dest/COPYING.LESSER"
