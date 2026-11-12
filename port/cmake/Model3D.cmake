# Port addition: shared static model dependencies and byte-preserving shaders.
function(mugen_enable_model3d target root)
  set(shader_dir "${CMAKE_CURRENT_BINARY_DIR}/model-shaders")
  set(headers "")
  foreach(shader model.vert model.frag model_basic.vert model_basic.frag
                 shadow.vert shadow.frag env.vert env_panorama.frag env_filter.frag)
    string(REPLACE "." "_" variable "${shader}")
    set(output "${shader_dir}/${shader}.h")
    add_custom_command(OUTPUT "${output}"
      COMMAND "${CMAKE_COMMAND}" "-DINPUT=${root}/src/shaders/${shader}"
              "-DOUTPUT=${output}" "-DVARIABLE=g_shader_${variable}"
              -P "${root}/port/cmake/EmbedModelShader.cmake"
      DEPENDS "${root}/src/shaders/${shader}" "${root}/port/cmake/EmbedModelShader.cmake"
      VERBATIM)
    list(APPEND headers "${output}")
  endforeach()
  # A target dependency ensures headers exist before any model TU is compiled.
  add_custom_target(${target}_model_shaders DEPENDS ${headers})
  add_dependencies(${target} ${target}_model_shaders)
  target_sources(${target} PRIVATE "${root}/third_party/cgltf-1.14/cgltf.c"
                                  "${root}/third_party/stb/stb_image.c")
  target_include_directories(${target} PRIVATE "${root}/third_party/cgltf-1.14"
                                             "${root}/third_party/stb" "${shader_dir}")
  target_compile_definitions(${target} PRIVATE MUGEN_MODEL3D=1)
endfunction()
