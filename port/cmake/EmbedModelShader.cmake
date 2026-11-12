# Hex bytes avoid CMake list splitting and preserve every shader character.
file(READ "${INPUT}" bytes HEX)
string(REGEX REPLACE "(..)" "0x\\1," bytes "${bytes}")
get_filename_component(directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${directory}")
file(WRITE "${OUTPUT}.tmp" "static const char ${VARIABLE}[] = {${bytes}0};\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${OUTPUT}.tmp" "${OUTPUT}"
                RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Cannot write model shader ${OUTPUT}")
endif()
file(TOUCH "${OUTPUT}")
file(REMOVE "${OUTPUT}.tmp")
