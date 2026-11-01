# =============================================================================
#  sdl-input-mapper -- PS Vita SDL 1.2 input inspection tool
# =============================================================================
#
#  A small standalone app that shows what SDL's Vita joystick driver actually
#  reports: button indices, hat values and axis ranges. It exists so that
#  mugen-unleashed's gamepad mapping can be written from observation instead of
#  guessed button numbers.
#
#  Kept as its own CMake project rather than a second target in
#  port/psvita/CMakeLists.txt so that building the tool never depends on the
#  game's sources, its data tree, or its pkg-config setup. It uses only SDL from
#  VitaSDK, linked by explicit name.
#
#  Build:
#      cmake -S port/psvita/input-mapper -B build/psvita/input-mapper -G Ninja \
#            -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake \
#            -DCMAKE_BUILD_TYPE=Release
#      cmake --build build/psvita/input-mapper
#
#  Output: build/psvita/input-mapper/sdl-input-mapper.vpk
#
# =============================================================================

cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR
      "VITASDK is not set. Run set VITASDK=C:\\vitasdk (or the setup script for "
      "this shell) and reconfigure; see port/psvita/README.md.")
  endif()
endif()

project(sdl_input_mapper C)

if(NOT DEFINED ENV{VITASDK})
  message(FATAL_ERROR "The VitaSDK toolchain file was used but VITASDK is not in the environment")
endif()
set(MUGEN_VITASDK "$ENV{VITASDK}")

if(NOT EXISTS "${MUGEN_VITASDK}/arm-vita-eabi/include/SDL/SDL.h")
  message(FATAL_ERROR
    "${MUGEN_VITASDK}/arm-vita-eabi/include/SDL/SDL.h not found: install "
    "VitaSDK's SDL 1.2 (vdpm install sdl) and reconfigure.")
endif()

include("${MUGEN_VITASDK}/share/vita.cmake" REQUIRED)

# The sources live in port/psvita/src, next to the game port's own, so all the
# Vita-specific code for this project is in one place.
get_filename_component(MUGEN_PORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(MUGEN_PORT_SRC "${MUGEN_PORT_DIR}/src")
if(NOT EXISTS "${MUGEN_PORT_SRC}/sdl_input_mapper.c")
  message(FATAL_ERROR "${MUGEN_PORT_SRC}/sdl_input_mapper.c not found")
endif()

# ------------------------------------------------------------- packaging -----

set(VITA_APP_NAME "SDL Input Mapper")
set(MUGEN_VITA_TITLEID "MSDK00002" CACHE STRING "9-character Vita title ID")
set(MUGEN_VITA_VERSION "01.00"     CACHE STRING "APP_VER, format ##.##")

# --------------------------------------------------------------- build ------

add_executable(sdl-input-mapper
  "${MUGEN_PORT_SRC}/sdl_input_mapper.c"
  "${MUGEN_PORT_SRC}/vita_newlib_inputmapper.c")

target_include_directories(sdl-input-mapper PRIVATE
  "${MUGEN_VITASDK}/arm-vita-eabi/include"
  "${MUGEN_VITASDK}/arm-vita-eabi/include/SDL"
  "${MUGEN_PORT_SRC}")            # input_mapper_font.h

target_compile_options(sdl-input-mapper PRIVATE
  -std=gnu11
  -O2
  -Wall -Wno-unused-parameter
  -ffunction-sections -fdata-sections)

# SDL 1.2 plus the SCE userland stubs its Vita backend calls. Listed explicitly
# rather than through pkg-config: that keeps this tool buildable even when the
# host pkg-config setup the game port needs is not present (see the "How
# pkg-config is handled" section of port/psvita/README.md). The list is the one
# in VitaSDK's sdl.pc.
target_link_directories(sdl-input-mapper PRIVATE "${MUGEN_VITASDK}/arm-vita-eabi/lib")
target_link_libraries(sdl-input-mapper PRIVATE
  SDL
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
  m
  c)

target_link_options(sdl-input-mapper PRIVATE
  -Wl,--gc-sections
  -Wl,--allow-multiple-definition)

set_target_properties(sdl-input-mapper PROPERTIES OUTPUT_NAME "sdl-input-mapper")

# No MEMSIZE: that is vita-make-fself's -m, and "Normal app: 0" means omitting it
# so the system sizes the process. Passing a number there selects the
# system-mode bracket (4-74 MiB) and starves SceGxm -- the mistake that cost the
# game port several debug rounds.
vita_create_self(sdl-input-mapper.self sdl-input-mapper UNSAFE)

set(MUGEN_VITA_SCE_SYS "${MUGEN_PORT_DIR}/sce_sys")
set(MUGEN_VITA_VPK     "${CMAKE_CURRENT_BINARY_DIR}/sdl-input-mapper.vpk")
set(MUGEN_VITA_SFO     "${CMAKE_CURRENT_BINARY_DIR}/param.sfo")

add_custom_command(
  OUTPUT ${MUGEN_VITA_SFO}
  COMMAND ${VITA_MKSFOEX}
          -s APP_VER=${MUGEN_VITA_VERSION}
          -s TITLE_ID=${MUGEN_VITA_TITLEID}
          "${VITA_APP_NAME}" ${MUGEN_VITA_SFO}
  COMMENT "Generating param.sfo for sdl-input-mapper.vpk"
  VERBATIM)

add_custom_command(
  OUTPUT ${MUGEN_VITA_VPK}
  COMMAND ${VITA_PACK_VPK}
          -s ${MUGEN_VITA_SFO}
          -b "${CMAKE_CURRENT_BINARY_DIR}/sdl-input-mapper.self"
          -a ${MUGEN_VITA_SCE_SYS}/icon0.png=sce_sys/icon0.png
          -a ${MUGEN_VITA_SCE_SYS}/livearea/contents/bg.png=sce_sys/livearea/contents/bg.png
          -a ${MUGEN_VITA_SCE_SYS}/livearea/contents/startup.png=sce_sys/livearea/contents/startup.png
          -a ${MUGEN_VITA_SCE_SYS}/livearea/contents/template.xml=sce_sys/livearea/contents/template.xml
          ${MUGEN_VITA_VPK}
  DEPENDS ${MUGEN_VITA_SFO} sdl-input-mapper.self
  COMMENT "Building sdl-input-mapper.vpk"
  VERBATIM)

add_custom_target(sdl-input-mapper-vpk ALL DEPENDS ${MUGEN_VITA_VPK})

# ---------------------------------------------------------------- summary ----

message(STATUS "")
message(STATUS "sdl-input-mapper -- PS Vita")
message(STATUS "  VitaSDK  ${MUGEN_VITASDK}")
message(STATUS "  Title    ${VITA_APP_NAME} (${MUGEN_VITA_TITLEID} v${MUGEN_VITA_VERSION})")
message(STATUS "  Output   ${MUGEN_VITA_VPK}")
message(STATUS "")
message(STATUS "  Install with VitaShell, launch, then press buttons / move the")
message(STATUS "  stick and d-pad. Events also go to ux0:data/sdl-input-mapper.log")
message(STATUS "")
