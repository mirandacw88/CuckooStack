# Script mode: cmake -DINPUT=file -DOUTPUT=file.cpp -DSYMBOL=name -DNAMESPACE=cs::fontdata -P EmbedFile.cmake
# Emits the file as `const unsigned char SYMBOL[]` plus `SYMBOL_size`.
file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" len)
math(EXPR bytes "${len} / 2")
string(REGEX REPLACE "(..)" "0x\\1," body "${hex}")
get_filename_component(name "${INPUT}" NAME)
file(WRITE "${OUTPUT}" "// Generated from ${name} by cmake/EmbedFile.cmake. Do not edit.\n#include <cstddef>\nnamespace ${NAMESPACE} {\nextern const unsigned char ${SYMBOL}[];\nextern const size_t ${SYMBOL}_size;\nalignas(4) const unsigned char ${SYMBOL}[] = {${body}};\nconst size_t ${SYMBOL}_size = ${bytes};\n}\n")
