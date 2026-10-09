#!/usr/bin/env bash
# Generates the iOS Xcode project from the root CMakeLists.txt, one build folder per environment.
#   ./generate_xcode.sh [TEAM_ID]              staging (default): build-ios/            .staging app ID, test ads
#   ./generate_xcode.sh --env prod [TEAM_ID]   prod:              build-ios-prod/       store app ID, real ads in Release
#   MOLTENVK=/path/to/MoltenVK.xcframework ./generate_xcode.sh [TEAM_ID]
# Then open build-ios[-prod]/CuckooStack.xcodeproj (or: cmake --build build-ios --config Release -- -sdk iphonesimulator).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MVK="${MOLTENVK:-$ROOT/third_party/MoltenVK/MoltenVK.xcframework}"  # vendored dynamic MoltenVK 1.4.2 (iOS slices)
ENV=staging
if [ "${1:-}" = "--env" ]; then ENV="${2:?--env needs staging or prod}"; shift 2; fi
OUT="$ROOT/build-ios"
[ "$ENV" = "prod" ] && OUT="$ROOT/build-ios-prod"
[ -d "$MVK" ] || { echo "MoltenVK.xcframework not found at '$MVK' (set MOLTENVK or VULKAN_SDK)"; exit 1; }
cmake -S "$ROOT" -B "$OUT" -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCS_MOLTENVK_XCFRAMEWORK="$MVK" \
  -DCS_ENV="$ENV" \
  -DCS_IOS_TEAM_ID="${1:-}"
echo "Open $OUT/CuckooStack.xcodeproj ($ENV)"
