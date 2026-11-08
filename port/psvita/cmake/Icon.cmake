# Derives the PS Vita's shell artwork (sce_sys/) from res/icons/*.ico.
#
# The Vita wants more than a square icon: the bubble carries a 128x128 icon, an
# 840x500 LiveArea background and a 280x158 start-up image. res/icons holds the
# Windows artwork at 64x64 and below, so all three are derived here rather than
# kept as hand-made files -- the ports ship one picture, and updating it means
# replacing one .ico.
#
# It runs at configure time and needs only the Python standard library
# (tools/iconpack.py, which is also the single .ico decoder in this project).
#
# Inputs (set by CMakeLists.txt or on the command line):
#   MUGEN_ROOT              repository root
#   MUGEN_VITA_SCE_SYS      source sce_sys/ directory, holding the sample assets
#   MUGEN_VITA_ICON_ICO     the .ico to derive everything from
#   MUGEN_VITA_ICON_SIZE    icon0.png edge length (the Vita expects 128)
#   MUGEN_VITA_BG_W/H       LiveArea background size (840x500)
#   MUGEN_VITA_STARTUP_W/H  start-up image size (280x158)
#   MUGEN_VITA_ICON_OUT_DIR where the PNGs are written. Explicit rather than
#                           CMAKE_CURRENT_BINARY_DIR, because `cmake -P` (the
#                           gate below) has no build directory and would put the
#                           files in the caller's working directory -- a stray
#                           sce_sys/ in the source tree.
#
# Outputs:
#   MUGEN_VITA_ICON_PNG     the PNG to pack as sce_sys/icon0.png
#   MUGEN_VITA_BG_PNG                       sce_sys/livearea/contents/bg.png
#   MUGEN_VITA_STARTUP_PNG                  sce_sys/livearea/contents/startup.png
#
# Each is the generated file when its conversion worked, and the committed
# sample asset otherwise, so a tree without Python3 still builds and packs.
#
# Kept in its own module rather than inline in CMakeLists.txt so that
# tools/test_vita_icon.sh can run it with `cmake -P`, without a VitaSDK.
#
# Note: this file is executed in script mode by that test, so it must not use
# project-only commands (add_custom_command and friends).

# Defaults live here, not only in CMakeLists.txt, so that `cmake -P` (the gate)
# runs the same values the build does. A default that only exists in
# CMakeLists.txt is untested by construction.
if(NOT DEFINED MUGEN_VITA_ICON_ICO)
  set(MUGEN_VITA_ICON_ICO "${MUGEN_ROOT}/res/icons/1.ico")
endif()
if(NOT DEFINED MUGEN_VITA_ICON_SIZE)
  set(MUGEN_VITA_ICON_SIZE "128")
endif()
if(NOT DEFINED MUGEN_VITA_BG_W)
  set(MUGEN_VITA_BG_W "840")
endif()
if(NOT DEFINED MUGEN_VITA_BG_H)
  set(MUGEN_VITA_BG_H "500")
endif()
if(NOT DEFINED MUGEN_VITA_STARTUP_W)
  set(MUGEN_VITA_STARTUP_W "280")
endif()
if(NOT DEFINED MUGEN_VITA_STARTUP_H)
  set(MUGEN_VITA_STARTUP_H "158")
endif()
if(NOT DEFINED MUGEN_VITA_ICON_OUT_DIR)
  set(MUGEN_VITA_ICON_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/sce_sys")
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)

# What ships until a conversion proves otherwise: the committed sample assets.
set(MUGEN_VITA_ICON_PNG "${MUGEN_VITA_SCE_SYS}/icon0.png")
set(MUGEN_VITA_BG_PNG
    "${MUGEN_VITA_SCE_SYS}/livearea/contents/bg.png")
set(MUGEN_VITA_STARTUP_PNG
    "${MUGEN_VITA_SCE_SYS}/livearea/contents/startup.png")

# _mugen_vita_art(<result-var> <output> <label> <subcommand> [extra...])
#
# Runs one conversion and points <result-var> at the generated file, or leaves
# it at the sample asset (with a warning) when the conversion cannot run. The
# one thing this must never do is hand cmake a path that does not exist: the
# VPK packing command fails on a missing -a source, and "the icon block looks
# right" is not a check (tools/test_vita_icon.sh is).
#
# The arguments go in iconpack.py's own order: subcommand, output, input, then
# whatever that subcommand takes (a size, or width/height/scale/background).
function(_mugen_vita_art result_var output label subcommand)
  if(NOT Python3_FOUND OR NOT EXISTS "${MUGEN_VITA_ICON_ICO}")
    return()
  endif()
  file(MAKE_DIRECTORY "${MUGEN_VITA_ICON_OUT_DIR}/livearea/contents")
  execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${MUGEN_ROOT}/tools/iconpack.py"
            ${subcommand} "${output}" "${MUGEN_VITA_ICON_ICO}" ${ARGN}
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _out)
  if(NOT _rc EQUAL 0 OR NOT EXISTS "${output}")
    message(WARNING
      "tools/iconpack.py could not derive ${label} from ${MUGEN_VITA_ICON_ICO} "
      "(${_out}); the sample asset is packed instead")
    return()
  endif()
  message(STATUS "${label} comes from ${MUGEN_VITA_ICON_ICO}")
  set(${result_var} "${output}" PARENT_SCOPE)
endfunction()

_mugen_vita_art(MUGEN_VITA_ICON_PNG
  "${MUGEN_VITA_ICON_OUT_DIR}/icon0.png" "sce_sys/icon0.png"
  png "${MUGEN_VITA_ICON_SIZE}")

# The LiveArea background is the artwork on the average colour of its own opaque
# pixels (`auto`): a full-bleed colour field, derived from the icon rather than
# invented, so the bubble and its background belong together.
_mugen_vita_art(MUGEN_VITA_BG_PNG
  "${MUGEN_VITA_ICON_OUT_DIR}/livearea/contents/bg.png"
  "sce_sys/livearea/contents/bg.png"
  canvas "${MUGEN_VITA_BG_W}" "${MUGEN_VITA_BG_H}" 45 auto)

# The start-up image is drawn on the bubble itself, so it stays transparent and
# the artwork is not shrunk to nothing by the wide 280x158 frame.
_mugen_vita_art(MUGEN_VITA_STARTUP_PNG
  "${MUGEN_VITA_ICON_OUT_DIR}/livearea/contents/startup.png"
  "sce_sys/livearea/contents/startup.png"
  canvas "${MUGEN_VITA_STARTUP_W}" "${MUGEN_VITA_STARTUP_H}" 90 none)

if(NOT Python3_FOUND)
  message(STATUS
    "sce_sys/ artwork is the committed sample (no Python3 to derive it from ${MUGEN_VITA_ICON_ICO})")
elseif(NOT EXISTS "${MUGEN_VITA_ICON_ICO}")
  message(STATUS
    "sce_sys/ artwork is the committed sample (no ${MUGEN_VITA_ICON_ICO})")
endif()
