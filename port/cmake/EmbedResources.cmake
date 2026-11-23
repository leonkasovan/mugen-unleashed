# Port addition: embed text resources under res/ as C data.
#
# mugen_embed_configs(<target> <root>) embeds res/configs/*.cfg as string
# literals; mugen_embed_lua(<target> <root>) embeds res/lua/*.lua as byte
# arrays and an EmbeddedFile table. Both write a header into the build tree and
# put its directory on the target's include path. src/configembed.c and
# src/luascripts.c write missing files into data/ at start-up.
#
# The GNU Make build uses tools/embed_resources.sh; both generators normalise
# line endings to LF, so every target embeds the same bytes.
include_guard(GLOBAL)

function(mugen_embed_resources target root directory output prefix mode extension last)
  set(_generated "${CMAKE_CURRENT_BINARY_DIR}/embedded-${prefix}")
  set(_header "${_generated}/${output}")
  file(GLOB _resources CONFIGURE_DEPENDS "${root}/${directory}/*.${extension}")
  add_custom_command(OUTPUT "${_header}"
    COMMAND "${CMAKE_COMMAND}"
            "-DINPUT_DIR=${root}/${directory}"
            "-DOUTPUT=${_header}"
            "-DPREFIX=${prefix}"
            "-DMODE=${mode}"
            "-DEXT=${extension}"
            "-DLAST=${last}"
            -P "${root}/port/cmake/EmbedResourcesFile.cmake"
    DEPENDS ${_resources} "${root}/port/cmake/EmbedResourcesFile.cmake"
    VERBATIM)
  add_custom_target(${target}_${prefix} DEPENDS "${_header}")
  add_dependencies(${target} ${target}_${prefix})
  target_include_directories(${target} PRIVATE "${_generated}")
endfunction()

function(mugen_embed_configs target root)
  mugen_embed_resources(${target} ${root} res/configs config_data.h config string cfg "")
endfunction()

function(mugen_embed_lua target root)
  mugen_embed_resources(${target} ${root} res/lua lua_scripts_data.h lua hex lua vector.lua)
endfunction()
