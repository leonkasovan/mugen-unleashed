# Vendored vitaGL, built with the flags this engine needs.
#
# Why vendored at all
# -------------------
# VitaSDK ships a prebuilt libvitaGL.a whose GL_TEXTURE_1D entry points are empty
# stubs:
#
#     00002aa4 <glTexImage1D>: bx lr
#
# and vitaGL's own source says why (source/textures.c):
#
#     #ifndef HAVE_UNPURE_TEXFORMATS
#         vgl_log("GL_TEXTURE_1D support is disabled. Compile vitaGL with "
#                 "UNPURE_TEXFORMATS=1 to enable it.\n");
#
# The engine's palettes are 1D textures (src/glrender.c binds them to
# GL_TEXTURE_1D), so the SDK build cannot render a paletted sprite at all. The
# flags below are compile-time switches, so the only way to get them is to build
# the library ourselves.
#
# Which flags, and why -- established by reading vitaGL's source, not by
# guessing:
#
#   HAVE_UNPURE_TEXFORMATS  glTexImage1D forwards to glTexImage2D with height 1,
#                           giving 1D textures a real backing. Required.
#   HAVE_FFP_SHADER_SUPPORT Provides the GLSL 1.20 matrix built-ins. Confirmed in
#                           custom_shaders.c: ffp_bind_names[] =
#                           gl_ModelViewProjectionMatrix, gl_ModelViewMatrix,
#                           gl_NormalMatrix. Without it the engine's shaders do
#                           not compile. Costs a little shader-pipeline speed.
#   HAVE_UNPURE_TEXCOORDS   Multitexture with unit 0's GL_TEXTURE_2D disabled.
#                           Gl_DrawQuadIndexed binds unit 0, binds unit 1 and
#                           draws without ever calling glEnable(GL_TEXTURE_2D) --
#                           exactly the case this flag legalises.
#   TEXTURES_SPEEDHACK      Drops a last_frame write per texture bind. Verified
#                           compatible (must not be combined with
#                           HAVE_TEXTURE_CACHE).
#
# Deliberately NOT enabled:
#   PRIMITIVES_SPEEDHACK    documented to glitch GL_LINES/GL_POINTS, and the
#                           engine draws both (console cursor, debug lines).
#   SAMPLERS_SPEEDHACK      "may cause glitches" in sampler resolution; the
#                           palette sampler is the one that must be exact.
#   NO_TEX_COMBINER         the engine sets glTexEnvf(GL_TEXTURE_ENV_MODE,
#                           GL_REPLACE) on the screen-texture path.
#   DRAW/BUFFERS/INDICES/TEXTURE_UPLOADS/READBACKS_SPEEDHACK
#                           all documented as potentially crashing.
#
# Upstream: https://github.com/Rinnegatamante/vitaGL, pinned at
# third_party/vitagl/VENDORED.md.

# CMAKE_CURRENT_LIST_DIR, not CMAKE_CURRENT_SOURCE_DIR: this file is include()d,
# and CMAKE_CURRENT_SOURCE_DIR would be the including project's directory.
set(MUGEN_VITAGL_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../third_party/vitagl/src")

function(mugen_add_vitagl target)
  if(NOT EXISTS "${MUGEN_VITAGL_DIR}/source/vgl.c")
    message(FATAL_ERROR
      "Vendored vitaGL not found at ${MUGEN_VITAGL_DIR}.\n"
      "It is fetched by tools/psvita/fetch_vitagl.sh; see "
      "third_party/vitagl/VENDORED.md.")
  endif()

  file(GLOB _vgl_c   "${MUGEN_VITAGL_DIR}/source/*.c"
                     "${MUGEN_VITAGL_DIR}/source/utils/*.c")
  file(GLOB _vgl_cxx "${MUGEN_VITAGL_DIR}/source/utils/*.cpp"
                     "${MUGEN_VITAGL_DIR}/source/utils/preprocessor/*.cpp")

  add_library(${target} STATIC ${_vgl_c} ${_vgl_cxx})

  target_include_directories(${target} PUBLIC "${MUGEN_VITAGL_DIR}/source")

  target_compile_definitions(${target} PRIVATE
    HAVE_UNPURE_TEXFORMATS=1
    HAVE_FFP_SHADER_SUPPORT=1
    HAVE_UNPURE_TEXCOORDS=1
    TEXTURES_SPEEDHACK=1
    VGL_GIT_HASH="9bf3e3f")

  # vitaGL's own build line (its Makefile): -O3 -ffast-math, Cortex-A9 + NEON,
  # IEEE half floats. -Wl,-q is required for vita-elf-create's relocation table
  # and is already in the toolchain flags.
  target_compile_options(${target} PRIVATE
    -O3 -ffast-math -mtune=cortex-a9 -mfpu=neon -mfp16-format=ieee
    -Wno-incompatible-pointer-types -Wno-stringop-overflow -w)
  target_compile_options(${target} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fexceptions;-std=gnu++11;-Wno-write-strings>)
  target_compile_options(${target} PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-std=gnu11>)

  # vitaGL is C++ internally (its GLSL preprocessing uses std::string/std::map),
  # so the final link needs libstdc++ and pthread; the caller adds those.
  target_link_libraries(${target} PUBLIC
    vitashark
    SceShaccCgExt
    SceShaccCg_stub
    taihen_stub
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
    SceKernelDmacMgr_stub
    SceLibKernel_stub
    mathneon
    pthread
    stdc++
    m)
endfunction()
