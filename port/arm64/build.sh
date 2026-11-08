#!/usr/bin/env bash
# Build mugen-unleashed for Linux arm64 from a Windows host (MSYS2/Git Bash),
# using Arm's GNU toolchain. No arm64 machine and no chroot needed.
#
#   port/arm64/build.sh              download anything missing, then build
#   port/arm64/build.sh --setup      download the toolchain, then stop
#   port/arm64/build.sh --check-tools  report what is present, build nothing
#   port/arm64/build.sh --sysroot    re-download the dependency tree, then build
#   port/arm64/build.sh --clean      remove this port's build output
#   port/arm64/build.sh --no-sysroot  use the existing tree as-is (do not check)
#   port/arm64/build.sh --offline    fail instead of downloading anything
#   port/arm64/build.sh --jobs N     parallel make jobs (default: CPU count)
#   port/arm64/build.sh --gles 0|1   renderer: 1 = GLES 3.0 (the default for
#                                    linux-arm64), 0 = desktop GL
#   port/arm64/build.sh --upload     build, then copy the binary to the device
#                                    (DEVICE, default ark@192.168.7.2, into
#                                    DEVICE_DIR, default ~/mugen)
#
# Which toolchain: ARM64_TOOLCHAIN=9.2 (default) or 15.3. The choice is set by
# the *device's glibc*, since a binary cannot require a newer glibc than the
# device has:
#
#   ARM64_TOOLCHAIN=9.2    gcc 9.2.1,  glibc 2.30  <- RG351MP/ArkOS, verified
#   ARM64_TOOLCHAIN=15.3   gcc 15.3.1, glibc 2.42  <- a modern distro
#
# Check the device with `ldd --version` (or ssh: `ldd --version | head -1`).
# A mismatch is not subtle: the loader refuses the binary with
# "version `GLIBC_2.38' not found" before any engine code runs.
#
# Two things are needed and both are handled here:
#
#   1. the aarch64-none-linux-gnu toolchain, installed into TOOLCHAIN_DIR
#      (default C:/arm-gnu-toolchain). --setup downloads Arm's official archive
#      and verifies it against the checksum Arm publishes.
#   2. a dependency tree (SDL2, SDL2_mixer, SDL2_ttf, zlib, libpng, FreeType as
#      shared objects, plus hand-written .pc files). The toolchain ships no SDL,
#      the target has no sysroot to link against, and building the whole stack
#      with a cross compiler is a project of its own -- so the tree is published
#      as a release asset (arm64-sysroot.zip) and downloaded here. It is
#      unpacked into build/tmp/arm64-sysroot/ and verified against a SHA-256
#      pinned in this script; ARM64_SYSROOT_URL / ARM64_SYSROOT_SHA256 override
#      both, for a fork or a re-published asset.
#
#      The tree's contents are the link-time closure of the six libraries the
#      engine links (see docs/BUILD-LINUX.md). Its usr/lib/rpath/ is not a
#      runtime search path: the finished binary has no RPATH.
#
# Output: build/linux-arm64-aarch64-none-linux-gnu-sdl2/
#           mugen-linux-arm64-aarch64-none-linux-gnu
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$ROOT/build/linux-arm64-aarch64-none-linux-gnu-sdl2"

# ---------------------------------------------------------------- toolchains
#
# Which toolchain to use is decided by the *target's glibc*, because a binary
# cannot require a newer glibc than the device has. Each release pins one:
#
#   gcc-arm-9.2-2019.12   glibc 2.30   <- what the RG351MP/ArkOS has
#   arm-gnu-toolchain-15.3.rel2  glibc 2.42  <- current, needs a modern distro
#
# Verified on an RG351MP (ArkOS, `ldd` = "Ubuntu GLIBC 2.30-0ubuntu2.2"): the
# 15.3 build is rejected by the loader with "version `GLIBC_2.38' not found",
# the 9.2 build runs with the GLES renderer on the Mali-G31. So 9.2 is the
# default here, and 15.3 is the one you pick deliberately for a new target.
#
#   ARM64_TOOLCHAIN=9.2    (default) gcc 9.2.1, glibc 2.30
#   ARM64_TOOLCHAIN=15.3   gcc 15.3.1, glibc 2.42
#
# Both are Windows-host (Mingw-w64) builds from Arm and both are verified
# against a checksum Arm publishes before anything is extracted.
TOOLCHAIN_KIND="${ARM64_TOOLCHAIN:-9.2}"
case "$TOOLCHAIN_KIND" in
    9.2|2019.12)
        # The last release that ships glibc 2.30. Older releases are not on
        # GitLab (which starts at 11.2); they live on developer.arm.com behind
        # a redirect to armkeil.blob.core.windows.net.
        TOOLCHAIN_VER=9.2-2019.12
        TOOLCHAIN_TAG=9.2
        TOOLCHAIN_GLIBC=2.30
        TOOLCHAIN_FILE="gcc-arm-${TOOLCHAIN_VER}-mingw-w64-i686-aarch64-none-linux-gnu.tar.xz"
        TOOLCHAIN_URL="https://developer.arm.com/-/media/Files/downloads/gnu-a/${TOOLCHAIN_VER}/binrel/${TOOLCHAIN_FILE}"
        # Arm publishes MD5 for this series (a 32-hex .sha256asc beside the
        # archive, despite the name). Verified as MD5, which is what it is.
        TOOLCHAIN_HASH_KIND=md5
        TOOLCHAIN_HASH=a004a0ab54d7e46ae8bab60297dcda3d
        ;;
    15.3|15.3.rel2)
        TOOLCHAIN_VER=15.3.rel2
        TOOLCHAIN_TAG=15.3
        TOOLCHAIN_GLIBC=2.42
        TOOLCHAIN_FILE="arm-gnu-toolchain-${TOOLCHAIN_VER}-mingw-w64-x86_64-aarch64-none-linux-gnu.zip"
        # Note %2F: the same URL with a literal "/" in the project path is 404.
        TOOLCHAIN_URL="https://gitlab.arm.com/api/v4/projects/tooling%2Fgnu-toolchains-for-arm/packages/generic/gnu-toolchain/${TOOLCHAIN_VER}/${TOOLCHAIN_FILE}"
        TOOLCHAIN_HASH_KIND=sha256
        TOOLCHAIN_HASH=dd6bce7fef2724e510e1ee5880dea3dc037fd677a71c1fa094319b4d102850d2
        ;;
    *)  fail "unknown ARM64_TOOLCHAIN=$TOOLCHAIN_KIND (use 9.2 or 15.3)" ;;
esac

# TOOLCHAIN_DIR must end up holding bin/aarch64-none-linux-gnu-gcc.exe. The two
# archives differ in shape -- 15.3 unpacks its tree directly, 9.2 wraps it in a
# gcc-arm-<version>-... directory -- so install_toolchain() flattens that.
#
# The default carries the toolchain version in its name: one directory per
# ARM64_TOOLCHAIN. A single shared C:/arm-gnu-toolchain means switching the
# version has to destroy whatever is already installed there, and a tree put
# there by hand (no marker file) is indistinguishable from one --setup made --
# so the default would have offered to wipe a perfectly good 15.3 install to
# make room for the 9.2 one. Versioned directories cannot collide, and the two
# can be kept side by side.
TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-C:/arm-gnu-toolchain-${TOOLCHAIN_TAG}}"

CROSS=aarch64-none-linux-gnu-
XSYSROOT="$ROOT/build/tmp/arm64-sysroot"
DOWNLOADS="$ROOT/build/tmp/arm64-downloads"

# The dependency tree. A release asset rather than something assembled on the
# build machine: it is the same 39 MB of headers, shared objects and .pc files
# for every host, and producing it needs an aarch64 root filesystem that most
# checkouts do not have. The SHA-256 is pinned so a changed or truncated
# download cannot silently become the tree that gets linked; both values can be
# overridden when a fork publishes its own copy:
#
#   ARM64_SYSROOT_URL=https://... ARM64_SYSROOT_SHA256=<hex> port/arm64/build.sh
SYSROOT_URL="${ARM64_SYSROOT_URL:-https://github.com/leonkasovan/mugen-unleashed/releases/download/toolchains-v1.0/arm64-sysroot.zip}"
SYSROOT_FILE="arm64-sysroot.zip"
SYSROOT_SHA256="${ARM64_SYSROOT_SHA256:-16bbbf25b83b770f92ab478e330ea9eca0351c1f5792037ba958f02bece65049}"

# Device for --upload. The password is not a variable here on purpose: it is
# read from DEVICE_PASS, or prompted for, and only ever handed to sshpass on
# stdin-free form (sshpass -p) for the duration of one command.
DEVICE="${DEVICE:-ark@192.168.7.2}"
DEVICE_DIR="${DEVICE_DIR:-mugen}"
DEVICE_BIN="${DEVICE_BIN:-mugen-linux-arm64}"

DO_SETUP=0
DO_UPLOAD=0
KEEP_OLD=1
CHECK_TOOLS=0
DO_SYSROOT=0
SKIP_SYSROOT=0
CLEAN=0
OFFLINE=0
JOBS=""
GLES=""

say()  { printf '\n==> %s\n' "$*"; }
info() { printf '    %s\n' "$*"; }
fail() { printf '\nerror: %s\n' "$*" >&2; exit 1; }
usage() {
  awk 'NR>1 && /^#/ {sub(/^# ?/,""); print; next} NR>1 {exit}' "$0"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --setup) DO_SETUP=1; CHECK_TOOLS=1 ;;
        --check-tools) CHECK_TOOLS=1 ;;
        --sysroot) DO_SYSROOT=1 ;;
        --no-sysroot) SKIP_SYSROOT=1 ;;
        --clean) CLEAN=1 ;;
        --offline) OFFLINE=1 ;;
        --jobs) JOBS="${2:-}"; shift ;;
        --jobs=*) JOBS="${1#*=}" ;;
        --gles) GLES="${2:-}"; shift ;;
        --gles=*) GLES="${1#*=}" ;;
        --upload) DO_UPLOAD=1 ;;
        --no-keep-old) KEEP_OLD=0 ;;
        --device) DEVICE="${2:-}"; shift ;;
        --device=*) DEVICE="${1#*=}" ;;
        --device-dir) DEVICE_DIR="${2:-}"; shift ;;
        --device-dir=*) DEVICE_DIR="${1#*=}" ;;
        --help|-h) usage; exit 0 ;;
        *) printf 'unknown argument: %s (--help)\n' "$1" >&2; exit 2 ;;
    esac
    shift
done

if [ "$OFFLINE" = 1 ] && { [ "$DO_SETUP" = 1 ] || [ "$DO_SYSROOT" = 1 ]; }; then
    fail "--offline cannot be combined with --setup or --sysroot"
fi

# ---------------------------------------------------------------- platform
# WSL runs the toolchain's Linux binaries; it cannot use the Mingw-w64 build.
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*|Windows_NT) HOST_FAMILY=windows ;;
    Linux)
        if grep -qi microsoft /proc/version 2>/dev/null; then
            HOST_FAMILY=wsl
        else
            HOST_FAMILY=linux
        fi ;;
    *) fail "unsupported host $(uname -s); this port targets Windows (MSYS2) and WSL" ;;
esac

if [ "$HOST_FAMILY" = wsl ]; then
    fail "run this from Windows (MSYS2/Git Bash), not from inside WSL.
       WSL cannot execute the Mingw-w64 toolchain. Build natively instead:
         make MUGEN_TARGET=linux-arm64
       only the cross build (this script) needs the script."
fi

shell_path() {                 # POSIX form of a user-supplied path
    if [ -z "$1" ]; then printf '\n'; return 0; fi
    if command -v cygpath >/dev/null 2>&1; then cygpath -u "$1"; else printf '%s\n' "$1"; fi
}

TOOLCHAIN_DIR=$(shell_path "$TOOLCHAIN_DIR")
CC_BIN="$TOOLCHAIN_DIR/bin/${CROSS}gcc"

# --------------------------------------------------------- toolchain --setup
fetch() {                      # fetch <url> <dest>
    command -v curl >/dev/null 2>&1 ||
        fail "curl is required to download the toolchain (https://curl.se/download.html)"
    case "$1" in https://*) ;; *) fail "refusing non-HTTPS download: $1" ;; esac
    if ! curl --fail --location --silent --show-error --proto '=https' \
            --retry 2 --connect-timeout 20 --max-time 1800 \
            --output "$2.part" "$1"; then
        rm -f "$2.part"
        fail "download failed: $1; rerun --setup to retry"
    fi
    mv -f "$2.part" "$2"
}

hash_of() {                    # hash_of <sha256|md5> <file> -> hex digest
    local alg="$1" file="$2"
    if command -v "${alg}sum" >/dev/null 2>&1; then
        "${alg}sum" "$file" | awk '{print $1}'
    elif [ "$alg" = sha256 ] && command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$file" | awk '{print $1}'
    elif command -v openssl >/dev/null 2>&1; then
        # shasum -a 1 is SHA-1, not MD5. Preserve the requested algorithm.
        openssl dgst "-$alg" "$file" | awk '{print $NF}'
    else
        fail "no verifier for $alg (install ${alg}sum or openssl)"
    fi
}

install_toolchain() {
    local archive dest stage top
    say "Arm GNU Toolchain $TOOLCHAIN_VER (glibc $TOOLCHAIN_GLIBC, $TOOLCHAIN_HASH_KIND)"

    # An installed toolchain is recorded in a marker file rather than inferred
    # from "the directory exists": a half-extracted tree is the one failure that
    # breaks every later run, and it leaves the directory in place.
    if [ -f "$TOOLCHAIN_DIR/.mugen-toolchain-${TOOLCHAIN_VER}" ] && [ -x "$CC_BIN" ]; then
        info "already installed in $TOOLCHAIN_DIR"
        return 0
    fi
    [ "$OFFLINE" = 1 ] && { [ -x "$CC_BIN" ] || fail "--offline: toolchain $TOOLCHAIN_VER not installed in $TOOLCHAIN_DIR"; return 0; }

    # Port fix: do not alter an installed tree until the replacement has
    # passed checksum, extraction and compiler-layout validation.
    mkdir -p "$DOWNLOADS"
    archive="$DOWNLOADS/$TOOLCHAIN_FILE"

    if [ -f "$archive" ] && [ "$(hash_of "$TOOLCHAIN_HASH_KIND" "$archive")" = "$TOOLCHAIN_HASH" ]; then
        info "cached download verified ($TOOLCHAIN_HASH_KIND)"
    else
        [ "$OFFLINE" = 1 ] && fail "--offline: $archive is missing or fails its checksum"
        rm -f "$archive"
        info "downloading $TOOLCHAIN_FILE (about 330 MB)"
        info "$TOOLCHAIN_URL"
        fetch "$TOOLCHAIN_URL" "$archive"
        if [ "$(hash_of "$TOOLCHAIN_HASH_KIND" "$archive")" != "$TOOLCHAIN_HASH" ]; then
            rm -f "$archive"
            fail "download failed Arm's $TOOLCHAIN_HASH_KIND checksum; nothing was extracted"
        fi
        info "$TOOLCHAIN_HASH_KIND verified"
    fi

    # Extract into a staging directory, then flatten: 15.3's archive puts bin/,
    # aarch64-none-linux-gnu/, ... at its top level, 9.2 wraps them in
    # gcc-arm-<version>-mingw-w64-i686-aarch64-none-linux-gnu/. TOOLCHAIN_DIR
    # must hold bin/ either way, so whatever single top directory appears is
    # unwrapped.
    stage="$DOWNLOADS/stage-${TOOLCHAIN_VER}"
    # Kept separately: flattening below reassigns $stage to the inner directory,
    # and the wrapper around it is empty and must not outlive the install (an
    # empty stage-<version> directory used to be left in $DOWNLOADS).
    stageroot="$stage"
    rm -rf "$stage" && mkdir -p "$stage"
    case "$TOOLCHAIN_FILE" in
        *.zip)
            command -v unzip >/dev/null 2>&1 || fail "unzip is required (MSYS2: pacman -S unzip)"
            info "unzipping"
            unzip -q -o "$archive" -d "$stage" || { rm -rf "$stage"; fail "unzip failed"; } ;;
        *.tar.xz|*.tar.gz)
            command -v tar >/dev/null 2>&1 || fail "tar is required to unpack $TOOLCHAIN_FILE"
            info "unpacking"
            tar xf "$archive" -C "$stage" || { rm -rf "$stage"; fail "tar failed"; } ;;
        *)  fail "unrecognised archive type: $TOOLCHAIN_FILE" ;;
    esac

    if [ ! -d "$stage/bin" ]; then
        top=$(find "$stage" -mindepth 1 -maxdepth 1 -type d | head -1)
        [ -n "$top" ] && [ -d "$top/bin" ] || { rm -rf "$stage"; fail "unexpected archive layout (no bin/ found)"; }
        set -- "$top"; stage="$1"
    fi
    [ -x "$stage/bin/${CROSS}gcc" ] ||
        fail "the archive did not produce bin/${CROSS}gcc; installed toolchain was kept"
    # Prepare beside the destination so the final renames stay on one volume.
    # Never erase an unmanaged installation or an interrupted transaction.
    dest="${TOOLCHAIN_DIR}.mugen-new"
    local backup="${TOOLCHAIN_DIR}.mugen-old"
    [ ! -e "$dest" ] && [ ! -e "$backup" ] ||
        fail "unfinished toolchain transaction at $dest or $backup; installed tree was kept"
    if [ -d "$TOOLCHAIN_DIR" ] && [ -n "$(find "$TOOLCHAIN_DIR" -mindepth 1 -maxdepth 1 -print -quit)" ]; then
        compgen -G "$TOOLCHAIN_DIR/.mugen-toolchain-*" >/dev/null ||
            fail "refusing to replace unmanaged toolchain $TOOLCHAIN_DIR; choose another TOOLCHAIN_DIR"
    fi
    mkdir -p "$dest"
    cp -a "$stage/." "$dest/" || { rm -rf "$dest"; fail "copy failed; installed toolchain was kept"; }
    printf '%s\n' "$TOOLCHAIN_VER" > "$dest/.mugen-toolchain-${TOOLCHAIN_VER}"
    if [ -e "$TOOLCHAIN_DIR" ]; then
        mv "$TOOLCHAIN_DIR" "$backup" || { rm -rf "$dest"; fail "cannot back up installed toolchain"; }
    fi
    if ! mv "$dest" "$TOOLCHAIN_DIR"; then
        [ ! -e "$backup" ] || mv "$backup" "$TOOLCHAIN_DIR"
        fail "cannot install replacement; previous toolchain restored"
    fi
    rm -rf "$backup" "$stage" "$stageroot"
    info "installed into $TOOLCHAIN_DIR"

}

# --------------------------------------------------------------- checks
check_toolchain() {
    if [ ! -x "$CC_BIN" ]; then
        printf '  toolchain  MISSING  (%s)\n' "$CC_BIN"
        printf '             run: port/arm64/build.sh --setup\n'
        return 1
    fi
    local triple version installed
    triple=$("$CC_BIN" -dumpmachine 2>/dev/null || true)
    version=$("$CC_BIN" --version 2>/dev/null | head -1 || true)
    case "$triple" in
        aarch64-*) ;;
        *) printf '  toolchain  WRONG    expected aarch64-, got %s\n' "$triple"
           printf '             %s\n' "$TOOLCHAIN_DIR"
           return 1 ;;
    esac
    printf '  toolchain  ok       %s\n' "$version"
    printf '             %s -> %s\n' "$TOOLCHAIN_DIR" "$triple"
    # What glibc is actually installed, versus what was asked for. A mismatch
    # means a stale directory (an earlier --setup of the other version).
    installed=$(strings "$TOOLCHAIN_DIR/aarch64-none-linux-gnu/libc/lib64/libc.so.6" 2>/dev/null \
                | sed -n 's/.*release version \([0-9][0-9]*\.[0-9][0-9]*\).*/\1/p' | head -1)
    if [ -n "$installed" ] && [ "$installed" != "$TOOLCHAIN_GLIBC" ]; then
        printf '  glibc      WRONG    installed %s, ARM64_TOOLCHAIN=%s wants %s\n' \
               "$installed" "$TOOLCHAIN_KIND" "$TOOLCHAIN_GLIBC"
        printf '             run: port/arm64/build.sh --setup   (it replaces the tree)\n'
        return 1
    fi
    printf '  glibc      ok       %s (target needs >= this)\n' "${installed:-$TOOLCHAIN_GLIBC}"
    return 0
}

check_sysroot() {
    if [ ! -f "$XSYSROOT/usr/lib/pkgconfig/sdl2.pc" ]; then
        printf '  sysroot    MISSING  (%s)\n' "$XSYSROOT"
        printf '             run: port/arm64/build.sh --sysroot\n'
        return 1
    fi
    printf '  sysroot    ok       %s\n' "$XSYSROOT"
    return 0
}

# --------------------------------------------------------------- sysroot
#
# Download, verify and unpack the dependency tree. Same discipline as the
# toolchain above: the archive is cached under $DOWNLOADS and checked against
# the pinned SHA-256 every time it is used, and the tree is prepared beside the
# destination and swapped in only once it is known to be complete. A
# half-unpacked sysroot breaks every later run, and the tree already in place is
# what would get the blame.
install_sysroot() {
    local archive stagedir tree dest backup
    say "Dependency tree ($SYSROOT_FILE, sha256)"

    mkdir -p "$DOWNLOADS"
    archive="$DOWNLOADS/$SYSROOT_FILE"

    if [ -f "$archive" ] && [ "$(hash_of sha256 "$archive")" = "$SYSROOT_SHA256" ]; then
        info "cached download verified (sha256)"
    else
        [ "$OFFLINE" = 1 ] && fail "--offline: $archive is missing or fails its checksum"
        rm -f "$archive"
        info "downloading $SYSROOT_FILE (about 16 MB)"
        info "$SYSROOT_URL"
        fetch "$SYSROOT_URL" "$archive"
        if [ "$(hash_of sha256 "$archive")" != "$SYSROOT_SHA256" ]; then
            rm -f "$archive"
            fail "$SYSROOT_FILE failed its SHA-256; nothing was extracted
       expected $SYSROOT_SHA256
       (a re-published asset needs ARM64_SYSROOT_SHA256=<new hash>)"
        fi
        info "sha256 verified"
    fi

    command -v unzip >/dev/null 2>&1 || fail "unzip is required (MSYS2: pacman -S unzip)"
    stagedir="$DOWNLOADS/stage-sysroot"
    rm -rf "$stagedir" && mkdir -p "$stagedir"
    info "unpacking"
    unzip -q -o "$archive" -d "$stagedir" || { rm -rf "$stagedir"; fail "unzip failed"; }

    # The asset is packed with one top directory (arm64-sysroot/), but that is
    # not its contract: whatever single directory appears is unwrapped, as with
    # the toolchain archive above.
    tree="$stagedir"
    if [ ! -d "$tree/usr" ]; then
        top=$(find "$stagedir" -mindepth 1 -maxdepth 1 -type d | head -1)
        [ -n "$top" ] && [ -d "$top/usr" ] || { rm -rf "$stagedir"; fail "unexpected archive layout (no usr/ found)"; }
        tree="$top"
    fi
    [ -f "$tree/usr/lib/pkgconfig/sdl2.pc" ] || {
        rm -rf "$stagedir"
        fail "$SYSROOT_FILE did not produce usr/lib/pkgconfig/sdl2.pc; the existing tree was kept"
    }

    # No absolute path may survive in the tree. The published arm64 asset bakes
    # the prefix of the machine that generated it (/c/Projects/...), so unpacked
    # anywhere else every -I and -L points at a directory that does not exist --
    # and the compile fails as "SDL.h: No such file or directory" far from the
    # cause. ${pcfiledir} is the directory holding the .pc file, so ../.. is the
    # tree's usr/: the tree then works wherever it is unpacked. Rewriting an
    # already-relative prefix is a no-op, so this stays correct once the asset
    # ships that way.
    for pc in "$tree"/usr/lib/pkgconfig/*.pc; do
        [ -f "$pc" ] || continue
        sed -i 's|^prefix=.*|prefix=${pcfiledir}/../..|' "$pc"
    done

    # Prepare beside the destination so the final renames stay on one volume,
    # and keep the old tree until the new one is in place.
    dest="$XSYSROOT.mugen-new"
    backup="$XSYSROOT.mugen-old"
    [ ! -e "$dest" ] && [ ! -e "$backup" ] || {
        rm -rf "$stagedir"
        fail "unfinished sysroot transaction at $dest or $backup; existing tree was kept"
    }
    mkdir -p "$(dirname "$XSYSROOT")"
    mv "$tree" "$dest" || { rm -rf "$stagedir"; fail "cannot stage the tree beside $XSYSROOT"; }
    rm -rf "$stagedir"
    if [ -e "$XSYSROOT" ]; then
        mv "$XSYSROOT" "$backup" || { rm -rf "$dest"; fail "cannot back up the existing tree"; }
    fi
    if ! mv "$dest" "$XSYSROOT"; then
        [ ! -e "$backup" ] || mv "$backup" "$XSYSROOT"
        fail "cannot install the dependency tree; the previous one was restored"
    fi
    rm -rf "$backup"
    info "installed into $XSYSROOT"
}

# ------------------------------------------------------------------ build
if [ "$CLEAN" = 1 ]; then
    say "Cleaning $OUT"
    rm -rf "$OUT"
fi

if [ "$DO_SETUP" = 1 ] || ! check_toolchain >/dev/null 2>&1; then
    if [ "$DO_SETUP" = 1 ] || [ "$OFFLINE" != 1 ]; then
        install_toolchain
    fi
fi

if [ "$CHECK_TOOLS" = 1 ]; then
    say "Tools"
    rc=0
    check_toolchain || rc=1
    check_sysroot || rc=1
    if command -v pkg-config >/dev/null 2>&1; then
        printf '  pkg-config ok       %s\n' "$(pkg-config --version 2>/dev/null)"
    else
        printf '  pkg-config MISSING  (MSYS2: pacman -S pkgconf)\n'
        rc=1
    fi
    exit $rc
fi

if [ "$SKIP_SYSROOT" = 0 ]; then
    if [ "$DO_SYSROOT" = 1 ] || ! check_sysroot >/dev/null 2>&1; then
        install_sysroot
    fi
fi

[ -n "$JOBS" ] || JOBS=$(nproc 2>/dev/null || echo 4)

# The toolchain is NOT added to the caller's PATH permanently; it is prepended
# for this build only. The Makefile takes an explicit CROSS_COMPILE prefix, so
# the compiler choice cannot silently fall back to the host gcc.
say "Building mugen-unleashed for linux-arm64"
info "compiler  $CC_BIN"
info "sysroot   $XSYSROOT"
info "output    $OUT"
info "jobs      $JOBS"
PATH="$TOOLCHAIN_DIR/bin:$PATH" \
make -C "$ROOT" -j"$JOBS" \
    CROSS_COMPILE="$CROSS" \
    XSYSROOT="$XSYSROOT" \
    MUGEN_TARGET=linux-arm64 \
    ${GLES:+MUGEN_GLES="$GLES"}

target="$OUT/mugen-linux-arm64-aarch64-none-linux-gnu"
[ -f "$target" ] || fail "the build reported success but $target is missing"

# ---------------------------------------------------------------- upload
#
# Transport is `tar` over the ssh *exec* channel, not scp or sftp: this device's
# sshd points its sftp subsystem at a dropbear path that does not exist
# (/usr/local/crosware/software/dropbear/current/libexec/sftp-server), so both
# scp and sftp fail against it while plain ssh works fine. A tar stream over
# `ssh "tar xzf -"` needs nothing on the device beyond tar, transfers the mode
# bits, and is verified with a checksum before the old binary is replaced.
upload() {
    local pass size remote_md5 local_md5 tmpname
    command -v sshpass >/dev/null 2>&1 ||
        fail "sshpass is required for --upload (MSYS2: pacman -S sshpass)"
    command -v ssh >/dev/null 2>&1 || fail "ssh is required for --upload"

    say "Uploading to $DEVICE:$DEVICE_DIR/"
    if [ -z "${DEVICE_PASS:-}" ]; then
        if [ -t 0 ]; then
            printf '    password for %s: ' "$DEVICE"
            stty -echo 2>/dev/null || true
            IFS= read -r DEVICE_PASS
            stty echo 2>/dev/null || true
            printf '\n'
        else
            fail "no password: set DEVICE_PASS=<password> (or run interactively)"
        fi
    fi

    # One ssh invocation per concern, each with its own connect. -o LogLevel=ERROR
    # keeps OpenSSH's post-quantum advisory out of the output; the device's
    # OpenSSH is older than the client's default KEX policy.
    SSHOPTS=(-o StrictHostKeyChecking=no
             -o UserKnownHostsFile="$ROOT/build/tmp/arm64-ssh-known-hosts"
             -o ConnectTimeout=15 -o LogLevel=ERROR)
    mkdir -p "$ROOT/build/tmp"

    remote() { sshpass -p "$DEVICE_PASS" ssh "${SSHOPTS[@]}" "$DEVICE" "$@"; }

    remote 'echo ok' >/dev/null 2>&1 ||
        fail "cannot log in to $DEVICE -- check DEVICE and DEVICE_PASS"
    info "logged in"
    remote "mkdir -p '$DEVICE_DIR'" || fail "cannot create $DEVICE_DIR on the device"

    size=$(wc -c < "$target" | tr -d '[:space:]')
    local_md5=$(md5sum "$target" | awk '{print $1}')
    # Stage under a temporary name and rename only after the checksum matches:
    # a dropped connection mid-copy would otherwise leave a truncated binary in
    # place of a working one.
    tmpname=".$DEVICE_BIN.new"
    info "sending $((size / 1024 / 1024)) MB"
    # The whole chain runs under one `cd`: the tar stream and the rename must
    # both happen in $DEVICE_DIR, and the remote command is a single shell.
    # --warning=no-timestamp: the archive carries the build machine's clock,
    # which is ahead of the device's, and GNU tar otherwise floods the output
    # with "time stamp ... is ... s in the future" for every member.
    # Port fix: extraction must not overwrite DEVICE_BIN before verification,
    # even when its name equals the local build's basename.
    case "$DEVICE_BIN" in *[!A-Za-z0-9._-]*|""|.|..) fail "DEVICE_BIN must be a simple filename" ;; esac
    case "$(basename "$target")" in *[!A-Za-z0-9._-]*) fail "unexpected build filename" ;; esac
    tar cf - --warning=no-timestamp --transform="s|.*|$tmpname|" \
        -C "$(dirname "$target")" "$(basename "$target")" |
        remote "cd '$DEVICE_DIR' && tar xf - --warning=no-timestamp" ||
        fail "transfer failed"

    # Absolute paths everywhere: the device's ssh runs a non-interactive shell
    # where `~` and any cwd assumption do not hold, and a relative path here
    # silently produced "No such file or directory" instead of a checksum.
    remote_md5=$(remote "md5sum '$DEVICE_DIR/$tmpname'" 2>/dev/null | awk '{print $1}')
    if [ "$remote_md5" != "$local_md5" ]; then
        remote "rm -f '$DEVICE_DIR/$tmpname'" 2>/dev/null
        fail "checksum mismatch after upload (local $local_md5, remote ${remote_md5:-none}); nothing was replaced"
    fi
    info "checksum verified ($local_md5)"

    # Keep the previous binary, renamed aside -- the same .prev convention the
    # device already uses, so a bad build can be rolled back without a laptop.
    if [ "$KEEP_OLD" = 1 ]; then
        remote "cd '$DEVICE_DIR' && if [ -f '$DEVICE_BIN' ]; then mv -f '$DEVICE_BIN' '$DEVICE_BIN.prev'; fi" ||
            fail "could not move the previous binary aside"
        info "previous binary kept as $DEVICE_BIN.prev"
    fi
    remote "cd '$DEVICE_DIR' && mv -f '$tmpname' '$DEVICE_BIN' && chmod +x '$DEVICE_BIN'" ||
        fail "could not install the new binary"

    info "$DEVICE:$DEVICE_DIR/$DEVICE_BIN  ($(remote "ls -l '$DEVICE_DIR/$DEVICE_BIN'" 2>/dev/null | awk '{print $5}') bytes)"
    info ""
    info "run it on the device:"
    info "  ssh $DEVICE 'cd $DEVICE_DIR && ./$DEVICE_BIN -nosound'"
}

if [ "$DO_UPLOAD" = 1 ]; then
    upload
fi

say "Done"
info "$target"
if [ "$DO_UPLOAD" = 1 ]; then
    info ""
    info "uploaded to $DEVICE:$DEVICE_DIR/$DEVICE_BIN"
else
    info ""
    info "This is an arm64 Linux binary for the target device; it cannot run here."
    info "Copy it across with --upload, or by hand into the game directory."
fi
