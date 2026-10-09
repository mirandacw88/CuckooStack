#!/usr/bin/env bash
# Recompiles every shader to SPIR-V 1.0 and refreshes the checked-in embedded sources in generated/.
# CMake does this automatically when a compiler is available; run this before committing shader edits so
# builds on machines without the Vulkan SDK stay in sync.
#   GLSLANG=/path/to/glslangValidator ./compile_shaders.sh     (or GLSLC=/path/to/glslc; the Android NDK ships one)
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
GLSLANG="${GLSLANG:-$(command -v glslangValidator || command -v glslang || true)}"
[ -n "$GLSLANG" ] || { [ -n "${VULKAN_SDK:-}" ] && [ -x "$VULKAN_SDK/bin/glslangValidator" ] && GLSLANG="$VULKAN_SDK/bin/glslangValidator"; } || true
GLSLC="${GLSLC:-$(command -v glslc || ls "$HOME"/Library/Android/sdk/ndk/*/shader-tools/*/glslc 2>/dev/null | tail -1 || true)}"
compile() { # src out
  if [ -n "$GLSLANG" ] && [ -x "$GLSLANG" ]; then "$GLSLANG" -V --target-env vulkan1.0 -I"$DIR" -o "$2" "$DIR/$1" >/dev/null
  elif [ -n "$GLSLC" ] && [ -x "$GLSLC" ]; then "$GLSLC" --target-env=vulkan1.0 -I"$DIR" -o "$2" "$DIR/$1"
  else echo "no shader compiler: install the Vulkan SDK, or set GLSLANG / GLSLC"; exit 1; fi
}
mkdir -p "$DIR/generated" "$DIR/spv"
for src in sky.vert sky.frag lit.vert lit.frag unlit.vert unlit.frag particle.vert particle.frag fullscreen.vert composite.frag bloom.frag text.vert text.frag image.frag; do
  sym="${src//./_}"
  compile "$src" "$DIR/spv/$src.spv"
  cmake -DINPUT="$DIR/spv/$src.spv" -DOUTPUT="$DIR/generated/$sym.cpp" -DSYMBOL="$sym" -DSOURCE_NAME="$src" -P "$DIR/../../cmake/EmbedSpirv.cmake"
  echo "ok  $src"
done
