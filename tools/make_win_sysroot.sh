#!/usr/bin/env bash
# =============================================================================
# make_win_sysroot.sh — package a MinGW kit's dependency tree as a sysroot
#
#   tools/make_win_sysroot.sh <kit-dir> <name>
#
#     tools/make_win_sysroot.sh C:/w64devkit win64
#     tools/make_win_sysroot.sh C:/x86devkit win32
#
# Writes build/sysroots/<name>-sysroot/ and build/tmp/<name>-sysroot.zip: the
# headers, static archives and .pc files the Windows Makefile asks for by
# package name (sdl2 SDL2_mixer SDL2_ttf zlib libpng16), resolved through
# pkg-config so the tree carries the whole --static closure and nothing else.
#
# Why this exists. A MinGW devkit is 2 GB and carries its own opinion about
# which SDL2 it ships; a kit assembled without the SDL add-ons fails
# `make check-toolchain` with "pkg-config cannot find SDL2_mixer SDL2_ttf
# libpng16" even though the compiler is fine. Publishing the dependency set as
# an asset makes the external half of the build the same everywhere, which is
# what the arm64 port does with arm64-sysroot.zip.
#
# Layout, deliberately the same shape as the arm64 tree:
#
#   <name>-sysroot/usr/include/{SDL2,freetype2,libpng16,opus}/ + root headers
#   <name>-sysroot/usr/lib/lib*.a                static archives (closure only)
#   <name>-sysroot/usr/lib/pkgconfig/*.pc        prefix=${pcfiledir}/../..
#   <name>-sysroot/SYSROOT.txt                   what is in it, and what is not
#
# The .pc files are the kit's own, with the kit's absolute prefix replaced by
# ${pcfiledir}/../.. — pkgconf resolves that to the tree's own location, so the
# archive works from any checkout path. (The arm64 asset bakes an absolute
# prefix instead, so it only works where it was generated. Do not copy that
# here.)
#
# What is deliberately NOT copied: the MinGW-w64 CRT and Windows import
# libraries (-lmingw32, -lkernel32, -lshlwapi, ...) and the compiler runtimes
# (-lgcc, -lstdc++, -lssp, -lpthread/winpthread, -lm). They belong to the
# toolchain that compiles the engine, exactly as glibc belongs to the Arm
# toolchain rather than to arm64-sysroot.zip.
# =============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

# The five the Makefile names for a Windows SDL2 build (STATIC_PKGS).
PKGS="sdl2 SDL2_mixer SDL2_ttf zlib libpng16"

# Headers that live directly in the kit's include/ and are reached through
# -I${includedir}: zlib and libpng have their own .pc, bzlib and brotli come in
# through freetype2's Requires. Everything else is a directory and is copied
# whole (SDL2, freetype2, libpng16, opus).
ROOT_HEADERS="zlib.h zconf.h bzlib.h png.h pngconf.h pnglibconf.h"
ROOT_DIRS="brotli"

# Toolchain, not dependency. Matched on the -l name, before the file lookup.
is_toolchain_lib() {
    case "$1" in
        mingw32|mingwex|msvcrt|ucrt|ucrtbase|oldnames|moldname|m|\
        gcc|gcc_eh|gcc_s|stdc++|supc++|ssp|ssp_nonshared|pthread|winpthread) return 0 ;;
        # Windows API import libraries shipped with MinGW-w64.
        kernel32|user32|gdi32|winmm|imm32|ole32|oleaut32|version|uuid|advapi32|\
        setupapi|shell32|shlwapi|comctl32|comdlg32|ws2_32|wsock32|winspool|\
        wininet|iphlpapi|secur32|crypt32|bcrypt|userenv|oleacc|uxtheme|dwmapi|\
        shcore|gdiplus|pathcch|propsys|dbghelp|dxguid|dinput8|dsound|\
        opengl32|glu32|odbc32|normaliz) return 0 ;;
    esac
    return 1
}

say() { printf '  %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

if [ $# -ne 2 ]; then
    sed -n '3,8p' "$0" | sed 's/^# \{0,1\}//' >&2
    exit 2
fi
KIT="$(cd "$1" 2>/dev/null && pwd)" || die "no such kit directory: $1"
NAME="$2"
case "$NAME" in *[!a-z0-9-]*|"") die "name must be simple (win32, win64): $NAME" ;; esac

[ -d "$KIT/lib/pkgconfig" ] || die "$KIT/lib/pkgconfig is not a directory - not a devkit?"
[ -d "$KIT/include" ] || die "$KIT/include is not a directory"

# The tree is an installed sysroot, so it goes where sysroots live and stays
# usable by `make`; the zip is a scratch artefact for releasing, so it goes to
# build/tmp. See "What lives under build/" in AGENTS.md.
OUT="$ROOT/build/sysroots/$NAME-sysroot"
ZIP="$ROOT/build/tmp/$NAME-sysroot.zip"

# pkg-config resolves the kit's .pc files and nothing else. PKG_CONFIG_PATH is
# unset because an inherited one wins over PKG_CONFIG_LIBDIR, and the SYSTEM_*
# paths are pointed at the kit so pkgconf emits -I/-L instead of qualifying them
# as host system paths (the same trap the cross build hits).
pc() {
    env -u PKG_CONFIG_PATH \
        PKG_CONFIG_LIBDIR="$KIT/lib/pkgconfig" \
        PKG_CONFIG_SYSTEM_INCLUDE_PATH="$KIT/include" \
        PKG_CONFIG_SYSTEM_LIBRARY_PATH="$KIT/lib" \
        PKG_CONFIG_ALLOW_SYSTEM_CFLAGS=1 \
        PKG_CONFIG_ALLOW_SYSTEM_LIBS=1 \
        pkg-config "$@"
}

command -v pkg-config >/dev/null 2>&1 || die "pkg-config not found (MSYS2: pacman -S pkgconf)"
missing=""
for p in $PKGS; do pc --exists "$p" || missing="$missing $p"; done
[ -z "$missing" ] || die "the kit does not provide:$missing"

# Recursive Requires/Requires.private closure. pkg-config's own --print-requires
# is not transitive in every pkgconf version (freetype2's libbrotlidec goes
# missing), so the walk is done here.
pc_closure() {
    local queue="$*" seen=" " next p q
    while [ -n "$queue" ]; do
        next=""
        for p in $queue; do
            case "$seen" in *" $p "*) continue ;; esac
            seen="$seen$p "
            [ -f "$KIT/lib/pkgconfig/$p.pc" ] || continue
            printf '%s\n' "$p"
            for q in $(pc --print-requires --print-requires-private "$p" 2>/dev/null); do
                case "$q" in
                    '>='|'<='|'='|'>'|'<') continue ;;
                    [0-9]*) continue ;;                # a version number
                esac
                case "$seen" in *" $q "*) ;; *) next="$next $q" ;; esac
            done
        done
        queue="$next"
    done
}

echo "kit:  $KIT"
echo "tree: $OUT"

CLOSURE="$(pc_closure $PKGS)"

# --- the archive closure ----------------------------------------------------
# --libs --static follows Requires and Requires.private, so every codec behind
# SDL2_mixer and freetype is named here. -l:libX.a (an exact file) and -lX (a
# name) both appear, depending on how the kit wrote its .pc files.
echo "archives"
rm -rf "$OUT"
mkdir -p "$OUT/usr/lib/pkgconfig" "$OUT/usr/include"

n=0
for tok in $(pc --libs --static $PKGS); do
    case "$tok" in
        -l:lib*.a) lib="${tok#-l:lib}"; lib="${lib%.a}" ;;
        -l*)       lib="${tok#-l}" ;;
        *)         continue ;;
    esac
    is_toolchain_lib "$lib" && continue
    # One -l can be reached twice (a codec lists another that also lists it);
    # the copy is idempotent and the count follows the copies.
    [ -f "$OUT/usr/lib/lib$lib.a" ] && continue
    [ -f "$KIT/lib/lib$lib.a" ] ||
        die "-l$lib is named by a .pc file but $KIT/lib/lib$lib.a is missing"
    cp -p "$KIT/lib/lib$lib.a" "$OUT/usr/lib/"
    say "lib$lib.a"
    n=$((n + 1))
done

# --- headers ----------------------------------------------------------------
# The -I directories pkg-config asked for, which is exactly the set the engine's
# compile needs. One of them is the kit's include/ root, which holds hundreds of
# unconnected headers, so only the named files and directories are taken from it.
echo "headers"
# pkgconf answers -I in Windows form (C:/w64devkit/include/SDL2) while $KIT came
# from a POSIX shell (pwd gives /c/w64devkit), so the two never compare equal
# until the path is normalised. Getting this wrong copies no headers at all and
# still builds here, because the kit's include directory is on the compiler's
# default search path -- the tree is only wrong on a machine without the kit.
for d in $(pc --cflags $CLOSURE 2>/dev/null | tr ' ' '\n' | sed -n 's/^-I//p' | sort -u); do
    d="$(cygpath -u "$d" 2>/dev/null || printf '%s' "$d")"
    case "$d" in
        "$KIT/include")
            for f in $ROOT_HEADERS; do
                if [ -f "$KIT/include/$f" ]; then
                    cp -p "$KIT/include/$f" "$OUT/usr/include/"
                    say "$f"
                fi
            done
            for f in $ROOT_DIRS; do
                if [ -d "$KIT/include/$f" ]; then
                    cp -aL "$KIT/include/$f" "$OUT/usr/include/"
                    say "$f/"
                fi
            done
            ;;
        "$KIT/include"/*)
            [ -d "$d" ] || continue
            cp -aL "$d" "$OUT/usr/include/"
            say "${d#"$KIT/include/"}/"
            ;;
    esac
done

# --- .pc files --------------------------------------------------------------
# The kit's own, with its absolute prefix replaced. pcfiledir is the directory
# holding the .pc file, so ../.. is the tree's usr/ and the archive works
# wherever it is unpacked, which an absolute prefix would not.
echo "pkgconfig"
KIT_WIN="$(cygpath -m "$KIT" 2>/dev/null || printf '%s' "$KIT")"
for p in $CLOSURE; do
    sed -e "s|$KIT_WIN|\${pcfiledir}/../..|g" -e "s|$KIT|\${pcfiledir}/../..|g" \
        "$KIT/lib/pkgconfig/$p.pc" > "$OUT/usr/lib/pkgconfig/$p.pc"
    say "$p.pc"
done

# --- what this is -----------------------------------------------------------
# The tree is an opaque blob once zipped; this is the only place its provenance
# and its exclusions are written down.
{
    echo "$NAME-sysroot"
    echo
    echo "Cross-build dependency tree for the Windows targets of mugen-unleashed."
    echo "Generated by tools/make_win_sysroot.sh from a MinGW-w64 devkit."
    echo
    echo "source kit:  $KIT_WIN"
    echo "generated:   $(date -u '+%Y-%m-%d %H:%M UTC')"
    echo
    echo "packages:"
    for p in $CLOSURE; do
        printf '  %-16s %s\n' "$p" "$(pc --modversion "$p" 2>/dev/null)"
    done
    echo
    echo "contents: the static archives of the --static closure of"
    echo "  $PKGS"
    echo "  their headers, and their .pc files."
    echo
    echo "prefix:   the .pc files use prefix=\${pcfiledir}/../.. so the tree works"
    echo "          from any path; no absolute path is baked in."
    echo
    echo "excluded: MinGW-w64 CRT and Windows import libraries (kernel32, shlwapi,"
    echo "          ...) and compiler runtimes (gcc, stdc++, ssp, winpthread, m)."
    echo "          They come from the toolchain that compiles the engine."
} > "$OUT/SYSROOT.txt"

# --- zip --------------------------------------------------------------------
command -v zip >/dev/null 2>&1 || die "zip is required (MSYS2: pacman -S zip)"
rm -f "$ZIP"
( cd "$(dirname "$OUT")" && zip -qr "$ZIP" "$(basename "$OUT")" )

echo
echo "archives: $n"
echo "files:    $(find "$OUT" -type f | wc -l)"
echo "unpacked: $(du -sh "$OUT" | cut -f1)"
echo "zip:      $(du -h "$ZIP" | cut -f1)   $(basename "$ZIP")"
echo "sha256:   $(sha256sum "$ZIP" | awk '{print $1}')"
