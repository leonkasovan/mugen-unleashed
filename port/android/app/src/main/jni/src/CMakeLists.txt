# =============================================================================
# mugen-unleashed engine — Android build
#
# Built as a shared library (libmain.so) and loaded by SDL2's Java activity,
# which calls the engine's main() as a JNI entry point. That is the same shape
# the ikemen-droid reference build uses; see
# ikemen-develop-update/tools/generate_android_via_native.sh.
# =============================================================================
cmake_minimum_required(VERSION 3.22.1)

project(mugen_unleashed_android C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# The engine tree is seven levels up from port/android/app/src/main/jni/src:
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
file(GLOB MUGEN_ENGINE_SOURCES CONFIGURE_DEPENDS "${MUGEN_ROOT}/src/*.c")

# Entry points and platform back ends that must not be compiled here.
# Excluded: sdlmain.c/main.c are the desktop entry points (android_main.c
# supplies main here), and glrender.c/gldetect.c are the desktop-GL renderer,
# which cannot exist on Android.
#
# M8 GLES backend (RENDERING_PLAN.md section 12, opt-in, default OFF):
# -DMUGEN_GLES=1 compiles the GLES capability probe (src/gles_probe.c) and
# the vendored GLES 3.0 loader (third_party/glad-es-3.0) INSTEAD of the
# desktop GL files. The desktop glrender.c/gldetect.c stay excluded either
# way: the GLES draw path is a separate backend, not a recompile of the
# fixed-function desktop renderer (plan section 12.1). Until the GLES draw
# path lands, MUGEN_GLES only links the probe + loader; System stays the
# renderer and Gfx_Capability reports no GPU_SPRITE.
#
# NOT excluded: compat_linux.c. It is the Win32-over-POSIX shim the Linux and
# PS Vita builds use (FreeLibrary, InterlockedExchangeAdd, FontsDir, ...), and
# the engine calls those unconditionally.
# main.c stays in: it holds Screen_IsSpecial, Camera_GetX/Y, Version_Parse and
# Rng_Seed as well as the entry point; its main() is renamed Mugen_Main for this
# target (see src/main.c) and android_main.c calls it.
list(FILTER MUGEN_ENGINE_SOURCES EXCLUDE REGEX ".*/(sdlmain|glrender|gldetect|sdl2compat|sdlmixercompat)\\.c$")
option(MUGEN_GLES "M8 GLES 3.0 backend probe+loader (draw path not yet landed)" OFF)
if(MUGEN_GLES)
    list(APPEND MUGEN_ENGINE_SOURCES "${MUGEN_ROOT}/src/gles_probe.c")
    # MVP/quad CPU math the GLES vertex shader consumes (no fixed-function
    # matrix stack on GLES). Unit-tested on the host by tools/test_gl_mvp.sh.
    list(APPEND MUGEN_ENGINE_SOURCES "${MUGEN_ROOT}/src/gl_mvp.c")
    # GLES program loading + VBO quad submission (thin layer over gl_mvp.c).
    list(APPEND MUGEN_ENGINE_SOURCES "${MUGEN_ROOT}/src/gles_draw.c")
    list(APPEND MUGEN_ENGINE_SOURCES "${MUGEN_ROOT}/third_party/glad-es-3.0/glad.c")
endif()

# Native SDL2 backend; no SDL 1.2 compatibility translation units.
list(APPEND MUGEN_ENGINE_SOURCES
     "${CMAKE_CURRENT_SOURCE_DIR}/android_video.c"
     "${CMAKE_CURRENT_SOURCE_DIR}/android_main.c"
     "${CMAKE_CURRENT_SOURCE_DIR}/android_dependencies.c")

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

# Native SDL2_mixer and SDL2_ttf are required. SDL_gfx/SDL2_gfx are
# unnecessary: font scaling uses the engine's SDL2 surface operation.
# Set SDL2_MIXER_TTF_DIR to the directory holding their headers and .so files.
if(NOT DEFINED sdl2MixerTtfDir OR sdl2MixerTtfDir STREQUAL "")
    message(FATAL_ERROR "Native SDL2_mixer and SDL2_ttf prefix is required (sdl2MixerTtfDir)")
endif()
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

list(APPEND ANDROID_INCLUDES "${MUGEN_ROOT}/src" "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(main SHARED ${MUGEN_ENGINE_SOURCES})

target_include_directories(main PRIVATE ${ANDROID_INCLUDES})

# ---------------------------------------------------------------------------
# Compile definitions: Android identity is independent of the Vita port and
# of the desktop SDL 1.2-over-SDL2 build. Never force-include a compatibility
# header into either the engine or third-party translation units.
target_compile_definitions(main PRIVATE
    ANDROID_PORT=1
    MUGEN_ANDROID_SW=1
    LIBRESAMPLE_STATIC=1
    PNG_ARM_NEON_OPT=0
    PNG_ARM_NEON_CHECK_SUPPORTED=0
    PNG_SETJMP_SUPPORTED=1
)
# M8 GLES backend: MUGEN_GLES define + vendored loader include path + link
# against the NDK's GLESv3/EGL. Off unless -DMUGEN_GLES=1 (default OFF).
if(MUGEN_GLES)
    target_compile_definitions(main PRIVATE MUGEN_GLES=1)
    target_include_directories(main PRIVATE "${MUGEN_ROOT}/third_party/glad-es-3.0")
    target_link_libraries(main PRIVATE GLESv3 EGL)
    message(STATUS "M8 GLES backend: probe+loader enabled (draw path not yet landed)")
endif()
target_compile_options(main PRIVATE
    # libresample's public header is written for MSVC: it uses __declspec
    # attributes behind RESAMPLE_DECL. clang needs this to parse it. The engine
    # does the same thing for its own __cdecl/__int64 (see the Makefile's
    # MINGW note), so this is not a new dependency, just the ARM spelling.
    -fdeclspec
    -Wall
    -Werror=implicit-function-declaration
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

target_link_options(main PRIVATE -Wl,--no-undefined)

target_link_libraries(main PRIVATE ${MUGEN_ANDROID_SHARED_LIBS}
                      log android m dl z)

set_target_properties(main PROPERTIES
    OUTPUT_NAME "main"
    PREFIX "lib"
    POSITION_INDEPENDENT_CODE ON
)

# Reject accidental SDL 1.2 libraries from a caller-supplied prefix/APK inputs.
add_custom_command(TARGET main POST_BUILD
    COMMAND "${CMAKE_COMMAND}" "-DREADELF=${CMAKE_READELF}"
            "-DLIBRARIES=${MUGEN_ANDROID_SHARED_LIBS}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/check_sdl2.cmake"
    VERBATIM)
