# Port addition: embed shaders from src/shaders as C string literals.
#
# Every shader is compiled into the binary for every target (desktop GL, GLES
# 3.0, PS Vita); nothing is read from disk at run time. The generated header
# defines a static array g_shader_<flattened-path>, where '/' and '.' in the
# path become '_' -- gles/indexed.vert -> g_shader_gles_indexed_vert -- so the
# dialects and the model shaders never share a symbol.
#
# mugen_embed_shaders(<target> <root> <relative-path>...) takes paths relative
# to <root>/src/shaders. It is the shared entry point; mugen_enable_model3d and
# mugen_enable_sprite_shaders call it with their own file lists.

function(mugen_embed_shaders target root)
  set(shader_dir "${CMAKE_CURRENT_BINARY_DIR}/model-shaders")
  set(headers "")
  foreach(shader IN LISTS ARGN)
    string(MAKE_C_IDENTIFIER "${shader}" variable)
    set(output "${shader_dir}/${shader}.h")
    add_custom_command(OUTPUT "${output}"
      COMMAND "${CMAKE_COMMAND}" "-DINPUT=${root}/src/shaders/${shader}"
              "-DOUTPUT=${output}" "-DVARIABLE=g_shader_${variable}"
              -P "${root}/port/cmake/EmbedModelShader.cmake"
      DEPENDS "${root}/src/shaders/${shader}" "${root}/port/cmake/EmbedModelShader.cmake"
      VERBATIM)
    list(APPEND headers "${output}")
  endforeach()
  # A target dependency ensures headers exist before any TU that includes them.
  # The name derives from the first shader path, so it is unique per set (a
  # target can embed more than one: Android's main embeds model + gles sprite),
  # deterministic across reconfigures, and never a global counter (which
  # collided when a build directory was reused).
  list(GET headers 0 _first_header)
  get_filename_component(_first_rel "${_first_header}" NAME_WE)
  string(MAKE_C_IDENTIFIER "${_first_rel}" _set_id)
  add_custom_target(${target}_shaders_${_set_id} DEPENDS ${headers})
  add_dependencies(${target} ${target}_shaders_${_set_id})
  target_include_directories(${target} PRIVATE "${shader_dir}")
  set(MUGEN_SHADER_HEADERS "${headers}" PARENT_SCOPE)
endfunction()

# glTF model support: cgltf, stb_image and the nine model/environment shaders.
function(mugen_enable_model3d target root)
  mugen_embed_shaders(${target} ${root}
    model.vert model.frag model_basic.vert model_basic.frag
    shadow.vert shadow.frag env.vert env_panorama.frag env_filter.frag)
  target_sources(${target} PRIVATE "${root}/third_party/cgltf-1.14/cgltf.c"
                                  "${root}/third_party/stb/stb_image.c")
  target_include_directories(${target} PRIVATE "${root}/third_party/cgltf-1.14"
                                             "${root}/third_party/stb")
  target_compile_definitions(${target} PRIVATE MUGEN_MODEL3D=1)
endfunction()

# Sprite shaders for one or more dialects. Pass the dialects the target's
# renderer compiles: "gles" for src/gles_render.c (Android, Linux arm64),
# "vita" + "desktop" for src/glrender.c on Vita (the desktop set is its embedded
# fallback for the game's own data/*.vert), "desktop" for a desktop CMake build.
function(mugen_enable_sprite_shaders target root)
  set(files "")
  foreach(dialect IN LISTS ARGN)
    foreach(name indexed.vert indexed.frag rgba.vert rgba.frag)
      list(APPEND files "${dialect}/${name}")
    endforeach()
  endforeach()
  mugen_embed_shaders(${target} ${root} ${files})
endfunction()
