#!/usr/bin/env bash
# Generates the iOS Xcode project from the root CMakeLists.txt.
#   ./generate_xcode.sh [TEAM_ID]          (uses third_party/MoltenVK)
#   MOLTENVK=/path/to/MoltenVK.xcframework ./generate_xcode.sh [TEAM_ID]
# Then open build-ios/CuckooStack.xcodeproj (or: cmake --build build-ios --config Release -- -sdk iphonesimulator).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MVK="${MOLTENVK:-$ROOT/third_party/MoltenVK/MoltenVK.xcframework}"  # vendored dynamic MoltenVK 1.4.2 (iOS slices)
[ -d "$MVK" ] || { echo "MoltenVK.xcframework not found at '$MVK' (set MOLTENVK or VULKAN_SDK)"; exit 1; }
cmake -S "$ROOT" -B "$ROOT/build-ios" -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCS_MOLTENVK_XCFRAMEWORK="$MVK" \
  -DCS_IOS_TEAM_ID="${1:-}"
echo "Open $ROOT/build-ios/CuckooStack.xcodeproj"
