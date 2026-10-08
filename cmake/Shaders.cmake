# Offline GLSL -> SPIR-V 1.0 (--target-env vulkan1.0) -> embedded C++ arrays.
# Compiler search order: glslangValidator / glslc from $VULKAN_SDK, the Android NDK's shader-tools, then PATH.
# Without a compiler, the checked-in sources in assets/shaders/generated are used (refresh them with
# assets/shaders/compile_shaders.sh after editing a shader).

set(CS_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders)
set(CS_SHADERS sky.vert sky.frag lit.vert lit.frag unlit.vert unlit.frag particle.vert particle.frag fullscreen.vert composite.frag bloom.frag text.vert text.frag image.frag)

set(_hints "")
if(DEFINED ENV{VULKAN_SDK})
  list(APPEND _hints "$ENV{VULKAN_SDK}/bin")
endif()
if(ANDROID_NDK)
  file(GLOB _ndk_tools "${ANDROID_NDK}/shader-tools/*")
  list(APPEND _hints ${_ndk_tools})
endif()
find_program(CS_GLSLANG NAMES glslangValidator glslang HINTS ${_hints})
find_program(CS_GLSLC NAMES glslc HINTS ${_hints})

set(CS_SHADER_SOURCES "")
if(CS_GLSLANG OR CS_GLSLC)
  set(_out ${CMAKE_CURRENT_BINARY_DIR}/shaders)
  file(MAKE_DIRECTORY ${_out})
  foreach(sh ${CS_SHADERS})
    string(REPLACE "." "_" sym ${sh})
    set(spv ${_out}/${sh}.spv)
    set(cpp ${_out}/${sym}.cpp)
    if(CS_GLSLANG)
      set(cmd ${CS_GLSLANG} -V --target-env vulkan1.0 -I${CS_SHADER_DIR} -o ${spv} ${CS_SHADER_DIR}/${sh})
    else()
      set(cmd ${CS_GLSLC} --target-env=vulkan1.0 -I${CS_SHADER_DIR} -o ${spv} ${CS_SHADER_DIR}/${sh})
    endif()
    add_custom_command(
      OUTPUT ${cpp}
      COMMAND ${cmd}
      COMMAND ${CMAKE_COMMAND} -DINPUT=${spv} -DOUTPUT=${cpp} -DSYMBOL=${sym} -DSOURCE_NAME=${sh} -P ${CMAKE_CURRENT_LIST_DIR}/EmbedSpirv.cmake
      DEPENDS ${CS_SHADER_DIR}/${sh} ${CS_SHADER_DIR}/frame.glsl ${CMAKE_CURRENT_LIST_DIR}/EmbedSpirv.cmake
      COMMENT "SPIR-V 1.0: ${sh}"
      VERBATIM)
    list(APPEND CS_SHADER_SOURCES ${cpp})
  endforeach()
  message(STATUS "Cuckoo Stack: compiling shaders with ${CS_GLSLANG}${CS_GLSLC}")
else()
  foreach(sh ${CS_SHADERS})
    string(REPLACE "." "_" sym ${sh})
    list(APPEND CS_SHADER_SOURCES ${CS_SHADER_DIR}/generated/${sym}.cpp)
  endforeach()
  message(WARNING "Cuckoo Stack: no glslangValidator/glslc found; using pre-compiled shaders from assets/shaders/generated")
endif()

add_library(cs_shaders STATIC ${CS_SHADER_SOURCES})
