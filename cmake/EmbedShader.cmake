# Turns a shader source into a C header holding it as a string:
#   cmake -DIN=<file> -DOUT=<header> -DNAME=<variable> -P EmbedShader.cmake
# Each line becomes one string literal ending in \n.

file(STRINGS "${IN}" _lines)
set(_text "/* Generated from ${IN} by cmake/EmbedShader.cmake. */\nstatic const char ${NAME}[] =\n")
foreach(_line IN LISTS _lines)
  string(REPLACE "\\" "\\\\" _line "${_line}")
  string(REPLACE "\"" "\\\"" _line "${_line}")
  string(APPEND _text "    \"${_line}\\n\"\n")
endforeach()
string(APPEND _text "    \"\";\n")
file(WRITE "${OUT}" "${_text}")
