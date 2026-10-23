# Libraries that mugen.exe links statically. They are built from their original
# sources (same versions as identified in the binary) instead of being decompiled.

set(_tp "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

function(mugen_third_party target)
  target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
  # Old code: silence warnings that are not ours to fix.
  target_compile_options(${target} PRIVATE /W0)
endfunction()

# Lua 5.1.4
file(GLOB _lua_sources "${_tp}/lua-5.1.4/src/*.c")
add_library(lua51 STATIC ${_lua_sources})
target_include_directories(lua51 PUBLIC "${_tp}/lua-5.1.4/src")
mugen_third_party(lua51)

# libpng 1.2.38 (against zlib 1.2.3 from zlib1.dll)
file(GLOB _png_sources "${_tp}/libpng-1.2.38/png*.c")
list(FILTER _png_sources EXCLUDE REGEX "pngtest\\.c$")
add_library(png12 STATIC ${_png_sources})
target_include_directories(png12 PUBLIC "${_tp}/libpng-1.2.38")
target_link_libraries(png12 PUBLIC mugen_sdk)
mugen_third_party(png12)

# GLEW 1.5.4
add_library(glew_static STATIC "${_tp}/glew-1.5.4/src/glew.c")
target_include_directories(glew_static PUBLIC "${_tp}/glew-1.5.4/include")
target_compile_definitions(glew_static PUBLIC GLEW_STATIC)
target_link_libraries(glew_static PUBLIC opengl32)
mugen_third_party(glew_static)

# minizip "unzip 0.15" (zlib 1.1.3 contrib), used for zipped characters
add_library(minizip STATIC "${_tp}/minizip-0.15/unzip.c")
target_include_directories(minizip PUBLIC "${_tp}/minizip-0.15")
target_link_libraries(minizip PUBLIC mugen_sdk)
mugen_third_party(minizip)
