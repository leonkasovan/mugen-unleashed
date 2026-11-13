#!/bin/sh
# Fetch the default M.U.G.E.N 1.1b1 game tree into install/ when there is none.
#
# Every build entry point needs the tree: `make install` puts the binary into it,
# the PS Vita VPK packs it as /data, the Android build stages it as assets, and
# the log-comparison tests run the binaries inside it. Elecbyte's files are not
# in this repository, so a fresh clone has nothing to run against; this fetches
# the project's own packaged copy of them instead of asking the user to find a
# 2013 installer.
#
#   tools/fetch_game_data.sh               fetch into install/ if it is missing
#   tools/fetch_game_data.sh --check       report only, exit 1 when missing
#   tools/fetch_game_data.sh --offline     never download (a cached archive is used)
#   tools/fetch_game_data.sh --dest DIR    a tree somewhere other than install/
#
# The archive is cached in build/downloads/ like every other fetch, and is
# checked against a pinned SHA-256 before anything is unpacked. A tree that is
# already there -- including a real M.U.G.E.N installation -- is never touched.
#
# POSIX sh on purpose: port/psvita and port/android call it from bash, the
# Makefile and setup.bat from the w64devkit's sh.exe.
set -eu

URL="${GAME_ASSETS_URL:-https://github.com/leonkasovan/mugen-unleashed/releases/download/toolchains-v1.0/mugen_elecbyte_assets.zip}"
SHA256="${GAME_ASSETS_SHA256:-68f96ddb87c799f4b38d64a1a59e526455334bb7fc9ebb2c66ba11a27cdc92dd}"
FILE="mugen_elecbyte_assets.zip"

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
DEST="$ROOT/install"
DOWNLOADS="$ROOT/build/downloads"
SCRATCH="$ROOT/build/tmp"
STAGE="$SCRATCH/game-data-stage"
ARCHIVE="$DOWNLOADS/$FILE"

CHECK=0
OFFLINE=0

say()  { printf '\n==> %s\n' "$*"; }
info() { printf '    %s\n' "$*"; }
fail() { printf '\nerror: %s\n' "$*" >&2; exit 1; }
usage() { awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --check) CHECK=1 ;;
        --offline) OFFLINE=1 ;;
        --dest) DEST="${2:-}"; [ -n "$DEST" ] || fail "--dest requires a directory"; shift ;;
        --dest=*) DEST="${1#*=}" ;;
        --help|-h) usage; exit 0 ;;
        *) fail "unknown argument: $1 (--help)" ;;
    esac
    shift
done

case "$DEST" in
    /*|[A-Za-z]:*) ;;
    *) DEST="$ROOT/$DEST" ;;
esac

# A M.U.G.E.N tree is recognised by the file the engine opens first.
present() { [ -f "$DEST/data/mugen.cfg" ]; }

if present; then
    info "game data present: $DEST"
    exit 0
fi

if [ "$CHECK" = 1 ]; then
    printf 'game data missing: %s/data/mugen.cfg\n' "$DEST" >&2
    exit 1
fi

hash_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    elif command -v openssl >/dev/null 2>&1; then
        openssl dgst -sha256 "$1" | awk '{print $NF}'
    else
        fail "no SHA-256 verifier (install sha256sum, shasum or openssl)"
    fi
}

fetch() {
    command -v curl >/dev/null 2>&1 ||
        fail "curl is required to download $FILE (https://curl.se/download.html)"
    case "$1" in https://*) ;; *) fail "refusing non-HTTPS download: $1" ;; esac
    if ! curl --fail --location --silent --show-error --proto '=https' \
            --retry 2 --connect-timeout 20 --max-time 3600 \
            --output "$2.part" "$1"; then
        rm -f "$2.part"
        fail "download failed: $1; rerun to retry, or put the archive at $ARCHIVE"
    fi
    mv -f "$2.part" "$2"
}

say "Default M.U.G.E.N assets ($FILE)"
mkdir -p "$DOWNLOADS"

if [ -f "$ARCHIVE" ] && [ "$(hash_of "$ARCHIVE")" = "$SHA256" ]; then
    info "cached download verified (sha256)"
else
    rm -f "$ARCHIVE"
    if [ "$OFFLINE" = 1 ]; then
        fail "--offline and $FILE is not cached
       expected $ARCHIVE with sha256 $SHA256"
    fi
    info "downloading $FILE (about 2.3 MB, 5 MB unpacked)"
    info "$URL"
    fetch "$URL" "$ARCHIVE"
    if [ "$(hash_of "$ARCHIVE")" != "$SHA256" ]; then
        rm -f "$ARCHIVE"
        fail "$FILE failed its SHA-256; nothing was unpacked
       expected $SHA256
       (a re-published asset needs GAME_ASSETS_SHA256=<new hash>)"
    fi
    info "sha256 verified"
fi

rm -rf "$STAGE"
mkdir -p "$STAGE"
if command -v unzip >/dev/null 2>&1; then
    unzip -q -o "$ARCHIVE" -d "$STAGE" || fail "cannot unpack $ARCHIVE"
else
    # w64devkit's tar is bsdtar, which reads zip; GNU tar does not. Try, then
    # name the dependency rather than leaving half an extraction behind.
    tar -xf "$ARCHIVE" -C "$STAGE" 2>/dev/null ||
        { rm -rf "$STAGE"; fail "unzip is required to unpack $ARCHIVE (MSYS2: pacman -S unzip)"; }
fi
[ -f "$STAGE/data/mugen.cfg" ] && [ -f "$STAGE/data/system.def" ] ||
    { rm -rf "$STAGE"; fail "$FILE does not look like a M.U.G.E.N tree (no data/mugen.cfg)"; }

mkdir -p "$DEST"
cp -R "$STAGE"/. "$DEST"/ || { rm -rf "$STAGE"; fail "cannot copy the tree into $DEST"; }
COUNT=$(find "$DEST" -type f | wc -l | tr -d '[:space:]')
rm -rf "$STAGE"
info "unpacked into $DEST"
info "$COUNT file(s): $(cd "$DEST" && ls | tr '\n' ' ')"
info "the tree in the archive is Elecbyte's own 1.1b1 data; add characters and"
info "stages under chars/ and stages/ as usual."