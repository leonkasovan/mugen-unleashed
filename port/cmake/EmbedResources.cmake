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
  #
  # Port fix: the resource list is written to a file the rule also depends on,
  # because DEPENDS on the globbed files alone is not enough.
  #
  # CONFIGURE_DEPENDS makes CMake notice that the set changed and re-run the
  # configure -- but the Make/Ninja generators do not treat "reconfigure" as
  # "the output of this custom command is now stale". Adding a resource left
  # the header unregenerated; deleting one was worse, because the entry stayed
  # in the generated table and the binary kept extracting the file the tree no
  # longer had anywhere to get it from. Verified on Ninja with a deleted .lua:
  # CMake printed "GLOB mismatch!" and re-ran, then reported "no work to do"
  # and the deleted script was still in lua_scripts_data.h.
  #
  # The list file is rewritten on every configure, so its timestamp moves
  # whenever the glob result changes, and the custom command re-runs because
  # it is a declared dependency. Nothing else about the rule changes.
  set(_list "${_generated}/${prefix}-resources.txt")
  get_filename_component(_directory_path "${_list}" DIRECTORY)
  file(MAKE_DIRECTORY "${_directory_path}")
  set(_names "")
  foreach(_r IN LISTS _resources)
    get_filename_component(_rname "${_r}" NAME)
    string(APPEND _names "${_rname}\n")
  endforeach()
  file(WRITE "${_list}.tmp" "${_names}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                          "${_list}.tmp" "${_list}")
  file(REMOVE "${_list}.tmp")
  add_custom_command(OUTPUT "${_header}"
    COMMAND "${CMAKE_COMMAND}"
            "-DINPUT_DIR=${root}/${directory}"
            "-DOUTPUT=${_header}"
            "-DPREFIX=${prefix}"
            "-DMODE=${mode}"
            "-DEXT=${extension}"
            "-DLAST=${last}"
            -P "${root}/port/cmake/EmbedResourcesFile.cmake"
    DEPENDS ${_resources} "${_list}" "${root}/port/cmake/EmbedResourcesFile.cmake"
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
