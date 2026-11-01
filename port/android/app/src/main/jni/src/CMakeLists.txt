# =============================================================================
# mugen-unleashed engine — Android build
#
# Built as a shared library (libmain.so) and loaded by SDL2's Java activity,
# which calls the engine's main() as a JNI entry point. That is the same shape
# the ikemen-droid reference build uses; see
# ikemen-develop-update/tools/generate_android_via_native.sh.
# =============================================================================
cmake_minimum_required(VERSION 3.10)

project(mugen_unleashed_android C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# The engine tree is six levels up from port/android/app/src/main/jni/src:
#   src -> jni -> main -> src -> app -> android -> port -> <root>
set(MUGEN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../..")
get_filename_component(MUGEN_ROOT "${MUGEN_ROOT}" ABSOLUTE)

if(NOT EXISTS "${MUGEN_ROOT}/src/video.c")
    message(FATAL_ERROR
        "MUGEN_ROOT did not resolve to the engine tree (looked in ${MUGEN_ROOT}).\n"
        "This CMakeLists expects to live at port/android/app/src/main/jni/src.")
endif()

message(STATUS "mugen-unleashed source: ${MUGEN_ROOT}")

# ---------------------------------------------------------------------------
# Engine sources — every module except the three desktop entry points and the
# OpenGL loader, which cannot work on Android (no GLEW, and the Android GLES
# context is not what glrender.c was written against).
#
# The engine ships a software renderer, which is what this port uses; the GL
# renderer stays out of the build exactly as it does on the Vita port.
# ---------------------------------------------------------------------------
file(GLOB MUGEN_ENGINE_SOURCES "${MUGEN_ROOT}/src/*.c")

# Entry points and platform back ends that must not be compiled here.
# Excluded: sdlmain.c/main.c are the desktop entry points (android_main.c
# supplies main here), and glrender.c/gldetect.c are the desktop-GL renderer,
# which cannot exist on Android.
#
# NOT excluded: compat_linux.c. It is the Win32-over-POSIX shim the Linux and
# PS Vita builds use (FreeLibrary, InterlockedExchangeAdd, FontsDir, ...), and
# the engine calls those unconditionally.
# main.c stays in: it holds Screen_IsSpecial, Camera_GetX/Y, Version_Parse and
# Rng_Seed as well as the entry point; its main() is renamed Mugen_Main for this
# target (see src/main.c) and android_main.c calls it.
list(FILTER MUGEN_ENGINE_SOURCES EXCLUDE REGEX ".*/(sdlmain|glrender|gldetect)\\.c$")

# The Android video layer replaces SDL 1.2's removed calls; it is added below.
list(APPEND MUGEN_ENGINE_SOURCES
     "${CMAKE_CURRENT_SOURCE_DIR}/android_video.c"
     "${CMAKE_CURRENT_SOURCE_DIR}/android_main.c"
     "${CMAKE_CURRENT_SOURCE_DIR}/android_gfx.c"
     "${CMAKE_CURRENT_SOURCE_DIR}/android_glstub.c")

# ---------------------------------------------------------------------------
# Third-party libraries, compiled from the copies in the engine tree so the
# Android build matches the Windows and Linux builds exactly.
# ---------------------------------------------------------------------------
set(TP "${MUGEN_ROOT}/third_party")
set(ANDROID_INCLUDES "")

# zlib: third_party/zlib-1.2.3 holds only a placeholder, so the NDK's own zlib
# (-lz, linked below) is used. It is a stock zlib build, which is all libpng and
# minizip need.
# libpng 1.2.38 IS vendored and is built here, so that the PNG decoder matches
# the desktop builds pixel for pixel (the engine's SFF loading depends on it).

# libpng (the vendored 1.2.38; the NDK's include path also has a png.h, so this
# directory must come first -- see the include ordering below).
file(GLOB PNG_SRC "${TP}/libpng-1.2.38/png*.c")
# pnggccrd.c / pngvcrd.c are hand-written MMX/SSE assembly paths that do not
# build for ARM; libpng selects its C paths when they are absent.
list(REMOVE_ITEM PNG_SRC
     "${TP}/libpng-1.2.38/pnggccrd.c"
     "${TP}/libpng-1.2.38/pngvcrd.c")
list(APPEND MUGEN_ENGINE_SOURCES ${PNG_SRC})
list(APPEND ANDROID_INCLUDES "${TP}/libpng-1.2.38")

# Lua 5.1.4, without the interpreter front end
file(GLOB LUA_SRC "${TP}/lua-5.1.4/src/*.c")
list(REMOVE_ITEM LUA_SRC "${TP}/lua-5.1.4/src/lua.c" "${TP}/lua-5.1.4/src/luac.c")
list(APPEND MUGEN_ENGINE_SOURCES ${LUA_SRC})
list(APPEND ANDROID_INCLUDES "${TP}/lua-5.1.4/src")

# minizip (inside zlib's contrib), for the .zip-based modes
file(GLOB MINIZIP_SRC "${TP}/minizip-0.15/*.c")
list(APPEND ANDROID_INCLUDES "${TP}/minizip-0.15")
if(MINIZIP_SRC)
    list(APPEND MUGEN_ENGINE_SOURCES ${MINIZIP_SRC})
endif()

# libresample, used by the BGM speed paths
file(GLOB RESAMPLE_SRC "${TP}/libresample/src/*.c")
if(RESAMPLE_SRC)
    list(APPEND MUGEN_ENGINE_SOURCES ${RESAMPLE_SRC})
    # The public header is in include/, not src/ (bgm.c includes <libresample.h>).
    list(APPEND ANDROID_INCLUDES "${TP}/libresample/include" "${TP}/libresample/src")
endif()

# SDL_mixer, SDL_ttf, SDL_gfx and FreeType are NOT vendored in this repository:
# third_party/SDL_mixer-1.2.12, SDL_ttf-2.0.11, SDL_gfx-2.0.26 and
# freetype-2.10.4 are placeholder directories with no sources (the desktop
# builds take them from the system via pkg-config -- see the Makefile's
# STATIC_PKGS). Android cannot, so the build script compiles the SDL2-era
# equivalents and stages them into jniLibs:
#
#     libSDL2_mixer.so   SDL_mixer 2.x, API-compatible superset of 1.2
#     libSDL2_ttf.so     SDL_ttf 2.x
#
# and SDL_gfx's single used entry point (zoomSurface) is provided by
# android_gfx.c in this directory, because SDL_gfx has no upstream SDL2 port
# that is safe to depend on.
#
# Set SDL2_MIXER_TTF_DIR to the directory holding their headers and .so files.
if(DEFINED sdl2MixerTtfDir AND NOT sdl2MixerTtfDir STREQUAL "")
    list(APPEND ANDROID_INCLUDES "${sdl2MixerTtfDir}/include" "${sdl2MixerTtfDir}/include/SDL2")
    link_directories("${sdl2MixerTtfDir}/lib")
    message(STATUS "SDL2_mixer/SDL2_ttf: ${sdl2MixerTtfDir}/lib")
endif()

# glrender.c is excluded above, so the GL loader is not referenced at all.
# The engine's software renderer is the only draw path on this target.

# SDL2 headers come from the cross-compiled SDL2 staged by build.sh (they are
# not in the NDK). Passed as -Psdl2IncludeDir=... by the Gradle invocation.
if(NOT DEFINED sdl2IncludeDir OR sdl2IncludeDir STREQUAL "")
    if(DEFINED ENV{MUGEN_ANDROID_SDL2_INCLUDE})
        set(sdl2IncludeDir "$ENV{MUGEN_ANDROID_SDL2_INCLUDE}")
    endif()
endif()
if(DEFINED sdl2IncludeDir AND NOT sdl2IncludeDir STREQUAL "")
    # <SDL.h> lives in include/SDL2/ in SDL2's own layout; both spellings are
    # added because the engine and vendored code use each.
    list(APPEND ANDROID_INCLUDES "${sdl2IncludeDir}" "${sdl2IncludeDir}/SDL2")
    message(STATUS "SDL2 headers: ${sdl2IncludeDir}")
else()
    message(FATAL_ERROR
        "SDL2 include directory not set.\n"
        "Pass -Dsdl2IncludeDir=<path containing SDL.h> (build.sh sets it).")
endif()

list(APPEND ANDROID_INCLUDES "${MUGEN_ROOT}/src")

add_library(main SHARED ${MUGEN_ENGINE_SOURCES})

target_include_directories(main PRIVATE ${ANDROID_INCLUDES})

# ---------------------------------------------------------------------------
# Compile definitions
#
# MUGEN_VITA_GL is the engine's existing "non-desktop / software renderer" guard
# and is reused deliberately: it already selects the System rendering path and
# the guarded platform fixes, and inventing a second macro would mean auditing
# every occurrence for the Android case. It is named for the first port that
# needed it, not for the platform.
# ---------------------------------------------------------------------------
# android_main.h is force-included into every translation unit: it supplies the
# SDL 1.2 names SDL2 removed (SDL_SetVideoMode, SDL_Flip, SDL_UpdateRect and
# friends) so the engine's call sites stay exactly as reconstructed.
set_source_files_properties(${MUGEN_ENGINE_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/android_main.h")

target_compile_definitions(main PRIVATE
    MUGEN_VITA_GL=1
    ANDROID_PORT=1
    PNG_ARM_NEON_OPT=0
    PNG_ARM_NEON_CHECK_SUPPORTED=0
    PNG_SETJMP_SUPPORTED=1
)

target_compile_options(main PRIVATE
    # libresample's public header is written for MSVC: it uses __declspec
    # attributes behind RESAMPLE_DECL. clang needs this to parse it. The engine
    # does the same thing for its own __cdecl/__int64 (see the Makefile's
    # MINGW note), so this is not a new dependency, just the ARM spelling.
    -fdeclspec
    -Wall
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-pointer-sign
    -Wno-format
    -fexceptions          # SDL2's Java entry needs it; the engine is C
    -fno-strict-aliasing  # the engine type-puns surface formats
)

# Needed: SDL2 lives in libSDL2.so staged into jniLibs by the build script, and
# the standard C math library is not linked by default on Android.
# SDL2 and its companion libraries are linked by absolute path, not -lSDL2.
#
# Why: they are cross-compiled outside this project (Android has no vendored
# copies), so there is no stable library directory for the linker to search --
# CMAKE_LIBRARY_PATH is ignored by the NDK toolchain file for this purpose. The
# paths come from the two variables build.sh passes; the .so files are also
# copied into jniLibs by build.sh so the APK packages them.
if(NOT DEFINED sdl2IncludeDir OR sdl2IncludeDir STREQUAL "")
    message(FATAL_ERROR "sdl2IncludeDir must be set to locate libSDL2.so")
endif()
get_filename_component(SDL2_LIBDIR "${sdl2IncludeDir}/../lib" ABSOLUTE)

set(MUGEN_ANDROID_SHARED_LIBS "${SDL2_LIBDIR}/libSDL2.so")
if(DEFINED sdl2MixerTtfDir AND NOT sdl2MixerTtfDir STREQUAL "")
    list(APPEND MUGEN_ANDROID_SHARED_LIBS
         "${sdl2MixerTtfDir}/lib/libSDL2_mixer.so"
         "${sdl2MixerTtfDir}/lib/libSDL2_ttf.so")
endif()
foreach(lib ${MUGEN_ANDROID_SHARED_LIBS})
    if(NOT EXISTS "${lib}")
        message(FATAL_ERROR "required shared library not found: ${lib}")
    endif()
endforeach()
message(STATUS "SDL2 libraries: ${MUGEN_ANDROID_SHARED_LIBS}")

target_link_libraries(main PRIVATE ${MUGEN_ANDROID_SHARED_LIBS}
                      GLESv1_CM GLESv2 log android m dl z)

set_target_properties(main PROPERTIES
    OUTPUT_NAME "main"
    PREFIX "lib"
    POSITION_INDEPENDENT_CODE ON
)
