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
# Shared engine sources. Android owns the window and entry point, with System
# by default and an optional GLES renderer. main.c also provides shared
# engine functions and exposes Mugen_Main on Android.
# ---------------------------------------------------------------------------
option(MUGEN_GLES "Opt-in GLES 3.0 sprite renderer" OFF)
option(MUGEN_MODEL3D "Opt-in GLES glTF model renderer (device qualification required)" OFF)
if(MUGEN_MODEL3D AND NOT MUGEN_GLES)
    message(FATAL_ERROR "MUGEN_MODEL3D requires MUGEN_GLES=ON on Android")
endif()
file(GLOB MUGEN_ENGINE_SOURCES CONFIGURE_DEPENDS "${MUGEN_ROOT}/src/*.c")
list(FILTER MUGEN_ENGINE_SOURCES EXCLUDE REGEX ".*/(platform_main_win32|glrender|gldetect|sdl2video)\\.c$")
if(MUGEN_GLES)
    list(APPEND MUGEN_ENGINE_SOURCES "${MUGEN_ROOT}/third_party/glad-es-3.0/glad.c")
endif()

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

# Lua 5.1.4 and bionic's FORTIFY disagree, fatally.
#
# Lua stores a string's bytes *after* its TString header and reaches them with
# getstr(ts) == (char *)(ts + 1) (third_party/lua-5.1.4/src/lobject.h). When
# bionic's fortified strchr is inlined -- which it is inside lgc.c's
# traversetable, called from propagatemark -- clang computes the object size of
# that expression as 0 bytes (a pointer one past the object), so
# __strchr_chk aborts with "strchr: prevented read past end of buffer" on the
# first table that has a __mode metafield. Observed as an instant crash in
# Lua_Init -> luaL_openlibs -> luaopen_os on Android arm64; the same code is
# fine on the desktop builds, which do not use bionic.
#
# The read is legitimate: the bytes are inside the allocation, just not inside
# the declared subobject. FORTIFY is therefore disabled for the Lua translation
# units only -- the engine and every other third-party unit keep it.
set_source_files_properties(${LUA_SRC} PROPERTIES COMPILE_OPTIONS "-U_FORTIFY_SOURCE;-D_FORTIFY_SOURCE=0")

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
list(APPEND ANDROID_INCLUDES "${sdl2MixerTtfDir}/include" "${sdl2MixerTtfDir}/include/SDL2")
message(STATUS "SDL2_mixer/SDL2_ttf: ${sdl2MixerTtfDir}/lib")

# SDL2 headers come from the cross-compiled SDL2 staged by build.bat (they are
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
        "Pass -Dsdl2IncludeDir=<path containing SDL.h> (build.bat sets it).")
endif()

list(APPEND ANDROID_INCLUDES "${MUGEN_ROOT}/src" "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(main SHARED ${MUGEN_ENGINE_SOURCES})
if(MUGEN_MODEL3D)
    include("${MUGEN_ROOT}/port/cmake/Model3D.cmake")
    mugen_enable_model3d(main "${MUGEN_ROOT}")
endif()

target_include_directories(main PRIVATE ${ANDROID_INCLUDES})

# ---------------------------------------------------------------------------
# Compile definitions: Android identity is independent of the Vita port and
# of the desktop build. All targets use native SDL2. Never force-include a compatibility
# header into either the engine or third-party translation units.
target_compile_definitions(main PRIVATE
    ANDROID_PORT=1
    SDL_MAIN_HANDLED
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
    message(STATUS "GLES 3.0 sprite renderer enabled; model3d=${MUGEN_MODEL3D}")
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
# paths come from the two variables build.bat passes. It also stages the
# libraries under jniLibs for APK packaging.
get_filename_component(SDL2_LIBDIR "${sdl2IncludeDir}/../lib" ABSOLUTE)

set(MUGEN_ANDROID_SHARED_LIBS
    "${SDL2_LIBDIR}/libSDL2.so"
    "${sdl2MixerTtfDir}/lib/libSDL2_mixer.so"
    "${sdl2MixerTtfDir}/lib/libSDL2_ttf.so")
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
