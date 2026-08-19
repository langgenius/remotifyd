if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "INPUT and OUTPUT are required")
endif()

file(READ "${INPUT}" bundle HEX)
string(LENGTH "${bundle}" bundle_length)
set(offset 0)
set(source "#include <stddef.h>\n\nconst unsigned char remotify_ca_bundle[] = {\n")

# Byte initializers avoid the minimum string-literal limit of old C compilers.
while(offset LESS bundle_length)
    string(SUBSTRING "${bundle}" ${offset} 128 row)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, " row "${row}")
    string(APPEND source "    ${row}\n")
    math(EXPR offset "${offset} + 128")
endwhile()

string(APPEND source
    "};\n\n"
    "const size_t remotify_ca_bundle_length = sizeof(remotify_ca_bundle);\n"
)
file(WRITE "${OUTPUT}" "${source}")
