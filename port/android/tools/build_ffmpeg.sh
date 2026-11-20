#!/bin/sh
# Build the pinned WebM decoder libraries for the Android arm64-v8a APK.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
source_dir="$root/third_party/ffmpeg-7.1.5"
if [ ! -f "$source_dir/configure" ]; then
    echo "Initialize FFmpeg with git submodule update --init third_party/ffmpeg-7.1.5" >&2
    exit 1
fi

: "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME to the NDK for this build host}"
case "$(uname -s):$(uname -m)" in
    Linux:x86_64) host=linux-x86_64 ;;
    Darwin:*) host=darwin-x86_64 ;;
    *) echo "Build FFmpeg on Linux x86-64 (including WSL) or macOS" >&2; exit 1 ;;
esac
toolchain=$(CDPATH= cd -- "$ANDROID_NDK_HOME" && pwd)/toolchains/llvm/prebuilt/$host
if [ ! -x "$toolchain/bin/aarch64-linux-android21-clang" ]; then
    echo "Android API 21 ARM64 compiler missing under $toolchain/bin" >&2
    exit 1
fi

prefix="$root/build/android/ffmpeg/arm64-v8a"
mkdir -p "$(dirname "$prefix")"
work=$(mktemp -d "$root/build/android/ffmpeg-build.XXXXXX")
trap 'rm -rf "$work"' EXIT
trap 'exit 1' HUP INT TERM

(
    cd "$work"
    ln -s "$source_dir" src
    export PATH="$toolchain/bin:$PATH"
    export TMPDIR="$work"
    ./src/configure --prefix="$prefix" \
        --target-os=android --arch=aarch64 --enable-cross-compile \
        --cc=aarch64-linux-android21-clang --as=aarch64-linux-android21-clang \
        --ar=llvm-ar --nm=llvm-nm --ranlib=llvm-ranlib --strip=llvm-strip \
        --enable-pic --enable-shared --disable-static \
        --disable-everything --disable-autodetect --disable-programs \
        --disable-doc --disable-debug --disable-network --disable-iconv \
        --disable-avdevice --disable-avfilter --disable-swresample --disable-postproc \
        --enable-protocol=file --enable-demuxer=matroska \
        --enable-decoder=vp8,vp9 --enable-parser=vp8,vp9
    make -j "${JOBS:-4}"
    make DESTDIR="$work/install" install-libs install-headers
)

# Publish the prefix only after all four libraries and their headers install.
rm -rf "$prefix"
mv "$work/install$prefix" "$prefix"
printf 'Android FFmpeg prefix: %s\n' "$prefix"
