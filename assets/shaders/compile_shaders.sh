#!/usr/bin/env bash
# Recompiles every shader to SPIR-V 1.0 and refreshes the checked-in embedded sources in generated/.
# CMake does this automatically when a compiler is available; run this before committing shader edits so
# builds on machines without the Vulkan SDK stay in sync.
#   GLSLANG=/path/to/glslangValidator ./compile_shaders.sh
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
GLSLANG="${GLSLANG:-$(command -v glslangValidator || command -v glslang || true)}"
[ -n "$GLSLANG" ] || { [ -n "${VULKAN_SDK:-}" ] && GLSLANG="$VULKAN_SDK/bin/glslangValidator"; }
[ -x "$GLSLANG" ] || { echo "glslangValidator not found (install the Vulkan SDK or set GLSLANG)"; exit 1; }
mkdir -p "$DIR/generated" "$DIR/spv"
for src in sky.vert sky.frag lit.vert lit.frag unlit.vert unlit.frag particle.vert particle.frag fullscreen.vert composite.frag bloom.frag text.vert text.frag image.frag; do
  sym="${src//./_}"
  "$GLSLANG" -V --target-env vulkan1.0 -I"$DIR" -o "$DIR/spv/$src.spv" "$DIR/$src" >/dev/null
  cmake -DINPUT="$DIR/spv/$src.spv" -DOUTPUT="$DIR/generated/$sym.cpp" -DSYMBOL="$sym" -DSOURCE_NAME="$src" -P "$DIR/../../cmake/EmbedSpirv.cmake"
  echo "ok  $src"
done
