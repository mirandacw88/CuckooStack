#!/usr/bin/env bash
# Fetches pinned third-party sources into third_party/. Re-run safely; existing checkouts are skipped.
# Versions are pinned so Android, iOS and desktop builds always compile against identical headers.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TP="$ROOT/third_party"
mkdir -p "$TP"

fetch() { # name url tag
  local dir="$TP/$1"
  if [ -d "$dir" ]; then echo "skip  $1 (present)"; return; fi
  echo "fetch $1 @ $3"
  git -c advice.detachedHead=false clone --quiet --depth 1 --branch "$3" "$2" "$dir"
  (cd "$dir" && echo "$1 $3 $(git rev-parse HEAD)") >> "$TP/VERSIONS.txt"
  rm -rf "$dir/.git"
}

fetch volk           https://github.com/zeux/volk.git                                   vulkan-sdk-1.3.296.0
fetch Vulkan-Headers https://github.com/KhronosGroup/Vulkan-Headers.git                 vulkan-sdk-1.3.296.0
fetch VMA            https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git v3.1.0
fetch glm            https://github.com/g-truc/glm.git                                   1.0.1
# stb has no release tags: pin a commit (stb_truetype.h only is used, for SDF font atlases)
if [ ! -f "$TP/stb/stb_truetype.h" ]; then
  STB_SHA=2c980bb59875b0d32144a71867fbdebb2f77cd20
  mkdir -p "$TP/stb"
  curl -sfL -o "$TP/stb/stb_truetype.h" "https://raw.githubusercontent.com/nothings/stb/$STB_SHA/stb_truetype.h"
  echo "stb $STB_SHA (stb_truetype.h)" >> "$TP/VERSIONS.txt"
  echo "fetch stb_truetype @ $STB_SHA"
fi
# stb_image.h (PNG decoding for the HUD icon atlas), same pinned stb commit
if [ ! -f "$TP/stb/stb_image.h" ]; then
  STB_SHA=$(sed -n 's/^stb \([0-9a-f]*\) .*/\1/p' "$TP/VERSIONS.txt" | head -1)
  curl -sfL -o "$TP/stb/stb_image.h" "https://raw.githubusercontent.com/nothings/stb/$STB_SHA/stb_image.h"
  echo "stb $STB_SHA (stb_image.h)" >> "$TP/VERSIONS.txt"
  echo "fetch stb_image @ $STB_SHA"
fi
# miniaudio: desktop audio output only (single header, public domain / MIT-0)
if [ ! -f "$TP/miniaudio/miniaudio.h" ]; then
  mkdir -p "$TP/miniaudio"
  curl -sfL -o "$TP/miniaudio/miniaudio.h" "https://raw.githubusercontent.com/mackron/miniaudio/0.11.25/miniaudio.h"
  echo "miniaudio 0.11.25 (miniaudio.h)" >> "$TP/VERSIONS.txt"
  echo "fetch miniaudio @ 0.11.25"
fi
# Google Mobile Ads + User Messaging Platform for iOS (static xcframeworks, from Google's SPM release binaries).
# Pinned by version and SHA-256 (the checksums in Google's Package.swift).
fetch_xcf() { # name version url sha256
  local dir="$TP/GoogleMobileAds"
  [ -d "$dir/$1.xcframework" ] && return
  mkdir -p "$dir"
  local zip="$dir/$1.zip"
  curl -sfL -o "$zip" "$3"
  echo "$4  $zip" | shasum -a 256 -c - >/dev/null || { echo "checksum mismatch for $1"; rm -f "$zip"; exit 1; }
  unzip -qo "$zip" -d "$dir" && rm -f "$zip"
  echo "$1 $2 (iOS, $3)" >> "$TP/VERSIONS.txt"
  echo "fetch $1 $2"
}
fetch_xcf GoogleMobileAds 13.11.0 "https://dl.google.com/googleadmobadssdk/310516d18f0d600c/googlemobileadsios-spm-13.11.0.zip" 310516d18f0d600c9e45ed42b955a6b8ec52108d380f7cd8ed1e424e9d3fec22
fetch_xcf UserMessagingPlatform 3.1.0 "https://dl.google.com/googleadmobadssdk/90fe6bf3b0f4ce0d/googleusermessagingplatformios-spm-3.1.0.zip" 90fe6bf3b0f4ce0d0199628c0871de58b6f673375148b98d52348aecc86db231
# Firebase iOS SDK (static xcframeworks from the official release zip): only the products the game uses, only the
# iOS device + simulator slices. Pinned by version and SHA-256 of the release asset.
FB_VERSION=12.19.1
FB_SHA=8e9fb852fcd79cca65de1eb537505717eee8e472f88c67eb74e56d5327f27f8f
if [ ! -f "$TP/Firebase/FirebaseCore.xcframework/Info.plist" ]; then
  tmp=$(mktemp -d)
  curl -sfL -o "$tmp/Firebase.zip" "https://github.com/firebase/firebase-ios-sdk/releases/download/$FB_VERSION/Firebase.zip"
  echo "$FB_SHA  $tmp/Firebase.zip" | shasum -a 256 -c - >/dev/null || { echo "checksum mismatch for Firebase.zip"; rm -rf "${tmp:?}"; exit 1; }
  (cd "$tmp" && unzip -qo Firebase.zip "Firebase/FirebaseAnalytics/*" "Firebase/FirebaseCrashlytics/*" "Firebase/FirebaseRemoteConfig/*" \
      "Firebase/FirebaseAuth/*" "Firebase/FirebaseFunctions/*" "Firebase/FirebaseMessaging/*" "Firebase/Firebase.h" "Firebase/module.modulemap")
  mkdir -p "$TP/Firebase"
  for x in "$tmp"/Firebase/*/*.xcframework; do
    n=$(basename "$x")
    [ -d "$TP/Firebase/$n" ] || cp -R "$x" "$TP/Firebase/"
  done
  cp "$tmp/Firebase/Firebase.h" "$tmp/Firebase/module.modulemap" "$TP/Firebase/"
  for sl in "$TP"/Firebase/*.xcframework/*; do # keep the iOS device + simulator slices only
    case "$(basename "$sl")" in ios-arm64|ios-arm64_x86_64-simulator|Info.plist|_CodeSignature) ;; *) rm -rf "${sl:?}";; esac
  done
  rm -rf "${tmp:?}"
  echo "Firebase iOS $FB_VERSION (Analytics, Crashlytics, RemoteConfig, Auth, Functions, Messaging)" >> "$TP/VERSIONS.txt"
  echo "fetch Firebase iOS $FB_VERSION"
fi
echo "done"
