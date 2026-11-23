# Port addition: embed res/configs/*.cfg as C string data.
#
# mugen_embed_configs(<target> <root>) generates config_data.h from
# <root>/res/configs and puts its directory on the target's include path. The
# header defines g_config_<flattened-name> for every config; src/configembed.c
# includes it and writes the missing data/mugen.cfg and platform overlay when
# the engine starts, so no build step copies a config into the game tree.
#
# The GNU Make build does the same through tools/embed_configs.sh; both embed
# the same bytes on every host (line endings are LF).
function(mugen_embed_configs target root)
  set(_config_dir "${CMAKE_CURRENT_BINARY_DIR}/embedded-configs")
  set(_header "${_config_dir}/config_data.h")
  file(GLOB _configs CONFIGURE_DEPENDS "${root}/res/configs/*.cfg")
  add_custom_command(OUTPUT "${_header}"
    COMMAND "${CMAKE_COMMAND}"
            "-DINPUT_DIR=${root}/res/configs"
            "-DOUTPUT=${_header}"
            -P "${root}/port/cmake/EmbedConfigsFile.cmake"
    DEPENDS ${_configs} "${root}/port/cmake/EmbedConfigsFile.cmake"
    VERBATIM)
  add_custom_target(${target}_configs DEPENDS "${_header}")
  add_dependencies(${target} ${target}_configs)
  target_include_directories(${target} PRIVATE "${_config_dir}")
endfunction()
