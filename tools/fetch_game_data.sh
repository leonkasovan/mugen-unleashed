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
#   tools/fetch_game_data.sh --dest DIR    a tree somewhere other than install/
#
# The archive is cached in build/downloads/ like every other fetch, and is
# checked against a pinned SHA-256 before anything is unpacked. A tree that is
# already there -- including a real M.U.G.E.N installation -- is never touched.
#
# A download happens only when both of these miss, in this order:
#
#   1. $DEST/data/mugen.cfg already exists -> nothing is fetched or written.
#   2. build/downloads/$FILE already exists and matches the pinned SHA-256 ->
#      the cached copy is unpacked, no network access at all.
#
# Only when neither holds is the archive downloaded. There is no flag to
# suppress that: every build entry point is online, and a machine that is
# genuinely offline fails at the download rather than silently building against
# a tree it does not have.
#
# POSIX sh on purpose: port/psvita and port/android call it from bash, the
# Makefile and build.bat from the w64devkit's sh.exe.
set -eu

URL="${GAME_ASSETS_URL:-https://github.com/leonkasovan/mugen-unleashed/releases/download/toolchains-v1.0/mugen_elecbyte_assets.zip}"
SHA256="${GAME_ASSETS_SHA256:-440fb5046dae1965c0694c63796fdb6a274cdc8d36f3acb88e11c6e657b8d906}"
FILE="mugen_elecbyte_assets.zip"

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
DEST="$ROOT/install"
DOWNLOADS="$ROOT/build/downloads"
# Where a mismatched cached archive is kept instead of being deleted (see below).
REJECTED="$DOWNLOADS/$FILE.rejected"
SCRATCH="$ROOT/build/tmp"
STAGE="$SCRATCH/game-data-stage"
ARCHIVE="$DOWNLOADS/$FILE"

CHECK=0

say()  { printf '\n==> %s\n' "$*"; }
info() { printf '    %s\n' "$*"; }
fail() { printf '\nerror: %s\n' "$*" >&2; exit 1; }
usage() { awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --check) CHECK=1 ;;
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

# A M.U.G.E.N tree is recognised by the game's own files, not by data/mugen.cfg.
#
# Port fix: the config is the wrong witness. The engine writes its embedded
# default into a tree that lacks one as soon as it starts, so a single run --
# even one that then fails on the missing assets -- left a directory holding
# nothing but data/mugen.cfg that this script and every other entry point read
# as "game data present", and the download that would have fixed it was never
# retried. data/mugen.cfg is therefore neither necessary (a tree without one is
# complete, the engine supplies it) nor sufficient. TREE_FILES is the set KFM's
# own definition needs, which is exactly what the mode select opens.
TREE_FILES="data/system.def data/kfm.sff data/kfm.def data/kfm.snd"

tree_present()
{
    for f in $TREE_FILES; do
        # A *.def, .sff, .snd, and the *.st stages under chars/<name>/, so a
        # plain -f test per name is enough for the shipped tree.
        [ -f "$1/$f" ] && return 0
    done
    return 1
}

present() { tree_present "$DEST"; }

if present; then
    info "game data present: $DEST"
    exit 0
fi

if [ "$CHECK" = 1 ]; then
    printf 'game data missing: %s (no data/system.def, .sff or .snd)\n' "$DEST" >&2
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
    # A cached archive with the wrong digest is kept, renamed, rather than
    # deleted. It is often a freshly built asset whose pinned SHA-256 has not
    # been updated yet -- and deleting it there destroys the only copy of work
    # that cost a zip run.
    #
    # $REJECTED holds one file. A later failed download only takes that slot
    # when it is empty or holds identical bytes; otherwise it goes to
    # $REJECTED.downloaded, so a locally built archive is never overwritten by
    # the download that replaced it.
    if [ -f "$ARCHIVE" ]; then
        # stderr: these must be seen alongside the fail() below, whatever the
        # caller does with stdout.
        # Hash the archive while it is still where it is: the rename below
        # would leave hash_of reading a path that does not exist yet.
        _rejected_hash=$(hash_of "$ARCHIVE")
        mv -f "$ARCHIVE" "$REJECTED"
        printf '\n' >&2
        printf '    cached %s did not match the pinned sha256\n' "$FILE" >&2
        printf '    kept it as %s\n' "$REJECTED" >&2
        printf '      actual   %s\n' "$_rejected_hash" >&2
        printf '      expected %s\n' "$SHA256" >&2
        printf '      if it is the asset you built, re-pin with\n' >&2
        printf '        GAME_ASSETS_SHA256=%s\n' "$_rejected_hash" >&2
    fi
    # Nothing above this point touched the network: the destination tree was
    # absent and no verified archive was cached, so the only way forward is a
    # download. Offline builds are not supported on purpose.
    info "downloading $FILE (about 2.3 MB, 5 MB unpacked)"
    info "$URL"
    fetch "$URL" "$ARCHIVE"
    if [ "$(hash_of "$ARCHIVE")" != "$SHA256" ]; then
        # Keep the download aside too, but never over $REJECTED: that slot may
        # already hold a locally built archive, which is the one worth keeping.
        # Same contents collapse to one file; different contents get a suffix.
        _failed_hash=$(hash_of "$ARCHIVE")
        if [ ! -f "$REJECTED" ]; then
            mv -f "$ARCHIVE" "$REJECTED"
            _kept="$REJECTED"
        elif [ "$(hash_of "$REJECTED")" = "$_failed_hash" ]; then
            rm -f "$ARCHIVE"
            _kept="$REJECTED (same contents)"
        else
            _kept="$REJECTED.downloaded"
            mv -f "$ARCHIVE" "$_kept"
        fi
        fail "$FILE failed its SHA-256; nothing was unpacked
       expected $SHA256
       (a re-published asset needs GAME_ASSETS_SHA256=<new hash>)
       the download is kept as $_kept"
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
[ -f "$STAGE/data/system.def" ] && [ -f "$STAGE/data/mugen.cfg" ] ||
    { rm -rf "$STAGE"; fail "$FILE does not look like a M.U.G.E.N tree (no data/system.def)"; }

# Never overwrite a file the player owns. data/mugen.cfg is rewritten in place by
# the options screen and the key/joystick bindings, so a tree that already has
# one keeps it -- the same rule every other entry point follows (src/configembed.c
# extracts only a missing file, and the Android stager skips the tree's copy).
#
# Port fix: the config is now held back from the copy rather than restored after
# it. Backing it up and copying the backup back only survives a copy that
# finishes; when the copy failed on any *other* path the live file was left
# holding the archive's value, so a failed restore discarded the player's
# settings as its last act. Moving the archive's copy aside first means the
# player's file is never a temporary the copy can overwrite, whatever happens.
mkdir -p "$DEST"
ARCHIVE_CFG="$STAGE/data/mugen.cfg"
if [ -f "$DEST/data/mugen.cfg" ]; then
    CFG_INCOMING="$SCRATCH/game-data-mugen.cfg.incoming"
    mkdir -p "$SCRATCH"
    mv "$ARCHIVE_CFG" "$CFG_INCOMING" ||
        { rm -rf "$STAGE"; fail "cannot set aside $ARCHIVE_CFG"; }
fi
cp -R "$STAGE"/. "$DEST"/ || { rm -rf "$STAGE"; fail "cannot copy the tree into $DEST"; }
if [ -f "$DEST/data/mugen.cfg" ]; then
    info "kept the existing $DEST/data/mugen.cfg (the player's settings)"
    info "the archive's copy is at data/mugen.cfg.incoming"
    cp "$CFG_INCOMING" "$DEST/data/mugen.cfg.incoming"
    rm -f "$CFG_INCOMING"
fi
COUNT=$(find "$DEST" -type f | wc -l | tr -d '[:space:]')
rm -rf "$STAGE"
info "unpacked into $DEST"
info "$COUNT file(s): $(cd "$DEST" && ls | tr '\n' ' ')"
info "the tree in the archive is Elecbyte's own 1.1b1 data; add characters and"
info "stages under chars/ and stages/ as usual."