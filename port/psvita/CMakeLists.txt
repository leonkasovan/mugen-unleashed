# =============================================================================
#  mugen-unleashed -- PlayStation Vita port (VitaSDK, ARMv7-A)
# =============================================================================
#
#  Cross-builds the reconstructed M.U.G.E.N 1.1b1 engine for the PS Vita using
#  VitaSDK (https://github.com/vitasdk). Modelled on the VitaSDK SDL2 sample:
#
#      $VITASDK/share/gcc-arm-vita-eabi/samples/sdl2/redrectangle/CMakeLists.txt
#
#  Native VitaSDK SDL2 supplies input/audio/fonts and System rendering.
#  OpenGL/OpenGLScreen remain owned directly by the vitaGL display adapter.
#  Shared engine surface helpers do not require an SDL1 binary runtime.
#
#  Build:
#      cmake -S port/psvita -B build/psvita -G Ninja \
#            -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake \
#            -DCMAKE_BUILD_TYPE=Release
#      cmake --build build/psvita
#
#  Outputs (in the build directory):
#      mugen-psvita.self  the executable, runnable from ux0:app/MUGEN0001/
#      mugen-psvita.vpk   installable homebrew, data/ included
#
#  Notes on the source set (read before changing anything):
#      * The engine compiles from ../src with the non-Windows compat layer
#        (src/compat_linux.h), exactly like the Linux target. __VITA__ is a
#        POSIX-like target: no <windows.h>, no win32 calls, mugen.rc skipped.
#      * src/platform_main_win32.c is Windows-only by construction (WinMain, stdio
#        redirection, DDRAW preload) and compiles to an empty object here; the
#        entry point is main() in src/main.c, as on Linux.
#      * src/sdl2audio.c IS built: it supplies Sound_QueryMixerSpec, which
#        stock SDL2_mixer does not export.
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

if(NOT EXISTS "${MUGEN_VITASDK}/arm-vita-eabi/include/SDL2/SDL.h")
  message(FATAL_ERROR
    "${MUGEN_VITASDK}/arm-vita-eabi/include/SDL2/SDL.h not found.\n"
    "This port requires native VitaSDK SDL2, SDL2_mixer and SDL2_ttf. Install "
    "packages (vdpm install sdl2 sdl2_mixer sdl2_ttf vitaGL) and reconfigure.")
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
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Config.cmake")

# --------------------------------------------------------------- packaging --
#
# Title ID, name and version of the LiveArea bubble. MSDK = the project's
# author field, 00001 = first title, per the TITLEID convention in the sample.

set(VITA_APP_NAME "M.U.G.E.N Unleashed")
set(MUGEN_VITA_TITLEID "MUGEN0001" CACHE STRING "9-character Vita title ID")
set(MUGEN_VITA_VERSION "01.00"     CACHE STRING "APP_VER, format ##.##")

# Data zipped into the VPK. The game tree (chars/, stages/, data/, font/,
# sound/, plugins/ ...) is someone's M.U.G.E.N 1.1b1 install; this repository
# ships none of it, and the install/ tree here is a placeholder, so the default
# is "no data" and packing is opt-in:
#
#   cmake ... -DMUGEN_VITA_DATA_DIR=C:/mugen-1.1b1
#
# An empty value still produces a usable mugen-psvita.vpk -- install the game tree
# under ux0:data/mugen/ afterwards (see README.md).
set(MUGEN_VITA_DATA_DIR "" CACHE PATH
    "M.U.G.E.N directory to pack into the VPK (optional: its contents go to data/)")

# ------------------------------------------------------- platform config --
#
# The Vita's settings live in port/psvita/mugen.psvita.cfg, which the VPK packs
# as data/mugen.psvita.cfg exactly as written -- no build step and no flag
# rewrites a value in it. The engine merges it over the game tree's own
# data/mugen.cfg at start-up, per key (src/configmerge.c): a key the platform
# file names wins, everything else keeps the game's value.
#
# That is why nothing here patches data/mugen.cfg any more. The game's file is
# packed unchanged, so the tree stays the user's: the options screen writes back
# into it, and a value baked in at build time would look like a setting that
# refuses to change. The keys this port needs are all in the platform file:
#
#   GameWidth / GameHeight   a desktop tree is usually 1280x720 or larger, and
#                            vitaGL has one framebuffer -- the Vita's 960x544.
#                            SDL_SetVideoMode fails for anything else and the
#                            engine does not check that failure in its OpenGL
#                            path (a quirk kept from the original), so it calls
#                            Gl_Init with no context and dies with C2-12828-1.
#   FullScreen               0 asks for a window; the Vita has none.
#   RenderMode               OpenGL selects the native vitaGL path (src/vita_video.c).
#   Precache                 0: the background loader's 4 KiB stack overflowed
#                            on hardware.
#   FirstRun                 1 opens the licence panel and then rewrites
#                            data/mugen.cfg, which a packed VPK cannot do.
#
# The file is required: a VPK without it starts with the desktop values above
# and dies before the first frame, so a missing or invalid one is a configure
# error rather than a silent downgrade.
set(MUGEN_VITA_CFG "${CMAKE_CURRENT_SOURCE_DIR}/mugen.psvita.cfg" CACHE FILEPATH
    "platform config packed verbatim as data/mugen.psvita.cfg")
if(NOT MUGEN_VITA_CFG)
  message(FATAL_ERROR
    "MUGEN_VITA_CFG is empty. The Vita port needs a platform config: it is "
    "merged over the game's data/mugen.cfg at start-up and carries the "
    "resolution, RenderMode, FullScreen and Precache this device needs.")
endif()
if(NOT EXISTS "${MUGEN_VITA_CFG}" OR IS_DIRECTORY "${MUGEN_VITA_CFG}")
  message(FATAL_ERROR "MUGEN_VITA_CFG=${MUGEN_VITA_CFG} does not exist")
endif()

# Validate what it actually says, in its actual groups, before packing it.
# Reading the file the build ships is the point: a typo in RenderMode or a
# 640x480 OpenGL output is caught here instead of on the console.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${MUGEN_VITA_CFG}")
file(READ "${MUGEN_VITA_CFG}" _mugen_vita_cfg_text)
mugen_vita_validate_ini("${_mugen_vita_cfg_text}")
mugen_vita_ini_get("${_mugen_vita_cfg_text}" Video RenderMode _vita_renderer)
mugen_vita_ini_get("${_mugen_vita_cfg_text}" Config GameWidth _vita_game_w)
mugen_vita_ini_get("${_mugen_vita_cfg_text}" Config GameHeight _vita_game_h)
mugen_vita_ini_get("${_mugen_vita_cfg_text}" Video Width _vita_out_w)
mugen_vita_ini_get("${_mugen_vita_cfg_text}" Video Height _vita_out_h)
mugen_vita_ini_get("${_mugen_vita_cfg_text}" Video FullScreen _vita_fullscreen)
mugen_vita_ini_get("${_mugen_vita_cfg_text}" Config FirstRun _vita_firstrun)
mugen_vita_ini_get("${_mugen_vita_cfg_text}" Misc Precache _vita_precache)
if(_vita_renderer STREQUAL "")
  message(FATAL_ERROR
    "${MUGEN_VITA_CFG}: no [Video] RenderMode. The device needs one: "
    "system, opengl or openglscreen.")
endif()
# The value is matched case-insensitively, like every Ini lookup (the engine
# never sets g_iniCaseSensitive): RenderMode = OpenGL is what a M.U.G.E.N config
# traditionally says.
string(TOLOWER "${_vita_renderer}" _vita_renderer_lc)
if(NOT _vita_renderer_lc MATCHES "^(system|opengl|openglscreen)$")
  message(FATAL_ERROR
    "${MUGEN_VITA_CFG}: [Video] RenderMode='${_vita_renderer}' is not one of "
    "system, opengl, openglscreen")
endif()
set(_vita_renderer "${_vita_renderer_lc}")
foreach(_key game_w game_h out_w out_h)
  if("${_vita_${_key}}" STREQUAL "")
    message(FATAL_ERROR
      "${MUGEN_VITA_CFG}: missing a size the port needs "
      "([Config] GameWidth/GameHeight, [Video] Width/Height)")
  endif()
endforeach()
if(NOT _vita_fullscreen STREQUAL "1")
  message(WARNING
    "${MUGEN_VITA_CFG}: [Video] FullScreen='${_vita_fullscreen}'. The Vita has "
    "no window; the driver fills the panel either way, but the engine then "
    "takes its windowed paths (window position, SDL_ShowCursor).")
endif()
if(_vita_firstrun STREQUAL "1")
  message(WARNING
    "${MUGEN_VITA_CFG}: [Config] FirstRun=1 opens the licence panel and then "
    "rewrites data/mugen.cfg, which a packed VPK cannot do.")
endif()
if(NOT _vita_precache STREQUAL "0")
  message(WARNING
    "${MUGEN_VITA_CFG}: [Misc] Precache='${_vita_precache}'. The background "
    "loader's 4 KiB stack overflowed on hardware; 0 is the confirmed workaround.")
endif()
mugen_vita_validate_resolution("${_vita_renderer}" "${_vita_game_w}"
  "${_vita_game_h}" "${_vita_out_w}" "${_vita_out_h}")
message(STATUS "Vita: platform config ${MUGEN_VITA_CFG} (packed verbatim)")
message(STATUS "      ${_vita_renderer} ${_vita_out_w}x${_vita_out_h}, game "
               "${_vita_game_w}x${_vita_game_h}, FullScreen ${_vita_fullscreen}, "
               "Precache ${_vita_precache}")

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
# The part of that budget given to the newlib heap (malloc/operator new).
#
# VitaSDK's crt0 defaults the heap to 128 MiB when `_newlib_heap_size_user` is
# undefined -- larger than the whole budget above, so `sceKernelAllocMemBlock`
# fails, crt0 zeroes `_newlib_heap_size`, and every allocation from then on
# returns NULL. The first allocation happens in a C++ static constructor inside
# libvitaGL.a, before main, so the console shows C2-12828-1 with no mugen.log at
# all. port/psvita/src/vita_newlib.c defines the symbol; this sets its value.
#
# SDL's Vita video driver needs the *same* process memory for GXM, so an
# oversized heap starves `sceGxmInitialize` (SCE_GXM_ERROR_OUT_OF_MEMORY). 64 MiB
# leaves GXM and vitaGL plenty on a normal-app budget and is far more than
# M.U.G.E.N 1.1 needs on the Vita.
set(MUGEN_VITA_HEAP_MIB "64" CACHE STRING
    "newlib heap size in MiB (must leave room for GXM; 64 is comfortable)")

# ------------------------------------------------------------- vitaGL (opt) --
#
# Vendored vitaGL built with the flags the OpenGL renderer would need. Off by
# default: the native path uses standard 2D palettes, explicit MVP and vertex
# attributes, so it does not need the optional legacy compatibility flags.
# Kept as a source-build option for diagnostics and future vitaGL changes.
option(MUGEN_VITA_BUILD_VITAGL
       "Build the vendored vitaGL (needed only for the OpenGL renderer work)" OFF)

# Port optimization: state caching has a switch for controlled hardware A/B runs.
option(MUGEN_VITA_RENDER_CACHE "Cache Vita shader and texture draw state" ON)
# Upstream trusts cached shader files; keep disk caching an explicit experiment.
option(MUGEN_VITA_SHADER_CACHE "Experimental vitaGL disk shader cache" OFF)
if(MUGEN_VITA_SHADER_CACHE AND NOT MUGEN_VITA_BUILD_VITAGL)
  message(FATAL_ERROR "MUGEN_VITA_SHADER_CACHE requires MUGEN_VITA_BUILD_VITAGL=ON")
endif()

if(MUGEN_VITA_BUILD_VITAGL)
  enable_language(CXX)
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/VitaGl.cmake")
  mugen_add_vitagl(vitagl_vita)
  message(STATUS "vendored vitaGL enabled (UNPURE_TEXFORMATS, FFP shaders, UNPURE_TEXCOORDS)")
endif()

# ------------------------------------------------------------------ sources --

# The engine, one file per module of the original (see docs/ANALYSIS.md), plus
# the POSIX compat layer and the Sound_QueryMixerSpec shim. Globbed rather than
# listed so this file cannot go stale as modules are added -- the engine's
# module list is owned by the top-level CMakeLists.txt and the Makefile.
file(GLOB MUGEN_ENGINE_SOURCES CONFIGURE_DEPENDS "${MUGEN_SRC}/*.c")

# Not part of the reconstructed binary, and selected per platform.
list(REMOVE_ITEM MUGEN_ENGINE_SOURCES
  "${MUGEN_SRC}/platform_main_win32.c")          # WinMain: Windows-only, empty object here

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
  # Fix: one list element, not a string with a space in it. "-O0 -g3" reached
  # the compiler as a single quoted argument and cc1 rejected it
  # ("argument to '-O' should be a non-negative integer, 'g', 's', 'z' or
  # 'fast'"), so every debug build failed at the first object in both entry
  # points -- build.bat --debug reached it through both the removed MSYS2 script
  # and the current cmd.exe one. The
  # release value is a single flag, which is why only Debug was affected.
  set(MUGEN_VITA_OPT -O0 -g3)
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
  src/vita_video.c
  src/vita_input.c
  src/vita_shader.c
  src/vita_diagnostics.c
  src/vita_caps.c
  src/vita_fps.c
  src/vita_newlib.c
  src/vita_power.c
  ${MUGEN_LUA_SOURCES}
  ${MUGEN_MINIZIP_SOURCES}
  ${MUGEN_LIBRESAMPLE_SOURCES})

target_compile_definitions(mugen PRIVATE
  MUGEN_VITA_RENDER_CACHE=$<BOOL:${MUGEN_VITA_RENDER_CACHE}>)

option(MUGEN_MODEL3D "Opt-in basic VitaGL glTF profile (no skinning, lighting or optional effects)" OFF)
if(MUGEN_MODEL3D)
  include("${MUGEN_ROOT}/port/cmake/Model3D.cmake")
  mugen_enable_model3d(mugen "${MUGEN_ROOT}")
  message(STATUS "Vita model3d BASIC profile enabled; device qualification required")
endif()

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
# Port addition: enable the R4 smoke/pixel test and real frame-60 capture only
# for a diagnostic build. Native load/presentation logging is always retained.
option(MUGEN_VITA_GL_SELFTEST "Run Vita GL smoke test and capture game frame 60" OFF)
if(MUGEN_VITA_GL_SELFTEST)
  target_compile_definitions(mugen PRIVATE MUGEN_VITA_GL_SELFTEST=1)
  if(TARGET vitagl_vita)
    target_compile_definitions(vitagl_vita PRIVATE HAVE_SHARK_LOG=1)
  endif()
endif()

target_compile_definitions(mugen PRIVATE
  LIBRESAMPLE_STATIC
  SDL_MAIN_HANDLED
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
# SDK recipes sometimes hardcode includedir/libdir as well as prefix.
# Relocate copies, preserving the installed SDK.
file(GLOB _mugen_sdk_pcs "${MUGEN_VITASDK}/arm-vita-eabi/lib/pkgconfig/*.pc")
foreach(_pc IN LISTS _mugen_sdk_pcs)
  file(READ "${_pc}" _pc_text)
  string(REGEX MATCH "(^|\n)prefix=([^\r\n]+)" _pc_prefix_line "${_pc_text}")
  if(_pc_prefix_line)
    set(_pc_prefix "${CMAKE_MATCH_2}")
    string(LENGTH "${_pc_prefix}" _pc_prefix_length)
    if(_pc_prefix_length GREATER 1)
      string(REPLACE "${_pc_prefix}" "${MUGEN_VITASDK}/arm-vita-eabi"
             _pc_text "${_pc_text}")
    endif()
  endif()
  get_filename_component(_pc_name "${_pc}" NAME)
  file(WRITE "${MUGEN_VITA_PC_SHIM}/${_pc_name}" "${_pc_text}")
endforeach()
file(WRITE "${MUGEN_VITA_PC_SHIM}/libpng.pc"
  "# Generated by port/psvita/CMakeLists.txt -- do not edit.\n"
  "# freetype2.pc (shipped by VitaSDK) Requires: libpng, the pre-1.6 module\n"
  "# name; VitaSDK provides libpng16 only.\n"
  "Name: libpng\n"
  "Description: libpng shell module for freetype2.pc\n"
  "Version: 1.6.0\n"
  "Requires: libpng16\n")

# Use a real host pkg-config: the SDK wrapper overrides PKG_CONFIG_LIBDIR
# on Linux and cannot be executed directly by Windows CMake.
if(CMAKE_HOST_WIN32)
  file(TO_CMAKE_PATH "${MUGEN_VITASDK}" MUGEN_VITASDK_CMAKE)
  set(ENV{VITASDK} "${MUGEN_VITASDK_CMAKE}")
endif()
set(_mugen_pc "${PKG_CONFIG_EXECUTABLE}")
if(NOT _mugen_pc OR _mugen_pc MATCHES "arm-vita-eabi-pkg-config$")
  find_program(_mugen_host_pc NAMES pkg-config pkgconf NO_CMAKE_FIND_ROOT_PATH)
  if(NOT _mugen_host_pc)
    message(FATAL_ERROR "Host pkg-config not found; install pkg-config/pkgconf")
  endif()
  set(_mugen_pc "${_mugen_host_pc}")
endif()
set(PKG_CONFIG_EXECUTABLE "${_mugen_pc}" CACHE STRING
    "pkg-config command used for the Vita SDK" FORCE)

# The search path is set for the configure step only. Entries are
# ';'-separated because a ':' is ambiguous with the MSYS drive letter.
set(MUGEN_VITA_PC_DIRS
  "${MUGEN_VITA_PC_SHIM}"
  "${MUGEN_VITASDK}/arm-vita-eabi/lib/pkgconfig")
foreach(_extra IN LISTS MUGEN_VITA_PKGCONFIG_DIRS)
  if(_extra)
    list(APPEND MUGEN_VITA_PC_DIRS "${_extra}")
  endif()
endforeach()
list(REMOVE_DUPLICATES MUGEN_VITA_PC_DIRS)
if(CMAKE_HOST_WIN32)
  set(ENV{PKG_CONFIG_LIBDIR} "${MUGEN_VITA_PC_DIRS}")
else()
  list(JOIN MUGEN_VITA_PC_DIRS ":" _mugen_pc_path)
  set(ENV{PKG_CONFIG_LIBDIR} "${_mugen_pc_path}")
endif()
set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "")

set(PKG_CONFIG_ARGN "--define-variable=prefix=${MUGEN_VITASDK}/arm-vita-eabi"
                    "--define-variable=VITASDK=${MUGEN_VITASDK}")
find_package(PkgConfig REQUIRED)
message(STATUS "pkg-config   ${PKG_CONFIG_EXECUTABLE}")
message(STATUS "  .pc dirs   ${MUGEN_VITA_PC_DIRS}")

pkg_check_modules(MUGEN_VITA_PKGS REQUIRED
  sdl2
  SDL2_mixer
  SDL2_ttf
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
#   libSceShaccCgExt.a extensions for the device's ShaccCg shader compiler
#   libmathneon.a      NEON vector maths
# These are build-time dependencies. Runtime GLSL compilation also requires
# the decrypted libshacccg.suprx installed on the Vita; see README.md.
#
# Each name below is checked, so a VitaSDK update that renames one fails here
# with a readable message rather than as a wall of undefined Sce*/shark_* at
# link time.
set(MUGEN_VITA_VITAGL_DEPS "")
if(MUGEN_VITA_BUILD_VITAGL)
  # RENDERING_PLAN.md V1: the vendored vitaGL must be the one that ends up in
  # the executable, not simply a library that gets built and ignored. The
  # vendored target carries its own dependency chain (see cmake/VitaGl.cmake);
  # only the helper libraries that are not vendored are looked up in the SDK.
  message(STATUS "vitaGL: linking the VENDORED target (MUGEN_VITA_BUILD_VITAGL=ON)")
  list(APPEND MUGEN_VITA_VITAGL_DEPS vitagl_vita)
  foreach(_gl_dep
      vitashark   # vitaShaRK: shader compiler front end
      SceShaccCgExt  # the ShaccCg compiler
      mathneon)   # NEON maths helpers
    find_library(MUGEN_VITA_LIB_${_gl_dep} NAMES ${_gl_dep})
    if(NOT MUGEN_VITA_LIB_${_gl_dep})
      message(FATAL_ERROR
        "lib${_gl_dep}.a not found. The vendored vitaGL needs it; install it into "
        "${MUGEN_VITASDK}/arm-vita-eabi and reconfigure.")
    endif()
    list(APPEND MUGEN_VITA_VITAGL_DEPS "${MUGEN_VITA_LIB_${_gl_dep}}")
  endforeach()
  set(MUGEN_VITA_VITAGL_LIB vitagl_vita)
else()
  foreach(_gl_dep
      vitaGL      # the GL implementation itself
      vitashark   # vitaShaRK: shader compiler front end
      SceShaccCgExt  # the ShaccCg compiler
      mathneon)   # NEON maths helpers
    find_library(MUGEN_VITA_LIB_${_gl_dep} NAMES ${_gl_dep})
    if(NOT MUGEN_VITA_LIB_${_gl_dep})
      message(FATAL_ERROR
        "lib${_gl_dep}.a not found. The native display needs vitaGL "
        "and its dependencies; install them into "
        "${MUGEN_VITASDK}/arm-vita-eabi (vdpm install vitaGL) and reconfigure "
        "(or set -DMUGEN_VITA_BUILD_VITAGL=ON to build the vendored copy).")
    endif()
    list(APPEND MUGEN_VITA_VITAGL_DEPS "${MUGEN_VITA_LIB_${_gl_dep}}")
  endforeach()
  set(MUGEN_VITA_VITAGL_LIB "${MUGEN_VITA_LIB_vitaGL}")
endif()

target_include_directories(mugen PRIVATE
  ${MUGEN_VITA_PKGS_INCLUDE_DIRS}
  "${MUGEN_VITASDK}/arm-vita-eabi/include/SDL2")

target_link_directories(mugen PRIVATE ${MUGEN_VITA_PKGS_LIBRARY_DIRS})

# Static SDL2 codec dependencies come from Requires.private/Libs.private.
# mpg123 is also used directly by the engine BGM decoder.
pkg_check_modules(MUGEN_VITA_STATIC_PKGS REQUIRED
  sdl2 SDL2_mixer SDL2_ttf zlib libpng16 libmpg123)

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
  SceKernelModulemgr_stub
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
# SDL2_mixer no longer pulls in the colliding SDL1 mikmod Player_* symbols.
# Duplicate engine/codec definitions must be rejected by the linker.
target_link_options(mugen PRIVATE
  -Wl,--gc-sections
  -Wl,--wrap=sceDisplaySetFrameBuf
  -Wl,--wrap=sceKernelLoadStartModule
  # VitaSDK's linker reserves this gap for the SCE metadata appended by
  # vita-elf-create. Without it, small code-size changes can overlap LOAD
  # segments. Supported upstream workaround: vita-toolchain issue #303.
  -Wl,--defsym=__sce_headroom=0x10000)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
  set(_mugen_elf_check "${MUGEN_ROOT}/tools/psvita/check_elf.py")
  set_property(TARGET mugen APPEND PROPERTY LINK_DEPENDS "${_mugen_elf_check}")
  add_custom_command(TARGET mugen POST_BUILD
    COMMAND "${Python3_EXECUTABLE}" "${_mugen_elf_check}" "$<TARGET_FILE:mugen>"
    COMMENT "Checking SCE metadata headroom before vita-elf-create"
    VERBATIM)
endif()

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
  vita_create_self(mugen-psvita.self mugen
    UNSAFE
    MEMSIZE ${MUGEN_VITA_MEMSIZE})
else()
  # Normal app: no -m at all, so the system sizes the process. This is what the
  # VitaSDK samples do, and it is what GXM and vitaGL expect.
  vita_create_self(mugen-psvita.self mugen
    UNSAFE)
endif()

# The LiveArea assets under sce_sys/ are derived from res/icons/*.ico by
# cmake/Icon.cmake; the committed copies are what a tree without Python3 packs.
# Replace them with your own if you distribute a custom bubble.
set(MUGEN_VITA_SCE_SYS "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys")

# The bubble icon and the LiveArea artwork come from res/icons/*.ico, the same
# art the Windows build embeds, so every platform shows one picture. The
# conversions (the Vita wants a 128x128 icon, an 840x500 LiveArea background and
# a 280x158 start-up image, while res/icons tops out at 64x64) live in
# cmake/Icon.cmake, which tools/test_vita_icon.sh also runs on its own.
# The values are cached here for ccmake; the module carries the same defaults so
# that it can also be run on its own (tools/test_vita_icon.sh).
set(MUGEN_VITA_ICON_ICO "${MUGEN_ROOT}/res/icons/1.ico" CACHE FILEPATH
    "Icon to derive sce_sys/icon0.png from")
set(MUGEN_VITA_ICON_SIZE "128" CACHE STRING
    "icon0.png edge length; the Vita expects 128")
set(MUGEN_VITA_BG_W "840" CACHE STRING "LiveArea background width")
set(MUGEN_VITA_BG_H "500" CACHE STRING "LiveArea background height")
set(MUGEN_VITA_STARTUP_W "280" CACHE STRING "LiveArea start-up image width")
set(MUGEN_VITA_STARTUP_H "158" CACHE STRING "LiveArea start-up image height")
set(MUGEN_VITA_ICON_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/sce_sys" CACHE PATH
    "where the generated sce_sys/ artwork is written")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Icon.cmake")

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

  # --- the platform config -------------------------------------------------
  #
  # data/mugen.cfg is packed exactly as the tree ships it. The Vita's own
  # settings are port/psvita/mugen.psvita.cfg, packed beside it as
  # data/mugen.psvita.cfg and merged over it per key by the engine
  # (src/configmerge.c, called from Config_Load and Options_Load). Nothing is
  # rewritten here, which is what keeps the game's file the user's: the options
  # screen saves into it, and a value baked in at build time would look like a
  # setting that refuses to change.
  #
  # vita-pack-vpk refuses a destination already in the archive, so a stale copy
  # of mugen.psvita.cfg in the game tree is skipped where data/ is enumerated.

  # --- replace the four renderer shaders with the vitaGL-compatible versions --
  #
  # data/{indexed,rgba}.{vert,frag} ship with M.U.G.E.N 1.1b1 and are written in
  # GLSL 1.20 built-ins (gl_Vertex, gl_MultiTexCoord0, gl_TexCoord[0]) that
  # vitaGL's translator does not handle, plus sampler1D/texture1D which its GLSL
  # shim does not provide. res/shaders/vita/ holds rewritten equivalents whose
  # PalFX maths is unchanged. They are substituted here rather than edited in the
  # tree, so install/ stays pristine and the Windows/Linux builds keep using the
  # originals.
  #
  # NOTE: these only matter once the renderer draws through shaders. As things
  # stand the software renderer is the shipping path: vitaGL's immediate mode
  # (glBegin/glEnd) always renders with its own fixed-function shaders and never
  # consults a bound program, so src/glrender.c's glUseProgram has no effect
  # there. See README.md, "The OpenGL renderer".
  set(MUGEN_VITA_SHADERS "${CMAKE_CURRENT_SOURCE_DIR}/../../res/shaders/vita")
  foreach(_shader indexed.vert indexed.frag rgba.vert rgba.frag)
    if(EXISTS "${MUGEN_VITA_SHADERS}/${_shader}")
      set(MUGEN_VITA_SHADER_${_shader} "${MUGEN_VITA_SHADERS}/${_shader}")
    else()
      message(WARNING "${MUGEN_VITA_SHADERS}/${_shader} missing; the VPK keeps the original")
      set(MUGEN_VITA_SHADER_${_shader} "")
    endif()
  endforeach()

  message(STATUS "mugen-psvita.vpk packs ${MUGEN_VITA_DATA_DIR} as /data")
else()
  message(STATUS
    "mugen-psvita.vpk packs no game data (set MUGEN_VITA_DATA_DIR to include a "
    "M.U.G.E.N 1.1b1 install; see port/psvita/README.md)")
endif()

# The VPK is packed in one pass, by hand rather than through vita_create_vpk,
# for one reason: vita_create_vpk's FILE argument takes a src/dest pair per
# file, so it can add the LiveArea assets but not a whole directory. The game
# data is a directory (potentially thousands of files), and vita-pack-vpk's
# --add accepts "src=dst" for directories directly. Building the SFO and the
# archive explicitly keeps that in one command.
#
# Outputs mirror what vita_create_vpk would produce: mugen-psvita.vpk next to
# mugen-psvita.self, in the build directory.
set(MUGEN_VITA_VPK          "${CMAKE_CURRENT_BINARY_DIR}/mugen-psvita.vpk")
set(MUGEN_VITA_SFO          "${CMAKE_CURRENT_BINARY_DIR}/param.sfo")
set(MUGEN_VITA_EBOOT        "${CMAKE_CURRENT_BINARY_DIR}/mugen-psvita.self")

add_custom_command(
  OUTPUT ${MUGEN_VITA_SFO}
  COMMAND ${VITA_MKSFOEX}
          -s APP_VER=${MUGEN_VITA_VERSION}
          -s TITLE_ID=${MUGEN_VITA_TITLEID}
          "${VITA_APP_NAME}" ${MUGEN_VITA_SFO}
  COMMENT "Generating param.sfo for mugen-psvita.vpk"
  VERBATIM)

# Port addition: privately bundle a compiler supplied by the person building.
# This is a path input, not a downloaded or repository-owned runtime binary.
set(MUGEN_VITA_SHADER_COMPILER "" CACHE FILEPATH
    "Optional libshacccg.suprx to pack as module/libshacccg.suprx")
if(MUGEN_VITA_SHADER_COMPILER)
  if(NOT EXISTS "${MUGEN_VITA_SHADER_COMPILER}" OR IS_DIRECTORY "${MUGEN_VITA_SHADER_COMPILER}")
    message(FATAL_ERROR "MUGEN_VITA_SHADER_COMPILER must name an existing libshacccg.suprx file")
  endif()
  file(READ "${MUGEN_VITA_SHADER_COMPILER}" _compiler_magic LIMIT 4 HEX)
  if(NOT _compiler_magic STREQUAL "53434500")
    message(FATAL_ERROR "MUGEN_VITA_SHADER_COMPILER is not an SCE container; supply the .suprx, not a raw .elf")
  endif()
  # The header check does not establish that the native loader will accept it.
  message(STATUS "Bundling supplied shader compiler as module/libshacccg.suprx")
endif()

# External compiler use is supported, but must be visible in the build log.
# The renderer comes from the platform config validated above, so the warning
# fires for exactly the VPKs that need the module.
string(TOLOWER "${_vita_renderer}" _compiler_renderer)
if(NOT MUGEN_VITA_SHADER_COMPILER AND
   NOT EXISTS "${MUGEN_VITA_DATA_DIR}/module/libshacccg.suprx" AND
   (_compiler_renderer STREQUAL "opengl" OR _compiler_renderer STREQUAL "openglscreen"))
  message(WARNING "OpenGL VPK has no bundled shader compiler. Supply --shader-compiler FILE, or install libshacccg.suprx in ur0:/data/ on the Vita before launching.")
endif()

set(_mugen_vpk_args
  -s ${MUGEN_VITA_SFO}
  -b ${MUGEN_VITA_EBOOT}
  -a ${MUGEN_VITA_ICON_PNG}=sce_sys/icon0.png
  -a ${MUGEN_VITA_BG_PNG}=sce_sys/livearea/contents/bg.png
  -a ${MUGEN_VITA_STARTUP_PNG}=sce_sys/livearea/contents/startup.png
  -a ${MUGEN_VITA_SCE_SYS}/livearea/contents/template.xml=sce_sys/livearea/contents/template.xml)

if(MUGEN_VITA_SHADER_COMPILER)
  list(APPEND _mugen_vpk_args -a "${MUGEN_VITA_SHADER_COMPILER}=module/libshacccg.suprx")
endif()

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
  # so a stale mugen-msvc-win32-sdl1.exe (a 1.7 MB Windows PE) would otherwise be
  # packed into the VPK. The tree itself is left alone.
  set(_mugen_data_exclude
    # current output names (see the naming scheme in the root CMakeLists.txt
    # and the Makefile) plus legacy names from before the rename, so a stale
    # binary left behind in install/ never gets packed as game data
    mugen-msvc-win32-sdl1.exe mugen-msvc-win32-sdl1.pdb
    mugen-mingw-win32-sdl2.exe mugen-mingw-win32-sdl1.exe
    mugen-mingw-win64-sdl2.exe
    mugen-mingw-win32-sdl2-stripped.exe mugen-mingw-win32-sdl1-stripped.exe
    mugen-linux-x64 mugen-linux-arm64 mugen-linux-x86
    # legacy (pre-rename)
    mugen_unleashed.exe mugen_unleashed.pdb
    mugen-msvc-win32-sdl2.exe mugen-msvc-win32-sdl2.pdb
    mugen-static.exe mugen-static-sdl1.exe
    mugen-static-stripped.exe mugen-static-sdl1-stripped.exe
    mugen-win64.exe mugen-win64-sdl1.exe
    mugen.log mugen-match.log)

  file(GLOB _mugen_data_entries RELATIVE "${MUGEN_VITA_DATA_DIR}"
       "${MUGEN_VITA_DATA_DIR}/*")
  foreach(_entry IN LISTS _mugen_data_entries)
    if(_entry STREQUAL "module" AND MUGEN_VITA_SHADER_COMPILER)
      # Preserve other supplied modules, replacing only this explicit compiler.
      file(GLOB _mugen_module_files RELATIVE "${MUGEN_VITA_DATA_DIR}/module"
           "${MUGEN_VITA_DATA_DIR}/module/*")
      foreach(_module IN LISTS _mugen_module_files)
        if(NOT _module STREQUAL "libshacccg.suprx")
          list(APPEND _mugen_vpk_args
               -a "${MUGEN_VITA_DATA_DIR}/module/${_module}=module/${_module}")
        endif()
      endforeach()
      continue()
    endif()
    if(_entry IN_LIST _mugen_data_exclude)
      message(STATUS "  not packing ${_entry} (this project's own build output)")
      continue()
    endif()
    if(_entry STREQUAL "data")
      file(GLOB _mugen_data_files RELATIVE "${MUGEN_VITA_DATA_DIR}/data"
           "${MUGEN_VITA_DATA_DIR}/data/*")
      foreach(_sub IN LISTS _mugen_data_files)
        if(_sub STREQUAL "mugen.psvita.cfg")
          # The port's file is packed below; a copy left in the game tree would
          # be a duplicate destination, and vita-pack-vpk rejects those.
          continue()
        elseif(MUGEN_VITA_SHADER_${_sub})
          list(APPEND _mugen_vpk_args -a "${MUGEN_VITA_SHADER_${_sub}}=data/${_sub}")
        else()
          list(APPEND _mugen_vpk_args
               -a "${MUGEN_VITA_DATA_DIR}/data/${_sub}=data/${_sub}")
        endif()
      endforeach()
      # Verbatim: no value in the file is rewritten by the build.
      list(APPEND _mugen_vpk_args -a "${MUGEN_VITA_CFG}=data/mugen.psvita.cfg")
    else()
      list(APPEND _mugen_vpk_args -a "${MUGEN_VITA_DATA_DIR}/${_entry}=${_entry}")
    endif()
  endforeach()
endif()

add_custom_command(
  OUTPUT ${MUGEN_VITA_VPK}
  COMMAND ${VITA_PACK_VPK} ${_mugen_vpk_args} ${MUGEN_VITA_VPK}
  DEPENDS ${MUGEN_VITA_SFO} ${MUGEN_VITA_EBOOT} mugen-psvita.self
          ${MUGEN_VITA_ICON_PNG} ${MUGEN_VITA_BG_PNG} ${MUGEN_VITA_STARTUP_PNG}
          ${MUGEN_VITA_CFG} ${MUGEN_VITA_SHADER_COMPILER}
          "${MUGEN_VITA_SCE_SYS}/livearea/contents/template.xml"
          ${MUGEN_VITA_SHADER_indexed.vert} ${MUGEN_VITA_SHADER_indexed.frag}
          ${MUGEN_VITA_SHADER_rgba.vert} ${MUGEN_VITA_SHADER_rgba.frag}
  COMMENT "Building mugen-psvita.vpk"
  VERBATIM)

add_custom_target(mugen-vpk ALL DEPENDS ${MUGEN_VITA_VPK})

# ------------------------------------------------------------------ summary --

message(STATUS "")
message(STATUS "mugen-unleashed -- PS Vita")
message(STATUS "  VitaSDK      ${MUGEN_VITASDK}")
message(STATUS "  Build type   ${CMAKE_BUILD_TYPE}")
message(STATUS "  SDL2         ${MUGEN_VITA_PKGS_sdl2_VERSION} (VitaSDK arm-vita-eabi)")
message(STATUS "  vitaGL       ${MUGEN_VITA_VITAGL_LIB}")
message(STATUS "  Title        ${VITA_APP_NAME} (${MUGEN_VITA_TITLEID} v${MUGEN_VITA_VERSION})")
message(STATUS "  Memory       budget ${MUGEN_VITA_MEMSIZE} KiB, newlib heap ${MUGEN_VITA_HEAP_MIB} MiB")
message(STATUS "  Output       ${CMAKE_CURRENT_BINARY_DIR}/mugen-psvita.self")
message(STATUS "               ${CMAKE_CURRENT_BINARY_DIR}/mugen-psvita.vpk")
message(STATUS "")
message(STATUS "  Renderer: ${_vita_renderer} from ${MUGEN_VITA_CFG}")
message(STATUS "            packed as data/mugen.psvita.cfg and merged over the game's")
message(STATUS "            data/mugen.cfg at start-up (per key). Edit it to change them.")
message(STATUS "  Floating point: no x87 on ARMv7, so mugen.log differs from win32")
message(STATUS "  on the numeric lines by design. See port/psvita/README.md.")
message(STATUS "")
