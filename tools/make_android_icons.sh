#!/usr/bin/env bash
# =============================================================================
# make_android_icons.sh — derive the launcher icons from res/icons/*.ico
#
#   tools/make_android_icons.sh <out-res-dir> [ico]
#
#     tools/make_android_icons.sh port/android/app/build/generated/res/mugen-icons
#
# Writes, under <out-res-dir>:
#
#   mipmap-mdpi/ic_launcher.png            48x48      legacy icon, one per
#   mipmap-hdpi/ic_launcher.png            72x72      density bucket Android
#   mipmap-xhdpi/ic_launcher.png           96x96      picks from
#   mipmap-xxhdpi/ic_launcher.png         144x144
#   mipmap-xxxhdpi/ic_launcher.png        192x192
#
#   mipmap-<density>/ic_launcher_foreground.png   the adaptive icon's foreground:
#       a 108dp canvas at each density (108, 162, 216, 324, 432 px) with the
#       artwork confined to the 72dp safe zone -- 66% of the canvas -- because
#       Android 8+ masks an adaptive icon to the launcher's own shape and crops
#       whatever falls outside that box.
#
# Both come from res/icons/1.ico, the 64x64 32-bpp entry: it is the largest size
# in the set and the only one that carries an alpha channel, and Android scales
# up from it. The Android build regenerates these every run, so res/icons stays
# the single source of the artwork; the copies committed under
# app/src/main/res/mipmap-*/ are what a build without Python uses (Gradle gives
# the generated directory priority, so they never mix).
#
# Only the Python standard library is involved (tools/iconpack.py decodes the
# .ico), which is why there is no imaging package to install.
# =============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

[ $# -ge 1 ] || { sed -n '3,6p' "$0" | sed 's/^# \{0,1\}//' >&2; exit 2; }
OUT="$1"
ICO="${2:-$ROOT/res/icons/1.ico}"

[ -f "$ICO" ] || { printf 'error: no such icon: %s\n' "$ICO" >&2; exit 1; }

# Windows' 'python3' is usually the Microsoft Store alias, which exists and
# prints an advert instead of running, so every candidate is asked to run.
PY=""
for candidate in python3 python py; do
    if command -v "$candidate" >/dev/null 2>&1 &&
       "$candidate" -c "pass" >/dev/null 2>&1; then
        PY="$candidate"
        break
    fi
done
[ -n "$PY" ] || { printf 'error: no working Python; res/icons cannot be decoded\n' >&2; exit 1; }

# density:legacy-size:foreground-size (foreground = 108dp at that density)
BUCKETS="mdpi:48:108 hdpi:72:162 xhdpi:96:216 xxhdpi:144:324 xxxhdpi:192:432"

# Only the directories this script owns are cleared. `rm -rf "$OUT"` would be
# catastrophic when OUT is the tracked resource directory (the way the committed
# fallbacks are refreshed), and the script is meant to be safe to point there.
for bucket in $BUCKETS; do
    density="${bucket%%:*}"
    rest="${bucket#*:}"
    legacy="${rest%%:*}"
    foreground="${rest##*:}"
    dir="$OUT/mipmap-$density"
    rm -rf "$dir"
    mkdir -p "$dir"
    "$PY" "$HERE/iconpack.py" png "$dir/ic_launcher.png" "$ICO" "$legacy"
    # 66% of 108dp is the 72dp safe zone: a round launcher mask keeps a circle
    # of that diameter, so nothing outside it is guaranteed to survive.
    "$PY" "$HERE/iconpack.py" canvas "$dir/ic_launcher_foreground.png" "$ICO" \
          "$foreground" "$foreground" 66 none
done
