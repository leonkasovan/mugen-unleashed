# =============================================================================
#  mugen-unleashed -- PlayStation Vita port (VitaSDK, ARMv7-A)
# =============================================================================
#
#  Cross-builds the reconstructed M.U.G.E.N 1.1b1 engine for the PS Vita using
#  VitaSDK (https://github.com/vitasdk). Modelled on the VitaSDK SDL2 sample:
#
#      $VITASDK/share/gcc-arm-vita-eabi/samples/sdl2/redrectangle/CMakeLists.txt
#
#  The engine is written against **SDL 1.2**, not SDL2, and VitaSDK ships both:
#  SDL2 over the SceGxm GPU, and SDL 1.2 over vitaGL (the OpenGL 1.x/2.x
#  compatibility layer over SceGxm). This port uses the SDL 1.2 that comes with
#  VitaSDK -- $VITASDK/arm-vita-eabi/include/SDL and libSDL.a -- because the
#  78 engine modules call SDL_SetVideoMode, SDL_HWSURFACE, one video surface and
#  the fixed-function GL pipeline, none of which SDL2 provides.
#
#  Build:
#      cmake -S port/psvita -B build/psvita -G Ninja \
#            -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake \
#            -DCMAKE_BUILD_TYPE=Release
#      cmake --build build/psvita
#
#  Outputs (in the build directory):
#      mugen.self   the executable, runnable from ux0:app/MSDK00001/
#      mugen.vpk    installable homebrew, data/ included
#
#  Notes on the source set (read before changing anything):
#      * The engine compiles from ../src with the non-Windows compat layer
#        (src/compat_linux.h), exactly like the Linux target. __VITA__ is a
#        POSIX-like target: no <windows.h>, no win32 calls, mugen.rc skipped.
#      * src/sdlmain.c is Windows-only by construction (WinMain, stdio
#        redirection, DDRAW preload) and compiles to an empty object here; the
#        entry point is main() in src/main.c, as on Linux.
#      * src/sdlmixercompat.c IS built: it supplies Mix_QuerySpecEx, which
#        VitaSDK's stock SDL_mixer 1.2.12 does not export.
#      * libpng is the vitasdk one (1.6), not third_party/libpng-1.2.38. The
#        Windows build already proved the engine tolerates 1.6 (see PLANS.md),
#        and VitaSDK's libpng.a is prebuilt for the target.
#      * Lua 5.1.4, minizip 0.15 and libresample are vendored and built here,
#        version-pinned by src/symbols.h (no distro packages on this target).
#      * glad 2.1 is NOT built. It is generated for the desktop GL loader
#        (wglGetProcAddress / glXGetProcAddress + libGL.so), which does not
#        exist on the Vita. vitaGL declares and implements the GL 2.1 fixed
#        function entry points itself, so <glad.h> cannot be used on this
#        target; see MUGEN_VITA_GL and the README.
#
# =============================================================================

cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------- toolchain --
#
# Same guard as the VitaSDK sample: the Vita toolchain file must be in effect,
# and it comes from $VITASDK unless the caller passed one explicitly.

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR
      "VITASDK is not set. Run set VITASDK=C:\\vitasdk (or the setup script for this "
      "shell) and reconfigure; see port/psvita/README.md.")
  endif()
endif()

project(mugen_unleashed_psvita C)

if(NOT DEFINED ENV{VITASDK})
  message(FATAL_ERROR "The VitaSDK toolchain file was used but VITASDK is not in the environment")
endif()
set(MUGEN_VITASDK "$ENV{VITASDK}")

if(NOT EXISTS "${MUGEN_VITASDK}/arm-vita-eabi/include/SDL/SDL.h")
  message(FATAL_ERROR
    "${MUGEN_VITASDK}/arm-vita-eabi/include/SDL/SDL.h not found.\n"
    "This port links VitaSDK's SDL 1.2 (libSDL.a). Install VitaSDK's SDL 1.2 "
    "packages (vdpm sdl, sdl_gfx, sdl_mixer, sdl_ttf, vitagl) and reconfigure.")
endif()

include("${MUGEN_VITASDK}/share/vita.cmake" REQUIRED)

# ------------------------------------------------------------ repository ----
#
# The engine, the vendored libraries and the compat sources all live two
# directories above port/psvita. CMakeLists.txt files are not portable across
# parents, so everything is addressed from MUGEN_ROOT.

get_filename_component(MUGEN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(NOT EXISTS "${MUGEN_ROOT}/src/mugen.h")
  message(FATAL_ERROR
    "${MUGEN_ROOT}/src/mugen.h not found: port/psvita must stay two levels "
    "below the repository root")
endif()

set(MUGEN_SRC    "${MUGEN_ROOT}/src")
set(MUGEN_THIRD  "${MUGEN_ROOT}/third_party")

# --------------------------------------------------------------- packaging --
#
# Title ID, name and version of the LiveArea bubble. MSDK = the project's
# author field, 00001 = first title, per the TITLEID convention in the sample.

set(VITA_APP_NAME "M.U.G.E.N Unleashed")
set(MUGEN_VITA_TITLEID "MSDK00001" CACHE STRING "9-character Vita title ID")
set(MUGEN_VITA_VERSION "01.00"     CACHE STRING "APP_VER, format ##.##")

# Data zipped into the VPK. The game tree (chars/, stages/, data/, font/,
# sound/, plugins/ ...) is someone's M.U.G.E.N 1.1b1 install; this repository
# ships none of it, and the install/ tree here is a placeholder, so the default
# is "no data" and packing is opt-in:
#
#   cmake ... -DMUGEN_VITA_DATA_DIR=C:/mugen-1.1b1
#
# An empty value still produces a usable mugen.vpk -- install the game tree
# under ux0:data/mugen/ afterwards (see README.md).
set(MUGEN_VITA_DATA_DIR "" CACHE PATH
    "M.U.G.E.N directory to pack into the VPK (optional: its contents go to data/)")

# --------------------------------------------------------- video config patch --
#
# The game's data/mugen.cfg is written for a desktop, and two of its settings
# break this port:
#
#   GameWidth / GameHeight   A desktop install is usually 1280x720 or larger.
#                            vitaGL has one framebuffer -- the Vita's 960x544 --
#                            and SDL_SetVideoMode fails for anything else. The
#                            engine does not check that failure in its OpenGL
#                            path (a quirk kept from the original: "with OpenGL
#                            a failed SDL_SetVideoMode is not noticed here"), so
#                            it calls Gl_Init with no GL context and the process
#                            dies with C2-12828-1 before the first frame.
#
#   FullScreen               0 asks for a window; the Vita has none. The vita
#                            video driver fills the panel either way, but the
#                            engine takes different paths for windowed mode
#                            (window position env vars, SDL_ShowCursor).
#
# Both are rewritten when the game tree is packed, in a copy that goes into the
# VPK. The tree on disk is never modified -- install/ is the pristine reference.
# GameWidth / GameHeight: the size the engine renders at, before scaling to the
# panel.
#
# 640x480, not the panel's 960x544. The software renderer costs work per pixel
# on the ARM core (`SoftSprite_Blit` in src/softrender.c: a palette lookup and a
# blend for every pixel of every sprite), so this is the frame-rate lever:
#
#     960x544 = 522,240 px      640x480 = 307,200 px   (-41%)
#                               480x272 = 130,560 px   (-75%)
#
# 640x480 is the engine's own standard-definition target and the resolution the
# M.U.G.E.N motif is designed around, so nothing is lost in layout -- the engine
# scales 640x480 up to the panel. Try -DMUGEN_VITA_VIDEO_WIDTH=480
# -DMUGEN_VITA_VIDEO_HEIGHT=272 if matches are still short of 60 fps.
#
# (With the OpenGL renderer, if it is ever enabled, 960x544 would be the right
# choice again: the GPU does not pay this per-pixel cost.)
# 480x272 is the Vita panel's exact half, so the upscale is a clean 2x with no
# resampling artifacts, and it is 2.35x fewer pixels than 640x480. Measured on
# hardware: 640x480 gives 25-30 fps, and the software cost is per pixel, so this
# is the step that reaches 60. Raise it to 640x480 if this proves too soft.
set(MUGEN_VITA_VIDEO_WIDTH  "480" CACHE STRING
    "GameWidth written into the packed data/mugen.cfg (render size, not panel size)")
set(MUGEN_VITA_VIDEO_HEIGHT "272" CACHE STRING
    "GameHeight written into the packed data/mugen.cfg (render size, not panel size)")
set(MUGEN_VITA_FULLSCREEN   "ON"  CACHE BOOL
    "Write FullScreen = 1 into the packed data/mugen.cfg")

# Memory budget for the process, in KiB, passed to vita-make-fself as -m.
#
# Read vita-make-fself's help carefully: "Normal app: 0, System mode app:
# 0x1000 - 0x12800". The 0x1000-0x12800 range (4-74 MiB) is the *system mode*
# bracket, not the ceiling for a normal app -- 0 means "normal app, let the
# system size it", which is what the VitaSDK samples do and what this port
# needs. Passing a number there caps the process at a system-mode budget.
#
# That mistake cost two debug rounds: 131072 was rejected outright at module
# load (C1-2609-7), and 0x12000 was accepted but so small that
# `sceGxmInitialize` failed with SCE_GXM_ERROR_OUT_OF_MEMORY (0x805B0002) --
# which surfaced only as "Can't change video mode", because SDL's Vita video
# driver reports a GXM shortfall as a failed video mode.
#
# Leave it 0 unless you specifically need a system-mode budget.
set(MUGEN_VITA_MEMSIZE "0" CACHE STRING
    "vita-make-fself -m: 0 for a normal app (recommended), or 0x1000-0x12800 for system mode")

# The newlib heap (malloc / operator new).
#
# VitaSDK's crt0 defaults this to 128 MiB when `_newlib_heap_size_user` is
# undefined, which is larger than a system-mode budget and made every allocation
# fail -- the process then aborted in a C++ static constructor before main, with
# no log. port/psvita/src/vita_newlib.c defines the symbol; this sets its value.
#
# SDL's Vita video driver needs the *same* process memory for GXM, so an
# oversized heap starves `sceGxmInitialize` (SCE_GXM_ERROR_OUT_OF_MEMORY). 64 MiB
# leaves GXM and vitaGL plenty on a normal-app budget and is far more than
# M.U.G.E.N 1.1 needs on the Vita.
set(MUGEN_VITA_HEAP_MIB "64" CACHE STRING
    "newlib heap size in MiB (must leave room for GXM; 64 is comfortable)")

# An explicit budget must still fit the heap. A budget of 0 means "normal app",
# where the system decides and this check does not apply.
if(MUGEN_VITA_HEAP_MIB GREATER 0)
  math(EXPR MUGEN_VITA_HEAP_BYTES "${MUGEN_VITA_HEAP_MIB} * 1048576")
  if(MUGEN_VITA_MEMSIZE GREATER 0)
    math(EXPR _mugen_heap_kib "${MUGEN_VITA_HEAP_MIB} * 1024")
    math(EXPR _mugen_headroom_kib "${MUGEN_VITA_MEMSIZE} - ${_mugen_heap_kib}")
    if(_mugen_headroom_kib LESS 8192)
      message(FATAL_ERROR
        "MUGEN_VITA_HEAP_MIB=${MUGEN_VITA_HEAP_MIB} leaves only "
        "${_mugen_headroom_kib} KiB of the ${MUGEN_VITA_MEMSIZE} KiB budget for "
        "GXM and vitaGL, which fails with SCE_GXM_ERROR_OUT_OF_MEMORY. Lower the "
        "heap, or set MUGEN_VITA_MEMSIZE=0 for a normal-app budget.")
    endif()
    message(STATUS
      "newlib heap ${MUGEN_VITA_HEAP_MIB} MiB of the ${MUGEN_VITA_MEMSIZE} KiB "
      "budget (${_mugen_headroom_kib} KiB for GXM)")
  else()
    message(STATUS
      "newlib heap ${MUGEN_VITA_HEAP_MIB} MiB (normal-app budget: system sized)")
  endif()
else()
  math(EXPR MUGEN_VITA_HEAP_BYTES "134217728")
  message(WARNING
    "MUGEN_VITA_HEAP_MIB is 0: the heap falls back to VitaSDK's 128 MiB default, "
    "which starves GXM. Set MUGEN_VITA_HEAP_MIB.")
endif()


# Memory budget for the process, in KiB, passed to vita-make-fself as -m.
#
# Read vita-make-fself's help carefully: "Normal app: 0, System mode app:
# 0x1000 - 0x12800". The 0x1000-0x12800 range (4-74 MiB) is the *system mode*
# bracket, not the ceiling for a normal app -- 0 means "normal app, let the
# system size it", which is what the VitaSDK samples do and what this port
# needs. Passing a number there caps the process at a system-mode budget.
#
# That mistake cost two debug rounds: 131072 was rejected outright at module
# load (C1-2609-7), and 0x12000 was accepted but so small that
# `sceGxmInitialize` failed with SCE_GXM_ERROR_OUT_OF_MEMORY (0x805B0002) --
# which surfaced only as "Can't change video mode", because SDL's Vita video
# driver reports a GXM shortfall as a failed video mode.
#
# Leave it 0 unless you specifically need a system-mode budget.
set(MUGEN_VITA_MEMSIZE "0" CACHE STRING
    "vita-make-fself -m: 0 for a normal app (recommended), or 0x1000-0x12800 for system mode")

# The part of that budget given to the newlib heap (malloc/operator new).
#
# VitaSDK's crt0 defaults the heap to 128 MiB when `_newlib_heap_size_user` is
# undefined -- larger than the whole budget above, so `sceKernelAllocMemBlock`
# fails, crt0 zeroes `_newlib_heap_size`, and every allocation from then on
# returns NULL. The first allocation happens in a C++ static constructor inside
# libvitaGL.a, before main, so the console shows C2-12828-1 with no mugen.log at
# all. port/psvita/src/vita_newlib.c defines the symbol; this sets its value.
#
# The renderer written into the packed data/mugen.cfg.
#
# OpenGL is the only sane choice on the Vita: the System (software) renderer
# draws to a 16-bit surface in main memory and the CPU blits the whole frame
# through SceDisplay every frame, whereas the OpenGL path draws through
# vitaGL/SceGxm. A desktop mugen.cfg may say `system` (that is the engine's own
# default), so it is rewritten rather than trusted.
#
# `system` is still accepted, for isolating a GL problem; see README.md.
# Default to the System (software) renderer, because VitaSDK's SDL 1.2 has no
# OpenGL support: its Vita video driver (SDL_vitavideo.o) is a SceGxm 2D blitter
# that references no GL context API and never calls vglInit, so SDL_OPENGL
# yields a surface with no context and Gl_Init cannot start. The System renderer
# needs only a surface to blit to, which is exactly what that driver provides.
#
# The engine also falls back to System automatically if a requested GL renderer
# fails (see System_Init), so `opengl` here is safe to try -- it just costs a
# failed attempt on every launch. Set it if you build SDL 1.2 with vitaGL
# support yourself.
set(MUGEN_VITA_RENDERMODE "system" CACHE STRING
    "RenderMode written into the packed data/mugen.cfg (system | opengl | openglscreen)")

# ------------------------------------------------------------- vitaGL (opt) --
#
# Vendored vitaGL built with the flags the OpenGL renderer would need. Off by
# default: the software renderer is the shipping path, and the OpenGL renderer
# still cannot run on this stack (see README.md, "The OpenGL renderer"). Turning
# this on compiles and links the library so the remaining work can be done, but
# it does not by itself give a working renderer.
option(MUGEN_VITA_BUILD_VITAGL
       "Build the vendored vitaGL (needed only for the OpenGL renderer work)" OFF)

if(MUGEN_VITA_BUILD_VITAGL)
  enable_language(CXX)
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/VitaGl.cmake")
  mugen_add_vitagl(vitagl_vita)
  message(STATUS "vendored vitaGL enabled (UNPURE_TEXFORMATS, FFP shaders, UNPURE_TEXCOORDS)")
endif()

# ------------------------------------------------------------------ sources --

# The engine, one file per module of the original (see docs/ANALYSIS.md), plus
# the POSIX compat layer and the Mix_QuerySpecEx shim. Globbed rather than
# listed so this file cannot go stale as modules are added -- the engine's
# module list is owned by the top-level CMakeLists.txt and the Makefile.
file(GLOB MUGEN_ENGINE_SOURCES CONFIGURE_DEPENDS "${MUGEN_SRC}/*.c")

# Not part of the reconstructed binary, and selected per platform.
list(REMOVE_ITEM MUGEN_ENGINE_SOURCES
  "${MUGEN_SRC}/sdlmain.c")          # WinMain: Windows-only, empty object here

# Lua 5.1.4 (vendored, version-pinned by src/symbols.h).
file(GLOB MUGEN_LUA_SOURCES "${MUGEN_THIRD}/lua-5.1.4/src/*.c")
list(REMOVE_ITEM MUGEN_LUA_SOURCES
  "${MUGEN_THIRD}/lua-5.1.4/src/lua.c"     # the standalone interpreter
  "${MUGEN_THIRD}/lua-5.1.4/src/luac.c")   # the standalone compiler

# minizip 0.15 (zlib contrib), used for zipped characters.
set(MUGEN_MINIZIP_SOURCES "${MUGEN_THIRD}/minizip-0.15/unzip.c")

# libresample 0.1.3, vendored and ported to Elecbyte's multi-channel ABI.
file(GLOB MUGEN_LIBRESAMPLE_SOURCES "${MUGEN_THIRD}/libresample/src/*.c")

# ------------------------------------------------------------------- flags --

# ARMv7-A has no x87. The x86 builds pin -mfpmath=387 -fexcess-precision=standard
# to reproduce the original's 53-bit x87 intermediates; neither flag exists on
# ARM, and VFP/NEON rounds every operation correctly, so float results drift
# against the original exactly as they do on linux-arm64. -mfpu=neon and the
# -mfloat-abi are already set by the VitaSDK toolchain; do not repeat them.
set(MUGEN_VITA_OPT "-O2")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(MUGEN_VITA_OPT "-O0 -g3")
endif()

set(MUGEN_ENGINE_FLAGS
  -std=gnu11
  ${MUGEN_VITA_OPT}
  # The reconstruction aliases through unions/pointers the way the original
  # machine code did; do not let GCC exploit TBAA.
  -fno-strict-aliasing
  # Keep every function in its own section so the linker can drop the 78
  # modules' dead ends; the engine has no cross-module static constructors.
  -ffunction-sections -fdata-sections
  # Same warning baseline as the Makefile: -Wno-unused-parameter because the
  # reconstructed functions keep every original parameter, and the two
  # suppressions for code that is faithful to the original rather than clean.
  -Wall -Wno-unused-parameter -Wno-unused-result
  -Wno-stringop-truncation
  -Wno-error=incompatible-pointer-types)

# compat_linux.h supplies __int64/__cdecl and the Win32 type names; the engine
# sources that include <SDL.h> or <libresample.h> before mugen.h need it forced
# in, exactly as the Linux Makefile does.
set(MUGEN_VITA_FORCE_INCLUDE -include "${MUGEN_SRC}/compat_linux.h")

# ------------------------------------------------------------------- build --

add_executable(mugen
  ${MUGEN_ENGINE_SOURCES}
  src/vita_gl_shim.c
  src/vita_fps.c
  src/vita_newlib.c
  ${MUGEN_LUA_SOURCES}
  ${MUGEN_MINIZIP_SOURCES}
  ${MUGEN_LIBRESAMPLE_SOURCES})

target_include_directories(mugen PRIVATE
  "${MUGEN_SRC}"                                  # engine headers
  "${MUGEN_THIRD}/lua-5.1.4/src"
  "${MUGEN_THIRD}/minizip-0.15"
  "${MUGEN_THIRD}/libresample/include"
  "${MUGEN_THIRD}/libresample/src"
  src)                                            # vita_gl_shim.h

target_compile_options(mugen PRIVATE
  ${MUGEN_ENGINE_FLAGS}
  ${MUGEN_VITA_FORCE_INCLUDE}
  -DMUGEN_VITA_GL)

# LIBRESAMPLE_STATIC drops __declspec(dllimport) from libresample.h, which is
# meaningless (and rejected) off Windows.
target_compile_definitions(mugen PRIVATE
  LIBRESAMPLE_STATIC
  MUGEN_VITA
  MUGEN_VITA_DATA_DIR="${MUGEN_VITA_DATA_DIR}"
  MUGEN_VITA_HEAP_BYTES=${MUGEN_VITA_HEAP_BYTES})

# One shot at silencing the vendored libraries' warnings: -w for them only.
set_source_files_properties(
  ${MUGEN_LUA_SOURCES} ${MUGEN_MINIZIP_SOURCES} ${MUGEN_LIBRESAMPLE_SOURCES}
  PROPERTIES COMPILE_OPTIONS "-w;-fno-strict-aliasing")

# Lua: no LUA_USE_LINUX. That macro pulls in <dlfcn.h> and readline-based lua.c,
# neither of which exists on the Vita; luaconf.h then also reaches for MSVC's
# _setjmp/_longjmp. The plain ANSI configuration is what this build wants -- the
# engine only uses the library, never the standalone interpreter.

# ------------------------------------------------------------- link step ----

# ------------------------------------------------------- pkg-config setup --

# VitaSDK's .pc files describe the target libraries. They are read by a host
# pkg-config whose search path points at the SDK, because there is no Vita
# pkg-config binary that runs on Windows.
#
# Three VitaSDK packaging problems are handled here, once, so that a plain
# `cmake -S port/psvita -B ...` works with nothing but VITASDK set:
#
#  1. freetype2.pc Requires: libpng -- the pre-1.6 module name. VitaSDK ships
#     libpng16.pc only, so SDL_ttf (whose Requires.private reaches freetype)
#     cannot resolve. A shell module is generated into the build directory.
#
#  2. The SDK's own wrapper, arm-vita-eabi-pkg-config, is a bash script with no
#     file extension: Windows refuses to exec it (CMake reports "inappropriate
#     file type or format"), and its PKG_CONFIG_LIBDIR is ':'-separated, which
#     MSYS pkg-config misparses at the drive letter. A host pkg-config is used
#     instead, with the search path set explicitly.
#
#  3. The toolchain file sets CMAKE_SYSTEM_NAME to "Generic", so WIN32 is false
#     even on Windows. Host behaviour is what matters, hence CMAKE_HOST_WIN32.

# (1) the libpng shell module, for every host
set(MUGEN_VITA_PC_SHIM "${CMAKE_BINARY_DIR}/pkgconfig-shim")
file(MAKE_DIRECTORY "${MUGEN_VITA_PC_SHIM}")
file(WRITE "${MUGEN_VITA_PC_SHIM}/libpng.pc"
  "# Generated by port/psvita/CMakeLists.txt -- do not edit.\n"
  "# freetype2.pc (shipped by VitaSDK) Requires: libpng, the pre-1.6 module\n"
  "# name; VitaSDK provides libpng16 only.\n"
  "Name: libpng\n"
  "Description: libpng shell module for freetype2.pc\n"
  "Version: 1.6.0\n"
  "Requires: libpng16\n")

# (2)/(3) pick the pkg-config and set its search path
if(CMAKE_HOST_WIN32)
  file(TO_CMAKE_PATH "${MUGEN_VITASDK}" MUGEN_VITASDK_CMAKE)
  set(ENV{VITASDK} "${MUGEN_VITASDK_CMAKE}")

  set(_mugen_pc "${PKG_CONFIG_EXECUTABLE}")
  if(NOT _mugen_pc OR _mugen_pc MATCHES "arm-vita-eabi-pkg-config$")
    find_program(_mugen_host_pc NAMES pkg-config pkgconf)
    if(NOT _mugen_host_pc)
      message(FATAL_ERROR
        "No host pkg-config on PATH. VitaSDK's wrapper is a shell script and "
        "cannot stand in for one; install pkg-config or pass "
        "-DPKG_CONFIG_EXECUTABLE=<path>.")
    endif()
    set(_mugen_pc "${_mugen_host_pc}")
  endif()
  set(PKG_CONFIG_EXECUTABLE "${_mugen_pc}" CACHE STRING
      "pkg-config command used for the Vita SDK" FORCE)
endif()

# The search path is set for the configure step only. Entries are
# ';'-separated because a ':' is ambiguous with the MSYS drive letter.
set(MUGEN_VITA_PC_DIRS
  "${MUGEN_VITASDK}/arm-vita-eabi/lib/pkgconfig"
  "${MUGEN_VITA_PC_SHIM}")
foreach(_extra IN LISTS MUGEN_VITA_PKGCONFIG_DIRS)
  if(_extra)
    list(APPEND MUGEN_VITA_PC_DIRS "${_extra}")
  endif()
endforeach()
list(REMOVE_DUPLICATES MUGEN_VITA_PC_DIRS)
set(ENV{PKG_CONFIG_LIBDIR} "${MUGEN_VITA_PC_DIRS}")
set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "")

find_package(PkgConfig REQUIRED)
message(STATUS "pkg-config   ${PKG_CONFIG_EXECUTABLE}")
message(STATUS "  .pc dirs   ${MUGEN_VITA_PC_DIRS}")

pkg_check_modules(MUGEN_VITA_PKGS REQUIRED
  sdl          # SDL 1.2.16 over vitaGL
  SDL_gfx
  SDL_mixer
  SDL_ttf
  zlib
  libpng16)

# vitaGL: the GL 2.1 compatibility implementation SDL 1.2's OpenGL video driver
# sits on. It is not a pkg-config module in current VitaSDK releases; the
# library is libvitaGL.a and the header is vitaGL.h, both in the usual target
# search root.
#
# Its own dependency chain has to be linked too, and none of it is discoverable
# through a .pc file:
#   libvitaGL.a        GL 2.1 over SceGxm
#   libvitashark.a     runtime shader compiler front end (vitaShaRK package)
#   libSceShaccCgExt.a the ShaccCg shader compiler itself
#   libmathneon.a      NEON vector maths
#
# Each name below is checked, so a VitaSDK update that renames one fails here
# with a readable message rather than as a wall of undefined Sce*/shark_* at
# link time.
set(MUGEN_VITA_VITAGL_DEPS "")
foreach(_gl_dep
    vitaGL      # the GL implementation itself
    vitashark   # vitaShaRK: shader compiler front end
    SceShaccCgExt  # the ShaccCg compiler
    mathneon)   # NEON maths helpers
  find_library(MUGEN_VITA_LIB_${_gl_dep} NAMES ${_gl_dep})
  if(NOT MUGEN_VITA_LIB_${_gl_dep})
    message(FATAL_ERROR
      "lib${_gl_dep}.a not found. SDL 1.2's OpenGL video driver needs vitaGL "
      "and its dependencies; install them into "
      "${MUGEN_VITASDK}/arm-vita-eabi (vdpm install vitaGL) and reconfigure.")
  endif()
  list(APPEND MUGEN_VITA_VITAGL_DEPS "${MUGEN_VITA_LIB_${_gl_dep}}")
endforeach()
set(MUGEN_VITA_VITAGL_LIB "${MUGEN_VITA_LIB_vitaGL}")

target_include_directories(mugen PRIVATE
  ${MUGEN_VITA_PKGS_INCLUDE_DIRS}
  "${MUGEN_VITASDK}/arm-vita-eabi/include/SDL")

target_link_directories(mugen PRIVATE ${MUGEN_VITA_PKGS_LIBRARY_DIRS})

# The link is fully static, so the codec libraries SDL_mixer pulls in (FLAC,
# vorbis, ogg, mikmod, mpg123) and SDL_ttf's freetype have to be named. VitaSDK's
# .pc files list them only in Requires.private / Libs.private, which pkg-config
# emits with --static, and CMake's pkg_check_modules does not pass that flag.
# Listing the modules explicitly and taking their STATIC_ variables is what
# produces the complete archive list.
pkg_check_modules(MUGEN_VITA_STATIC_PKGS REQUIRED
  sdl SDL_gfx SDL_mixer SDL_ttf zlib libpng16 freetype2 bzip2
  flac vorbisfile vorbis ogg libmikmod libmpg123)

# The SCE userland stubs SDL 1.2 and vitaGL call: SceGxm/SceGxmInternal for the
# GPU, SceDisplay for the framebuffer, SceCtrl/SceTouch/SceHid/SceMotion for
# input, SceAudio, SceIofilemgr for file I/O, SceAppMgr for the app lifecycle,
# SceCommonDialog for the error/system dialogs, ScePower for the sleep state.
#
# pkg-config's sdl.pc already lists most of these, but the list is spelled out
# because it is exactly what must be present for the link to close -- a change
# to sdl.pc in a VitaSDK update would otherwise silently break the build with
# undefined Sce* symbols.
set(MUGEN_VITA_STUBS
  SceGxm_stub
  SceDisplay_stub
  SceCtrl_stub
  SceTouch_stub
  SceHid_stub
  SceMotion_stub
  SceAudio_stub
  SceIofilemgr_stub
  SceAppMgr_stub
  SceAppUtil_stub
  SceCommonDialog_stub
  ScePower_stub
  SceProcessmgr_stub
  SceSysmodule_stub
  SceKernelThreadMgr_stub
  SceLibKernel_stub
  # The DMA controller vitaGL uses to upload vertex/texture data.
  SceKernelDmacMgr_stub
  # taiHEN: vitaGL hooks the system shader compiler in memory at start-up
  # (vitaShaRK's approach). Linking taihen_stub is what makes that legal for a
  # homebrew; vita_create_self below marks the SELF UNSAFE for the same reason.
  taihen_stub
  # The ShaccCg shader compiler vitaGL calls through vitaShaRK. Its symbols live
  # in the SceShaccCg userland stub, not in libSceShaccCgExt.a (which is the
  # compiler itself, linked above as part of the vitaGL chain).
  SceShaccCg_stub)

target_link_libraries(mugen PRIVATE
  ${MUGEN_VITA_VITAGL_DEPS}
  ${MUGEN_VITA_STATIC_PKGS_STATIC_LIBRARIES}
  ${MUGEN_VITA_PKGS_LIBRARIES}
  ${MUGEN_VITA_STUBS}
  m
  # vitaGL's GLSL shader preprocessor is C++, so a C program that links
  # libvitaGL.a also needs libstdc++ (which carries libsupc++'s ABI runtime;
  # VitaSDK ships no separate libc++abi). Its worker threads need libpthread.
  stdc++
  pthread
  c)

# -Wl,--gc-sections pairs with the per-function sections above; -Wl,-q is
# already in the toolchain's CMAKE_C_FLAGS (required for vita-elf-create's
# relocation table).
#
# -Wl,--allow-multiple-definition: libmikmod (pulled in by SDL_mixer's
# MOD-file loader) exports Player_Free, Player_Load and friends in the same
# global namespace as the engine's own src/char.c Player_Free. The two are
# unrelated -- mikmod's takes a MODULE* and is only reachable from
# SDL_mixer's MikMod path, which this engine never selects because it uses
# the SDL_mixer BGM subsystem for WAV/OGG/MP3 only. There is no way to
# namespace a static archive, so the flag lets the first definition win. The
# engine's object files are all on the command line before the archives, so
# that is the engine's Player_Free -- which is the one that must win.
#
# This is a port-only concession: the Windows and Linux builds get SDL_mixer
# as a DLL/.so, where the collision cannot occur at link time.
target_link_options(mugen PRIVATE
  -Wl,--gc-sections
  -Wl,--allow-multiple-definition)

# vita_create_self derives the input path from ${CMAKE_CURRENT_BINARY_DIR}/${target}
# with no extra suffix handling, so the ELF keeps the target's plain name. Do
# not set a SUFFIX here: the SELF step would look for a file that is not there.
set_target_properties(mugen PROPERTIES OUTPUT_NAME mugen)

# ------------------------------------------------------- SELF / VPK / data --

# The SELF, built compressed. UNSAFE is required: vitaGL reaches the GPU
# through SceGxm and vitaShaRK hooks the system shader compiler in memory with
# taiHEN, both of which a "safe" (sandboxed) eboot.bin cannot do. vita.cmake
# omits vita-make-fself's -s accordingly.
#
# STRIPPED is deliberately NOT set: it is passed to vita-elf-create (-s), and
# keeping the symbols costs ~2 MB in the VPK while making crash addresses
# resolvable with arm-vita-eabi-addr2line. Worth far more than the size on a
# first port.
#
# MEMSIZE must stay inside vita-make-fself's accepted range; see the option's
# comment above.
if(MUGEN_VITA_MEMSIZE GREATER 0)
  # A system-mode budget was requested explicitly.
  vita_create_self(mugen.self mugen
    UNSAFE
    MEMSIZE ${MUGEN_VITA_MEMSIZE})
else()
  # Normal app: no -m at all, so the system sizes the process. This is what the
  # VitaSDK samples do, and it is what GXM and vitaGL expect.
  vita_create_self(mugen.self mugen
    UNSAFE)
endif()

# The LiveArea assets are the ones the VitaSDK sample ships; they are MIT/zlib
# artwork, not Elecbyte's. Replace sce_sys/ with your own if you distribute a
# custom bubble.
set(MUGEN_VITA_SCE_SYS "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys")

if(MUGEN_VITA_DATA_DIR)
  if(NOT IS_DIRECTORY "${MUGEN_VITA_DATA_DIR}")
    message(FATAL_ERROR
      "MUGEN_VITA_DATA_DIR=${MUGEN_VITA_DATA_DIR} is not a directory")
  endif()
  foreach(_required data/mugen.cfg data/mugen1/system.def font)
    if(NOT EXISTS "${MUGEN_VITA_DATA_DIR}/${_required}")
      message(WARNING
        "${MUGEN_VITA_DATA_DIR}/${_required} is missing: the VPK will install, "
        "but the game will exit at start-up. Point MUGEN_VITA_DATA_DIR at a "
        "complete M.U.G.E.N 1.1b1 tree.")
    endif()
  endforeach()

  # --- patch data/mugen.cfg into a build-directory copy ---------------------
  #
  # vita-pack-vpk refuses a destination that is already in the archive, so the
  # patched file cannot simply be added after the tree; it has to be substituted
  # at the point the tree is enumerated (see below). The source tree is never
  # touched.
  set(MUGEN_VITA_CFG_SRC "${MUGEN_VITA_DATA_DIR}/data/mugen.cfg")
  set(MUGEN_VITA_CFG_OUT "${CMAKE_CURRENT_BINARY_DIR}/vita-mugen.cfg")
  if(EXISTS "${MUGEN_VITA_CFG_SRC}")
    file(READ "${MUGEN_VITA_CFG_SRC}" _cfg)

    # Ini keys are case-insensitive in the engine, so match any casing and keep
    # whatever spelling and spacing the file uses.
    string(REGEX REPLACE
      "([Gg][Aa][Mm][Ee][Ww][Ii][Dd][Tt][Hh][ \t]*=[ \t]*)[0-9]+"
      "\\1${MUGEN_VITA_VIDEO_WIDTH}" _cfg "${_cfg}")
    string(REGEX REPLACE
      "([Gg][Aa][Mm][Ee][Hh][Ee][Ii][Gg][Hh][Tt][ \t]*=[ \t]*)[0-9]+"
      "\\1${MUGEN_VITA_VIDEO_HEIGHT}" _cfg "${_cfg}")

    if(MUGEN_VITA_FULLSCREEN)
      string(REGEX REPLACE
        "([Ff][Uu][Ll][Ll][Ss][Cc][Rr][Ee][Ee][Nn][ \t]*=[ \t]*)[0-9]+"
        "\\11" _cfg "${_cfg}")
    else()
      string(REGEX REPLACE
        "([Ff][Uu][Ll][Ll][Ss][Cc][Rr][Ee][Ee][Nn][ \t]*=[ \t]*)[0-9]+"
        "\\10" _cfg "${_cfg}")
    endif()

    # RenderMode takes a word, not a number, and it is also mentioned in the
    # file's comments (";Ignored if RenderMode=System ..."). Matching is
    # therefore anchored on a preceding newline, which comments in this file
    # never have directly before the key, so only the real setting is rewritten.
    if(MUGEN_VITA_RENDERMODE)
      string(REGEX REPLACE
        "\n([Rr][Ee][Nn][Dd][Ee][Rr][Mm][Oo][Dd][Ee][ \t]*=[ \t]*)[A-Za-z]+"
        "\n\\1${MUGEN_VITA_RENDERMODE}" _cfg "${_cfg}")
    endif()

    # Turn the on-screen frame counter on. The engine can only toggle it with
    # the console's "fps" command, which needs a backquote key -- unreachable on
    # a Vita. config.c now reads [Debug] ShowFPS, so it can be enabled here.
    # The frame rate is also written to mugen.log every 10 s (port/psvita/src/
    # vita_fps.c), which is the reading to quote in a bug report.
    # Anchored on a preceding newline so comments are not matched: this file
    # contains ";If Debug = 1, this will be ignored.", which a looser pattern
    # turned into "ShowFPS = 1, this will be ignored."
    string(REGEX REPLACE
      "\n([Dd][Ee][Bb][Uu][Gg][ \t]*=[ \t]*[0-9]+)\n"
      "\n\\1\nShowFPS = 1\n" _cfg "${_cfg}")

    # --- real joystick button indices for the Vita pad ----------------------
    #
    # config.c seeds the joystick bindings for a *generic* pad:
    #
    #     joy_keys[KEY_JUMP] = 1; joy_keys[KEY_CROUCH] = 2;
    #     joy_keys[KEY_LEFT] = 3; joy_keys[KEY_RIGHT] = 4;
    #
    # which on SDL's Vita driver means Circle jumps, Cross crouches, Square
    # walks left and L walks right -- and A/B/C/X/Y/Z are all left unassigned
    # (Config_FillUnassigned), so no attack button works at all. The bindings
    # are only overridden by a [P1 Joystick]/[P2 Joystick] section, which the
    # shipped mugen.cfg does not contain.
    #
    # The indices below are from SDL's Vita joystick driver (SDL_sysjoystick.o),
    # confirmed on hardware with build/psvita/input-mapper:
    #
    #   0 Triangle  1 Circle  2 Cross   3 Square   4 L   5 R
    #   6 Down      7 Left    8 Up      9 Right   10 Select  11 Start
    #   12 buttons, 4 axes, 0 hats (the d-pad is buttons, not a hat)
    #
    # Layout is the conventional PS one: Square/Triangle/Circle/Cross = A/B/C/X.
    #
    # [P1 Keys] Start stays at 13 (Return).
    #
    # An earlier revision moved it to 283 (F2) so that Circle's Return could not
    # pause a fight. That was wrong: the menus do not read screen->key at all,
    # they read screen->input, which Screen_ReadInput computes as
    #
    #     Input_FindKey(input_keys, 2, screen->key_code)
    #
    # where `input_keys` is the built-in keyboard table in src/system.c
    # (Jump = SDLK_UP, ..., Start = SDLK_RETURN). A key reaches a menu only if it
    # is present in that table, so re-binding Start away from Return removed the
    # only route by which Return could confirm anything -- Circle stopped
    # confirming. The pause collision is instead avoided by not binding Circle to
    # the C attack in src/config.c.
    #
    # FirstRun = 0: the "This is 1 the first time you run MUGEN" flag.
    #
    # With it set, the engine opens the licence/info panel on the title screen
    # before the mode select, and then REWRITES mugen.cfg to clear the flag
    # (config.c, "Clear FirstRun once it has been seen") -- on the Vita that
    # means writing into the app directory, after which the packed config and
    # the on-disk one disagree. Packing it as 0 avoids both.
    #
    # Anchored on newlines so the comment above the value (";This is 1 the first
    # time you run MUGEN.") cannot match -- the same trap as ShowFPS below.
    string(REGEX REPLACE
      "\n([Ff][Ii][Rr][Ss][Tt][Rr][Uu][Nn][ \t]*=[ \t]*)1\n"
      "\n\\10\n" _cfg "${_cfg}")

    # Verify the patches actually applied.
    #
    # These substitutions are easy to lose: an edit to this block once removed
    # the FirstRun patch outright, and the build happily shipped a VPK with
    # FirstRun = 1. A wrong config is invisible until it reaches the console, so
    # a missing substitution is now a configure error rather than a surprise.
    foreach(_expect
        "GameWidth = ${MUGEN_VITA_VIDEO_WIDTH}"
        "GameHeight = ${MUGEN_VITA_VIDEO_HEIGHT}"
        "FirstRun = 0"
        "ShowFPS = 1")
      string(FIND "${_cfg}" "${_expect}" _found)
      if(_found EQUAL -1)
        message(FATAL_ERROR
          "mugen.cfg patch did not apply: expected \"${_expect}\" in the packed "
          "config. Fix the patch in port/psvita/CMakeLists.txt; shipping the "
          "pristine value would change behaviour on the console.")
      endif()
    endforeach()
    if(MUGEN_VITA_FULLSCREEN)
      string(FIND "${_cfg}" "FullScreen = 1" _found)
      if(_found EQUAL -1)
        message(FATAL_ERROR "mugen.cfg patch did not apply: expected \"FullScreen = 1\"")
      endif()
    endif()

    file(WRITE "${MUGEN_VITA_CFG_OUT}" "${_cfg}")
    message(STATUS "mugen.cfg patched: GameWidth ${MUGEN_VITA_VIDEO_WIDTH}, "
                   "GameHeight ${MUGEN_VITA_VIDEO_HEIGHT}, "
                   "FullScreen ${MUGEN_VITA_FULLSCREEN}, "
                   "RenderMode ${MUGEN_VITA_RENDERMODE}")
  else()
    set(MUGEN_VITA_CFG_OUT "")
    message(WARNING
      "${MUGEN_VITA_CFG_SRC} not found: the VPK carries the tree's config "
      "unchanged, so GameWidth/GameHeight are whatever the desktop value was.")
  endif()

  # --- replace the four renderer shaders with the vitaGL-compatible versions --
  #
  # data/{indexed,rgba}.{vert,frag} ship with M.U.G.E.N 1.1b1 and are written in
  # GLSL 1.20 built-ins (gl_Vertex, gl_MultiTexCoord0, gl_TexCoord[0]) that
  # vitaGL's translator does not handle, plus sampler1D/texture1D which its GLSL
  # shim does not provide. port/psvita/shaders/ holds rewritten equivalents whose
  # PalFX maths is unchanged. They are substituted here rather than edited in the
  # tree, so install/ stays pristine and the Windows/Linux builds keep using the
  # originals.
  #
  # NOTE: these only matter once the renderer draws through shaders. As things
  # stand the software renderer is the shipping path: vitaGL's immediate mode
  # (glBegin/glEnd) always renders with its own fixed-function shaders and never
  # consults a bound program, so src/glrender.c's glUseProgram has no effect
  # there. See README.md, "The OpenGL renderer".
  set(MUGEN_VITA_SHADERS "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
  foreach(_shader indexed.vert indexed.frag rgba.vert rgba.frag)
    if(EXISTS "${MUGEN_VITA_SHADERS}/${_shader}")
      set(MUGEN_VITA_SHADER_${_shader} "${MUGEN_VITA_SHADERS}/${_shader}")
    else()
      message(WARNING "${MUGEN_VITA_SHADERS}/${_shader} missing; the VPK keeps the original")
      set(MUGEN_VITA_SHADER_${_shader} "")
    endif()
  endforeach()

  message(STATUS "mugen.vpk packs ${MUGEN_VITA_DATA_DIR} as /data")
else()
  message(STATUS
    "mugen.vpk packs no game data (set MUGEN_VITA_DATA_DIR to include a "
    "M.U.G.E.N 1.1b1 install; see port/psvita/README.md)")
endif()

# The VPK is packed in one pass, by hand rather than through vita_create_vpk,
# for one reason: vita_create_vpk's FILE argument takes a src/dest pair per
# file, so it can add the LiveArea assets but not a whole directory. The game
# data is a directory (potentially thousands of files), and vita-pack-vpk's
# --add accepts "src=dst" for directories directly. Building the SFO and the
# archive explicitly keeps that in one command.
#
# Outputs mirror what vita_create_vpk would produce: mugen.vpk next to
# mugen.self, in the build directory.
set(MUGEN_VITA_VPK          "${CMAKE_CURRENT_BINARY_DIR}/mugen.vpk")
set(MUGEN_VITA_SFO          "${CMAKE_CURRENT_BINARY_DIR}/param.sfo")
set(MUGEN_VITA_EBOOT        "${CMAKE_CURRENT_BINARY_DIR}/mugen.self")

add_custom_command(
  OUTPUT ${MUGEN_VITA_SFO}
  COMMAND ${VITA_MKSFOEX}
          -s APP_VER=${MUGEN_VITA_VERSION}
          -s TITLE_ID=${MUGEN_VITA_TITLEID}
          "${VITA_APP_NAME}" ${MUGEN_VITA_SFO}
  COMMENT "Generating param.sfo for mugen.vpk"
  VERBATIM)

set(_mugen_vpk_args
  -s ${MUGEN_VITA_SFO}
  -b ${MUGEN_VITA_EBOOT}
  -a ${MUGEN_VITA_SCE_SYS}/icon0.png=sce_sys/icon0.png
  -a ${MUGEN_VITA_SCE_SYS}/livearea/contents/bg.png=sce_sys/livearea/contents/bg.png
  -a ${MUGEN_VITA_SCE_SYS}/livearea/contents/startup.png=sce_sys/livearea/contents/startup.png
  -a ${MUGEN_VITA_SCE_SYS}/livearea/contents/template.xml=sce_sys/livearea/contents/template.xml)

if(MUGEN_VITA_DATA_DIR)
  # Each top-level entry of the game tree is added by name, so that the tree's
  # contents land at /data/... rather than nested one level deeper.
  # vita-pack-vpk's "src=dst" copies the directory *itself* to dst, so
  # "-a <tree>=data" would produce data/<tree-name>/...; adding
  # <tree>/chars=chars and friends produces /chars/... as intended.
  #
  # data/ is enumerated one level further rather than added whole, so that
  # data/mugen.cfg can be replaced by the patched copy. vita-pack-vpk rejects a
  # duplicate destination, so substitution has to happen here.
  #
  # Entries are enumerated at configure time; re-run cmake after adding a
  # directory to the game tree.
  # Anything this project itself builds is not game data. install/ is a working
  # M.U.G.E.N tree and is also where this project's test executables get copied,
  # so a stale mugen_unleashed.exe (a 1.7 MB Windows PE) would otherwise be
  # packed into the VPK. The tree itself is left alone.
  set(_mugen_data_exclude
    mugen_unleashed.exe mugen_unleashed.pdb mugen-static.exe
    mugen-static-stripped.exe mugen-linux-x64 mugen-linux-arm64 mugen-linux-x86
    mugen.log mugen-match.log)

  file(GLOB _mugen_data_entries RELATIVE "${MUGEN_VITA_DATA_DIR}"
       "${MUGEN_VITA_DATA_DIR}/*")
  foreach(_entry IN LISTS _mugen_data_entries)
    if(_entry IN_LIST _mugen_data_exclude)
      message(STATUS "  not packing ${_entry} (this project's own build output)")
      continue()
    endif()
    if(_entry STREQUAL "data" AND MUGEN_VITA_CFG_OUT)
      file(GLOB _mugen_data_files RELATIVE "${MUGEN_VITA_DATA_DIR}/data"
           "${MUGEN_VITA_DATA_DIR}/data/*")
      foreach(_sub IN LISTS _mugen_data_files)
        if(_sub STREQUAL "mugen.cfg")
          list(APPEND _mugen_vpk_args -a "${MUGEN_VITA_CFG_OUT}=data/mugen.cfg")
        elseif(MUGEN_VITA_SHADER_${_sub})
          list(APPEND _mugen_vpk_args -a "${MUGEN_VITA_SHADER_${_sub}}=data/${_sub}")
        else()
          list(APPEND _mugen_vpk_args
               -a "${MUGEN_VITA_DATA_DIR}/data/${_sub}=data/${_sub}")
        endif()
      endforeach()
    else()
      list(APPEND _mugen_vpk_args -a "${MUGEN_VITA_DATA_DIR}/${_entry}=${_entry}")
    endif()
  endforeach()
endif()

add_custom_command(
  OUTPUT ${MUGEN_VITA_VPK}
  COMMAND ${VITA_PACK_VPK} ${_mugen_vpk_args} ${MUGEN_VITA_VPK}
  DEPENDS ${MUGEN_VITA_SFO} ${MUGEN_VITA_EBOOT} mugen.self
  COMMENT "Building mugen.vpk"
  VERBATIM)

add_custom_target(mugen-vpk ALL DEPENDS ${MUGEN_VITA_VPK})

# ------------------------------------------------------------------ summary --

message(STATUS "")
message(STATUS "mugen-unleashed -- PS Vita")
message(STATUS "  VitaSDK      ${MUGEN_VITASDK}")
message(STATUS "  Build type   ${CMAKE_BUILD_TYPE}")
message(STATUS "  SDL 1.2      ${MUGEN_VITA_PKGS_sdl_VERSION} (VitaSDK arm-vita-eabi)")
message(STATUS "  vitaGL       ${MUGEN_VITA_VITAGL_LIB}")
message(STATUS "  Title        ${VITA_APP_NAME} (${MUGEN_VITA_TITLEID} v${MUGEN_VITA_VERSION})")
message(STATUS "  Memory       budget ${MUGEN_VITA_MEMSIZE} KiB, newlib heap ${MUGEN_VITA_HEAP_MIB} MiB")
message(STATUS "  Output       ${CMAKE_CURRENT_BINARY_DIR}/mugen.self")
message(STATUS "               ${CMAKE_CURRENT_BINARY_DIR}/mugen.vpk")
message(STATUS "")
message(STATUS "  Renderer: set `rendermode = opengl` in data/mugen.cfg on the Vita.")
message(STATUS "  Floating point: no x87 on ARMv7, so mugen.log differs from win32")
message(STATUS "  on the numeric lines by design. See port/psvita/README.md.")
message(STATUS "")
