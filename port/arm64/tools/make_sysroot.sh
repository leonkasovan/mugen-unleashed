#!/usr/bin/env bash
# =============================================================================
# make_sysroot.sh — build the dependency tree the arm64 cross build links
# against, from an existing aarch64 Ubuntu root filesystem.
#
#   port/arm64/tools/make_sysroot.sh [--root <dir>] [--dest <dir>]
#
# Defaults:
#   --root  an aarch64 Ubuntu root, looked for at /home/deck/ubuntu_root_arm64
#           and, from Windows, under //wsl.localhost/<distro>/... (probed; the
#           spelling differs per platform, and none is right everywhere)
#   --dest  build/tmp/arm64-sysroot         (inside this repository, git-ignored)
#
# Usually run through port/arm64/build.sh, which calls it for you.
#
# Then build with:
#
#   make CROSS_COMPILE=aarch64-none-linux-gnu- XSYSROOT=build/tmp/arm64-sysroot \
#        MUGEN_TARGET=linux-arm64
#
# What it does. The Arm GNU Toolchain (aarch64-none-linux-gnu, 15.3.Rel2 — the
# Mingw-w64 host build) ships a complete glibc sysroot but no SDL2, no zlib and
# no libpng, and its own glibc (2.42) is much newer than the handheld roots the
# linux-arm64 target runs on. Rather than cross-building the whole SDL stack,
# this copies the already-built aarch64 libraries out of an arm64 root
# filesystem into
#
#   <dest>/usr/include/{SDL2,zlib.h,png*.h,freetype2}   headers
#   <dest>/usr/lib/lib{SDL2,SDL2_mixer,SDL2_ttf,z,png16}.so* + .a
#   <dest>/usr/lib/pkgconfig/*.pc                       what the Makefile asks
#   <dest>/usr/lib/rpath/*.so*                          the runtime closure
#
# The .pc files are written here rather than copied: the distribution's ones
# describe the distro's full dependency set (X11, Wayland, ALSA, pulse), and
# this engine only ever calls into SDL itself. They also name the shared
# objects explicitly (-l:libSDL2-2.0.so.0) because the tree carries the static
# archives too, and ld prefers a .a over a .so when both are present.
#
# `usr/lib/rpath/` is not the runtime search path -- the finished binary has no
# RPATH. It exists so ld can resolve the *transitive* NEEDED libraries of
# libSDL2-2.0.so.0 (libasound, libpulse, libX11, libFLAC, ...) while linking;
# the Makefile passes it as -rpath-link. Those libraries are resolved from the
# target itself at run time, so they are not linked in and not shipped.
#
# Reading the tree from Windows. This script runs in two places and the root
# path means different things in each:
#
#   - inside WSL, where /home/deck/ubuntu_root_arm64 is a native path;
#   - from MSYS, where the same tree appears under
#     //wsl.localhost/<distro>/home/deck/ubuntu_root_arm64 (or the older
#     //wsl$/... spelling) and nothing is at /home.
#
# The default is resolved at run time: an existing native path wins, otherwise
# the WSL shares are probed. --root still overrides both.
# =============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ARM64="$(cd "$HERE/.." && pwd)"
ROOTDIR="$(cd "$ARM64/../.." && pwd)"

# Absolute inside a Linux root; the WSL share prefixes the distro directory, so
# the same tail is appended to "//wsl.localhost/<distro>/" below.
ROOT_TAIL="home/deck/ubuntu_root_arm64"
ROOT="/$ROOT_TAIL"
DEST="$ROOTDIR/build/tmp/arm64-sysroot"

while [ $# -gt 0 ]; do
    case "$1" in
        --root) ROOT="$2"; shift 2 ;;
        --dest) DEST="$2"; shift 2 ;;
        -h|--help) sed -n '2,52p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

# Find the arm64 root when --root was not given.
#
# The candidates are probed one by one rather than globbed: MSYS does not expand
# wildcards in "//host/share" paths at all (the pattern comes back literal, so a
# glob loop "finds" a directory that does not exist). $WSL_DISTRO_NAME, set by
# WSL itself, names the distro to look in when the share is reachable.
if [ ! -d "$ROOT/usr/lib/aarch64-linux-gnu" ] && [ "$ROOT" = "/$ROOT_TAIL" ]; then
    cands=""
    if [ -n "${WSL_DISTRO_NAME:-}" ]; then
        cands="//wsl.localhost/$WSL_DISTRO_NAME //wsl\\$/$WSL_DISTRO_NAME"
    fi
    # The distro names to try when the environment does not say: the common
    # ones first, then whatever the shares themselves list.
    cands="$cands //wsl.localhost/Ubuntu-22.04 //wsl\\$/Ubuntu-22.04 \
           //wsl.localhost/Ubuntu-24.04 //wsl\\$/Ubuntu-24.04 \
           //wsl.localhost/Ubuntu //wsl\\$/Ubuntu \
           //wsl.localhost/Debian //wsl\\$/Debian"
    for share in $cands; do
        [ -d "$share/$ROOT_TAIL/usr/lib/aarch64-linux-gnu" ] || continue
        ROOT="$share/$ROOT_TAIL"
        echo "root: $ROOT"
        echo "      (found through the WSL share; pass --root to use another)"
        break
    done
fi

LIBDIR_REL="usr/lib/aarch64-linux-gnu"
SRC_LIB="$ROOT/$LIBDIR_REL"
SRC_INC="$ROOT/usr/include"

if [ ! -d "$SRC_LIB" ]; then
    echo "error: $SRC_LIB is not a directory." >&2
    echo "       Pass --root <dir>, the top of an aarch64 Ubuntu root filesystem" >&2
    echo "       (the one holding usr/lib/aarch64-linux-gnu/)." >&2
    echo "       From WSL it is a plain path; from MSYS, //wsl.localhost/<distro>/..." >&2
    exit 1
fi

DEST_LIB="$DEST/usr/lib"
DEST_INC="$DEST/usr/include"
DEST_PC="$DEST_LIB/pkgconfig"
DEST_RPATH="$DEST_LIB/rpath"
mkdir -p "$DEST_LIB" "$DEST_INC" "$DEST_PC" "$DEST_RPATH"

say() { printf '  %s\n' "$*"; }

# --- headers ---------------------------------------------------------------
# -L, not -a: the distribution's headers contain symlinks into themselves
# (include/SDL2/SDL_platform.h -> ../../SDL2/SDL_platform.h). Copied as links
# they point at directories that do not exist in this tree and every SDL2
# consumer dies on "Invalid argument" reading a header. Dereferencing each one
# is what makes the tree self-contained.
echo "headers"
for d in SDL2 freetype2 libpng16; do
    [ -d "$SRC_INC/$d" ] && cp -aL "$SRC_INC/$d" "$DEST_INC" 2>/dev/null && say "$d/"
done
# Debian/Ubuntu's SDL2 splits its configuration: include/SDL2/SDL_config.h is a
# two-line indirection to <SDL2/_real_SDL_config.h>, which the *distribution*
# keeps in the architecture include directory (/usr/include/aarch64-linux-gnu).
# Both halves must end up in one place or every SDL2 consumer fails with
# "SDL2/_real_SDL_config.h: No such file or directory".
if [ -d "$SRC_INC/aarch64-linux-gnu/SDL2" ]; then
    # Only _real_SDL_config.h is needed from there. The directory also holds
    # SDL_platform.h as a link back into ../../SDL2/ -- which is include/SDL2/
    # itself, already copied above. Dereferencing it from Windows fails with
    # EIO ("Input/output error"), and it would be overwritten with identical
    # content anyway, so it is skipped by name.
    cp -aL "$SRC_INC/aarch64-linux-gnu/SDL2/_real_SDL_config.h" "$DEST_INC/SDL2/" 2>/dev/null || {
        echo "error: _real_SDL_config.h missing in $SRC_INC/aarch64-linux-gnu/SDL2" >&2
        exit 1
    }
    say "_real_SDL_config.h -> SDL2/"
fi
# zlib and libpng headers. Some of them are symlinks inside the root
# (usr/include/png.h -> libpng16/png.h), and a symlink read through the
# //wsl.localhost share fails with EIO as soon as it is dereferenced -- so a
# header is only copied from the root when the root can be *read* from here.
# libpng16/ is already in $DEST_INC by this point, so the link's target is
# resolved inside the copy instead, which works from either side.
for f in zlib.h zconf.h png.h pngconf.h pnglibconf.h; do
    # No test before the copy: on a link the Windows side cannot read, both
    # `[ -e ]` and `[ -L ]` fail, so a test would skip the header silently.
    # Trying the copy and falling back is the only reliable order here.
    if cp -aL "$SRC_INC/$f" "$DEST_INC/" 2>/dev/null; then
        say "$f"
        continue
    fi
    # Dereferencing failed: use whatever the copy resolved it to.
    for alt in libpng16/"$f" libpng/"$f"; do
        if [ -f "$DEST_INC/$alt" ]; then
            cp -p "$DEST_INC/$alt" "$DEST_INC/$f"
            say "$f  (from $alt)"
            break
        fi
    done
done

# --- the libraries the engine links ----------------------------------------
echo "libraries"
copy() {                       # copy() <glob>...
    for pat in "$@"; do
        for f in $SRC_LIB/$pat; do
            [ -e "$f" ] || continue
            # -L: the root's .so names are symlinks to the real versioned file.
            # Copied as links they arrive broken (a WSL symlink on DrvFs is not
            # resolvable from the Windows-side ld), so each name becomes a real
            # file here. versioned first keeps the loop's later links harmless.
            cp -aL "$f" "$DEST_LIB/" 2>/dev/null || true
            say "$(basename "$f")"
        done
    done
}
copy 'libSDL2.a' 'libSDL2-2.0.so*' 'libSDL2_mixer.a' 'libSDL2_mixer-2.0.so*' \
     'libSDL2_ttf.a' 'libSDL2_ttf-2.0.so*' 'libz.a' 'libz.so*' \
     'libpng16.a' 'libpng16.so*' 'libfreetype.a' 'libfreetype.so*'

# SONAME names: every library is also present under its own SONAME
# (libSDL2-2.0.so.0 for libSDL2-2.0.so.0.10.0), because -l:libSDL2-2.0.so.0 is
# what the .pc files ask for and ld has nothing else to resolve it from.
#
# Copied, not symlinked. A symlink here is a WSL-created link on a DrvFs mount,
# and a Windows-side ld (the Arm toolchain's) reports such a target as
# "cannot find -l:libSDL2-2.0.so.0" -- the link exists, its target does not
# resolve across the mount. 1 MB per library is a cheap price for a tree that
# reads the same from Windows and from WSL.
soname_link() {                # soname_link <dir>
    local dir="$1" f sn
    for f in "$dir"/*.so*; do
        [ -f "$f" ] || continue
        sn=$(readelf -d "$f" 2>/dev/null | sed -n 's/.*SONAME.*\[\(.*\)\].*/\1/p' || true)
        [ -n "$sn" ] || continue
        [ "$sn" = "$(basename "$f")" ] && continue
        [ -e "$dir/$sn" ] || cp -p "$f" "$dir/$sn"
    done
}
soname_link "$DEST_LIB"

# --- the runtime closure, for -Wl,-rpath-link only -------------------------
# Every library the root ships, because libSDL2_mixer pulls in its codecs and
# those pull in theirs. Copying all of them once is cheaper than chasing the
# closure by hand and gets the same result.
echo "runtime closure (rpath-link only)"
n=0
skip_glibc() {                 # skip_glibc <file>  -- 0 when it belongs to libc
    local base sn
    base=$(basename "$1")
    sn=$(readelf -d "$1" 2>/dev/null | sed -n 's/.*SONAME.*\[\(.*\)\].*/\1/p')
    [ -n "$sn" ] || sn="$base"
    # Never shadow libc. The toolchain's own copy is what the binary was
    # compiled against, and the root's copies are GLIBC_PRIVATE-incompatible
    # with it: linking them into the same link line fails on every one of
    # _dl_addr / __libc_dlopen_mode / __libc_allocate_rtsig_private.
    #
    # The test is on the SONAME, not the file name: the real file is called
    # libdl-2.31.so, so a name-based pattern missed it as soon as -L started
    # dereferencing the links.
    case "$sn" in
        libc.so*|libdl.so*|libpthread.so*|librt.so*|libm.so*|libmvec.so*|\
        libresolv.so*|libutil.so*|libnsl.so*|libanl.so*|libcrypt.so*|\
        libthread_db.so*|libnss_*.so*) return 0 ;;
    esac
    return 1
}
for f in "$SRC_LIB"/lib*.so*; do
    [ -f "$f" ] || continue
    skip_glibc "$f" && continue
    # -L (dereference) for the same reason as everywhere else in this script:
    # the root's .so names are links, and a link on a DrvFs mount does not
    # resolve for the Windows-side ld, so rpath-link silently finds nothing.
    cp -aL "$f" "$DEST_RPATH/" 2>/dev/null && n=$((n + 1)) || true
done
# Some distributions keep these outside usr/lib/<triple>: Ubuntu's
# libpulsecommon lives in usr/lib/<triple>/pulseaudio/, and libopusfile in
# plain usr/lib/.
for d in "usr/lib/aarch64-linux-gnu/pulseaudio" "usr/lib/pulseaudio" "usr/lib"; do
    for f in "$ROOT/$d"/libpulsecommon* "$ROOT/$d"/libopusfile.so.0*; do
        for g in $f; do
            [ -e "$g" ] && cp -aL "$g" "$DEST_RPATH/" 2>/dev/null && n=$((n + 1)) || true
        done
    done
done
soname_link "$DEST_RPATH"
say "$n files"

# --- pkg-config ------------------------------------------------------------
echo "pkg-config"
P="$(cd "$DEST" && pwd)/usr"
for f in sdl2 SDL2_mixer SDL2_ttf zlib libpng freetype2; do rm -f "$DEST_PC/$f.pc"; done

cat > "$DEST_PC/sdl2.pc" <<EOF
prefix=$P
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: sdl2
Description: Simple DirectMedia Layer 2, shared (cross dependency tree)
Version: 2.0.10
Libs: -L\${libdir} -l:libSDL2-2.0.so.0
Libs.private: -lm -ldl -lpthread
Cflags: -I\${includedir}/SDL2 -D_REENTRANT
EOF

cat > "$DEST_PC/SDL2_mixer.pc" <<EOF
prefix=$P
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: SDL2_mixer
Description: SDL2_mixer, shared (cross dependency tree)
Version: 2.0.4
Requires: sdl2 >= 2.0.7
Libs: -L\${libdir} -l:libSDL2_mixer-2.0.so.0
Cflags: -I\${includedir}/SDL2
EOF

cat > "$DEST_PC/SDL2_ttf.pc" <<EOF
prefix=$P
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: SDL2_ttf
Description: SDL2_ttf, shared (cross dependency tree)
Version: 2.0.15
Requires: sdl2 >= 2.0.8
Libs: -L\${libdir} -l:libSDL2_ttf-2.0.so.0
Cflags: -I\${includedir}/SDL2
EOF

cat > "$DEST_PC/zlib.pc" <<EOF
prefix=$P
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: zlib
Description: zlib (cross dependency tree)
Version: 1.2.11
Libs: -L\${libdir} -l:libz.so.1
Cflags: -I\${includedir}
EOF

cat > "$DEST_PC/libpng.pc" <<EOF
prefix=$P
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: libpng
Description: libpng16, shared (cross dependency tree)
Version: 1.6.37
Libs: -L\${libdir} -l:libpng16.so.16
Libs.private: -l:libz.so.1 -lm
Cflags: -I\${includedir}
EOF

cat > "$DEST_PC/freetype2.pc" <<EOF
prefix=$P
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: freetype2
Description: FreeType 2 (cross dependency tree)
Version: 2.10.1
Libs: -L\${libdir} -l:libfreetype.so.6
Libs.private: -l:libz.so.1
Cflags: -I\${includedir}/freetype2
EOF
ls "$DEST_PC" | sed 's/^/  /'

echo
echo "dependency tree ready: $DEST"
echo "Build with:  port/arm64/build.sh"
