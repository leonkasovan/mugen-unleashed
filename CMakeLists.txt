cmake_minimum_required(VERSION 3.20)
project(mugen_unleashed VERSION 1.1.0 LANGUAGES C RC)

# mugen.exe is a 32-bit MSVC 2008 binary; everything here targets Win32/x86.
if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Build with MSVC for x86 (32-bit), e.g. tools\\msvc_env.bat cmake --preset x86-release")
endif()

set(MUGEN_GAME_DIR "" CACHE PATH
    "M.U.G.E.N 1.1 Beta 1 directory (optional: its mugen.exe provides the window icon)")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(MugenSdk)
include(ThirdParty)

# The engine, one file per module of the original (see docs/ANALYSIS.md).
set(MUGEN_SOURCES
  src/afterimage.c
  src/ai.c
  src/anim.c
  src/bg.c
  src/bgm.c
  src/bgmplugins.c
  src/bgmstream.c
  src/bgmthread.c
  src/camera.c
  src/char.c
  src/clsn.c
  src/cmdinput.c
  src/cmdline.c
  src/cnsparse.c
  src/config.c
  src/console.c
  src/continue.c
  src/dbgcmd.c
  src/error.c
  src/events.c
  src/explod.c
  src/expr.c
  src/file.c
  src/font.c
  src/frame.c
  src/gameflow.c
  src/gameloop.c
  src/gfx.c
  src/gldetect.c
  src/globals.c
  src/glrender.c
  src/hashtable.c
  src/helper.c
  src/ini.c
  src/keybind.c
  src/keyboard.c
  src/keyedarray.c
  src/lang.c
  src/log.c
  src/lookup3.c
  src/luaapi.c
  src/luabind.c
  src/luautil.c
  src/main.c
  src/match.c
  src/matchload.c
  src/misc.c
  src/motif.c
  src/pads.c
  src/palette.c
  src/playerinput.c
  src/playerrel.c
  src/playerstate.c
  src/pngio.c
  src/prim.c
  src/result.c
  src/round.c
  src/sctrl.c
  src/sdlmain.c
  src/select.c
  src/sff.c
  src/softblend.c
  src/softrender.c
  src/sound.c
  src/sprite.c
  src/spritecodec.c
  src/statedef.c
  src/storyboard.c
  src/stream.c
  src/system.c
  src/textring.c
  src/timer.c
  src/title.c
  src/trigger.c
  src/util.c
  src/versus.c
  src/video.c
  src/wav.c
)

add_executable(mugen WIN32 ${MUGEN_SOURCES} src/mugen.rc src/mugen.manifest)
# A single PDB for the target. Without this CMake sets TARGET_COMPILE_PDB to the
# target's object directory, so every cl.exe writes vc140.pdb into the same
# directory and parallel builds fail with C1041 ("cannot open program database
# ... if multiple CL.EXE write to the same .PDB file, please use /FS"): /FS is
# already passed, but an unnamed PDB cannot be shared that way.
set_target_properties(mugen PROPERTIES
  OUTPUT_NAME mugen_unleashed
  COMPILE_PDB_NAME mugen_unleashed
  COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(mugen PRIVATE src)
target_compile_definitions(mugen PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
# /wd4100 unreferenced parameter: the reconstructed functions keep every original parameter.
# /arch:IA32 keeps floating point on the x87 FPU like the original (VS2008, no SSE).
target_compile_options(mugen PRIVATE "$<$<COMPILE_LANGUAGE:C>:/W3;/wd4100;/utf-8;/arch:IA32>")
target_link_libraries(mugen PRIVATE mugen_sdk lua51 png12 glad minizip
  opengl32 glu32 winmm shell32 user32 gdi32)

# The window icon of the original, when its mugen.exe and Python 3 with pefile are available.
find_package(Python3 COMPONENTS Interpreter)
if(MUGEN_GAME_DIR AND EXISTS "${MUGEN_GAME_DIR}/mugen.exe" AND Python3_FOUND)
  set(_mugen_ico "${CMAKE_CURRENT_BINARY_DIR}/icon/mugen.ico")
  add_custom_command(
    OUTPUT "${_mugen_ico}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/extract_icon.py"
            "${MUGEN_GAME_DIR}/mugen.exe" "${_mugen_ico}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/extract_icon.py"
    COMMENT "Extracting the icon of the original mugen.exe"
    VERBATIM)
  set_source_files_properties(src/mugen.rc PROPERTIES OBJECT_DEPENDS "${_mugen_ico}")
  target_include_directories(mugen PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/icon")
  target_compile_definitions(mugen PRIVATE MUGEN_HAS_ICON)
  message(STATUS "mugen.exe gets the icon of ${MUGEN_GAME_DIR}/mugen.exe")
else()
  message(STATUS "mugen.exe gets no icon (set MUGEN_GAME_DIR to the original game to embed it)")
endif()
