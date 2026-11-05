# Derives the PS Vita bubble icon (sce_sys/icon0.png) from res/icons/*.ico.
#
# The Vita wants a 128x128 PNG, while res/icons holds the Windows artwork at
# 64x64 and below, so the conversion is a step of its own. It runs at configure
# time and needs only the Python standard library (tools/iconpack.py).
#
# Inputs (set by CMakeLists.txt or on the command line):
#   MUGEN_ROOT              repository root
#   MUGEN_VITA_SCE_SYS      source sce_sys/ directory, holding the sample icon
#   MUGEN_VITA_ICON_ICO     the .ico to convert
#   MUGEN_VITA_ICON_SIZE    edge length for the PNG (the Vita expects 128)
#   MUGEN_VITA_ICON_OUT_DIR where the PNG is written. Explicit rather than
#                           CMAKE_CURRENT_BINARY_DIR, because `cmake -P` (the
#                           gate below) has no build directory and would put the
#                           file in the caller's working directory -- a stray
#                           sce_sys/ in the source tree.
#
# Output:
#   MUGEN_VITA_ICON_PNG     the PNG to pack as sce_sys/icon0.png. It is the
#                           generated file when the conversion worked, and the
#                           committed sample asset otherwise, so a tree without
#                           Python3 still builds.
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
if(NOT DEFINED MUGEN_VITA_ICON_OUT_DIR)
  set(MUGEN_VITA_ICON_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/sce_sys")
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)

set(MUGEN_VITA_ICON_PNG "${MUGEN_VITA_SCE_SYS}/icon0.png")

if(Python3_FOUND AND EXISTS "${MUGEN_VITA_ICON_ICO}")
  set(_mugen_vita_icon_dir "${MUGEN_VITA_ICON_OUT_DIR}")
  file(MAKE_DIRECTORY "${_mugen_vita_icon_dir}")
  execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${MUGEN_ROOT}/tools/iconpack.py" png
            "${_mugen_vita_icon_dir}/icon0.png" "${MUGEN_VITA_ICON_ICO}"
            "${MUGEN_VITA_ICON_SIZE}"
    RESULT_VARIABLE _mugen_icon_result
    OUTPUT_VARIABLE _mugen_icon_output
    ERROR_VARIABLE _mugen_icon_output)
  if(NOT _mugen_icon_result EQUAL 0 OR NOT EXISTS "${_mugen_vita_icon_dir}/icon0.png")
    message(WARNING
      "tools/iconpack.py could not convert ${MUGEN_VITA_ICON_ICO} "
      "(${_mugen_icon_output}); packing the sample icon instead")
  else()
    set(MUGEN_VITA_ICON_PNG "${_mugen_vita_icon_dir}/icon0.png")
    message(STATUS "sce_sys/icon0.png comes from ${MUGEN_VITA_ICON_ICO} "
                   "(${MUGEN_VITA_ICON_SIZE}x${MUGEN_VITA_ICON_SIZE})")
  endif()
else()
  message(STATUS
    "sce_sys/icon0.png is the sample asset (no Python3, or no ${MUGEN_VITA_ICON_ICO})")
endif()
