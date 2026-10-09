# Cuckoo Stack: native Vulkan port

A C++20 port of the WebGL game in [`cuckoo-stack.html`](cuckoo-stack.html) to a Vulkan 1.0 renderer that runs on:

- **Android 7.0+ (API 24)**: native Vulkan through GameActivity. Targets API 36, Google Play's requirement since
  31 Aug 2026.
- **iOS 15+**: Vulkan on Metal through MoltenVK (dynamic `MoltenVK.xcframework`, embedded in the app).
- **Desktop** (testing): GLFW, on any Vulkan driver or on MoltenVK on macOS.

See [`docs/PORTING.md`](docs/PORTING.md) for the source analysis, architecture, verification status and the list of
features not ported yet.

## Layout

```
cuckoo-stack.html        original WebGL game (reference)
CMakeLists.txt           one build for every platform
cmake/                   shader compile + SPIR-V embedding
assets/shaders/          GLSL 450 sources, compile_shaders.sh, generated/ (pre-built SPIR-V fallback)
assets/fonts/            web-build fonts, subset + OFL licences (scripts/fetch_fonts.sh)
assets/splash/           Aerosphere Games logo for the opening splash (scripts/splash/make_splash.py)
src/main.cpp             desktop entry point (GLFW)
src/core/                game logic, procedural geometry/world, camera, maths   (no OS headers, no Vulkan)
src/graphics/            Vulkan 1.0 + volk + VMA renderer                       (no OS headers)
src/platform/            PlatformSurface + Android (ANativeWindow) / iOS (CAMetalLayer) bridges
platforms/android/       Gradle project, manifest, CuckooActivity (GameActivity)
platforms/ios/           Info.plist, launch screen, Xcode project generator
third_party/             volk, Vulkan-Headers, VMA, glm (pinned; see VERSIONS.txt)
tests/                   course-parity test against the web build
```

## Build

Fetch dependencies once:

```bash
scripts/bootstrap_deps.sh
```

### Desktop

Needs CMake 3.22+, Ninja and a Vulkan driver. On macOS, install the Vulkan SDK.

```bash
cmake -S . -B build -G Ninja && cmake --build build
```

```bash
./build/cuckoo_stack
```

Controls: Space, Enter or Up lays an egg; click taps; M mutes; Esc quits. `--validate` enables the Khronos
validation layer. `--autoplay --capture out.ppm 120` renders 120 frames, saves the last one and exits, which is
useful for golden-image checks.

### iOS

```bash
VULKAN_SDK=~/VulkanSDK/<version>/macOS platforms/ios/generate_xcode.sh <TEAM_ID>
```

This generates `build-ios/CuckooStack.xcodeproj`. Open it in Xcode, then build and run. You can also pass
`MOLTENVK=/path/to/MoltenVK.xcframework` (the *dynamic* one) instead of `VULKAN_SDK`.

For the Simulator from the command line, sign ad-hoc, because an unsigned MoltenVK framework won't load:

```bash
xcodebuild -project build-ios/CuckooStack.xcodeproj -scheme CuckooStack -sdk iphonesimulator CODE_SIGN_IDENTITY=- CODE_SIGN_STYLE=Manual build
```

### Android

Needs NDK `28.2.13676358` and CMake `3.22.1` from the SDK Manager, and **JDK 17 or 21**. Gradle 8.13 cannot run on
the JDK 25 bundled with current Android Studio.

```bash
cd platforms/android && JAVA_HOME=$(/usr/libexec/java_home -v 21) ./gradlew installStagingDebug
```

```bash
adb shell am start -n com.cuckoostack.aerospheregames.staging/com.cuckoostack.aerospheregames.CuckooActivity
```

Gradle builds the root `CMakeLists.txt` through the NDK (r28). Shaders compile with the NDK's `glslc`.

### Environments: staging and prod

Two Firebase projects and two app IDs, so test data never mixes with real players and both apps install side by side:

| | Staging | Prod |
|---|---|---|
| App ID | `com.cuckoostack.aerospheregames.staging` ("Cuckoo Stack β", ribbon icon) | `com.cuckoostack.aerospheregames` |
| Android | `./gradlew installStagingDebug` | `./gradlew installProdDebug` / `bundleProdRelease` |
| iOS | `platforms/ios/generate_xcode.sh` → `build-ios/` | `platforms/ios/generate_xcode.sh --env prod` → `build-ios-prod/` |
| Ads | always Google's test IDs | real IDs in Release (the build fails if any `_PROD` ID is empty) |
| Firebase | `cuckoostack-staging` | `cuckoostack-prod` |

Firebase config files are git-ignored: put `google-services.json` in `platforms/android/app/src/<env>/` and
`GoogleService-Info.plist` in `config/firebase/<env>/`. Without them the game runs with Firebase off.

### Monetization and retention

Unlimited plays. Money comes from AdMob (an interstitial at most every 3 runs and 90 s, never in a new player's
first 5 runs, never during a run) plus optional rewarded ads, and from in-app purchases (coin packs, a starter pack,
Remove Ads). Players come back for the daily course, a daily drop, three daily missions, a streak, levels and outfits,
reminders (13+, opt-in), challenge links and daily leaderboards.

- **Every tunable** (prices, rewards, ad spacing, difficulty boost, reminder hours, live events) is in
  [`src/core/Tuning.h`](src/core/Tuning.h) and can be changed from Firebase Remote Config without a release.
- **Every ad rule** is in [`src/core/Monetization.h`](src/core/Monetization.h).
- **Setup checklist** (AdMob, Firebase, store products, leaderboards, links, deploys, store forms):
  [`docs/LIVE_OPS.md`](docs/LIVE_OPS.md).
- **Audience:** the game asks for a birth year once. Under-13s get child-directed G-rated ads and no tracking
  prompt, reminders, social features or loss-framed offers, and purchases go behind a parental gate.

### Tests

```bash
cd build && ctest
```

Headless checks of the meta-game: ad rules, economy and purchases, streak, missions, daily drop, reminders, age
screen and challenge links (`tests/*_test.cpp`).

```bash
tests/run_level_parity.sh
```

This needs Node.js. It checks that the C++ course generator reproduces the web build's daily course exactly.

Audio parity: render the native synth (`render_audio`, built with the desktop target) and the web build's own audio
code through Web Audio for Node (`npm install node-web-audio-api` into some folder), then compare:

```bash
./build/render_audio native.wav
```

```bash
NODE_PATH=/path/to/node_modules node tests/web_audio_reference.mjs web.wav
```

```bash
python3 tests/compare_audio.py native.wav web.wav
```

## Editing shaders

Edit the GLSL in `assets/shaders/`. CMake recompiles it automatically when `glslangValidator` or `glslc` is
available. Before committing, run `assets/shaders/compile_shaders.sh` to refresh `generated/`, which machines
without a shader compiler use.
