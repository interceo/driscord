# Usage: cmake -P spv_to_header.cmake <input.spv> <output.h> <array_name>
# Reads a SPIR-V binary and writes a C header with an embedded byte array.

set(INPUT  "${CMAKE_ARGV3}")
set(OUTPUT "${CMAKE_ARGV4}")
set(NAME   "${CMAKE_ARGV5}")

file(READ "${INPUT}" SPV_HEX HEX)
string(LENGTH "${SPV_HEX}" HEX_LEN)
math(EXPR BYTE_COUNT "${HEX_LEN} / 2")

set(ARRAY_BODY "")
math(EXPR LAST "${HEX_LEN} - 2")
foreach(i RANGE 0 ${LAST} 2)
    string(SUBSTRING "${SPV_HEX}" ${i} 2 BYTE)
    if(ARRAY_BODY)
        string(APPEND ARRAY_BODY ",")
    endif()
    string(APPEND ARRAY_BODY "0x${BYTE}")
endforeach()

file(WRITE "${OUTPUT}"
    "// Auto-generated SPIR-V embedding\n"
    "static const unsigned char ${NAME}[] = {${ARRAY_BODY}};\n"
    "static const unsigned int ${NAME}_len = ${BYTE_COUNT};\n"
)
