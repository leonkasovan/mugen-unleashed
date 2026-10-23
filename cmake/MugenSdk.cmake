# Headers and import libraries for the DLLs mugen.exe links against.
#
# Import libraries are generated from .def files extracted from the original DLLs
# in Elecbyte.MUGEN.libs, so the exports match exactly what the game ships with.

set(MUGEN_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdk")
set(MUGEN_IMPLIB_DIR "${CMAKE_CURRENT_BINARY_DIR}/implib")
file(MAKE_DIRECTORY "${MUGEN_IMPLIB_DIR}")

set(_mugen_implibs)
foreach(dll SDL SDL_mixer SDL_ttf SDL_gfx zlib1 libresample pthreadVC2)
  set(_def "${MUGEN_SDK_DIR}/def/${dll}.def")
  set(_lib "${MUGEN_IMPLIB_DIR}/${dll}.lib")
  add_custom_command(
    OUTPUT "${_lib}"
    COMMAND "${CMAKE_AR}" /nologo "/def:${_def}" /machine:x86 "/out:${_lib}"
    DEPENDS "${_def}"
    COMMENT "Generating import library ${dll}.lib"
    VERBATIM)
  list(APPEND _mugen_implibs "${_lib}")
endforeach()
add_custom_target(mugen_implibs DEPENDS ${_mugen_implibs})

add_library(mugen_sdk INTERFACE)
add_dependencies(mugen_sdk mugen_implibs)
target_include_directories(mugen_sdk INTERFACE
  "${MUGEN_SDK_DIR}/include"
  "${MUGEN_SDK_DIR}/include/SDL")
target_link_libraries(mugen_sdk INTERFACE ${_mugen_implibs})
