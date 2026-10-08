# Script mode: cmake -DINPUT=x.spv -DOUTPUT=x.cpp -DSYMBOL=name -P EmbedSpirv.cmake
# Emits the SPIR-V words as a uint32_t array (SPIR-V is little-endian, so bytes are reordered per word).
file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" len)
math(EXPR rem "${len} % 8")
if(NOT rem EQUAL 0)
  message(FATAL_ERROR "${INPUT} is not a whole number of 32-bit words")
endif()
math(EXPR bytes "${len} / 2")
string(REGEX REPLACE "(..)(..)(..)(..)" "0x\\4\\3\\2\\1u," words "${hex}")
string(REGEX REPLACE "((0x[0-9a-f]+u,){8})" "\\1\n    " words "${words}")
file(WRITE "${OUTPUT}" "// Generated from ${SOURCE_NAME} by cmake/EmbedSpirv.cmake. Do not edit.\n#include <cstddef>\n#include <cstdint>\nnamespace cs::spv {\nextern const uint32_t ${SYMBOL}[];\nextern const size_t ${SYMBOL}_size;\nalignas(4) const uint32_t ${SYMBOL}[] = {\n    ${words}\n};\nconst size_t ${SYMBOL}_size = ${bytes};\n} // namespace cs::spv\n")
