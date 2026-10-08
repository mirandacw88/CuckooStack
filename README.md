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
cd platforms/android && JAVA_HOME=$(/usr/libexec/java_home -v 21) ./gradlew installDebug
```

```bash
adb shell am start -n com.cuckoostack.aerospheregames/.CuckooActivity
```

Gradle builds the root `CMakeLists.txt` through the NDK (r28). Shaders compile with the NDK's `glslc`.

### Lives and ads (AdMob)

Players start with 3 lives (`src/core/Lives.h`), and each finished run uses one. Lives are saved between launches,
and quitting mid-run still costs that run's life. At 0 lives, an "Out of lives" offer opens over the game-over
screen: watching a **rewarded** video grants 3 more lives. Ads only appear when the player chooses to watch one.

If no ad can load within 8 seconds (offline, no fill, consent forbids ads), the offer turns into "Play anyway" so
players are never locked out. Switch this off with `GRANT_WHEN_AD_UNAVAILABLE = false`.

Ads run behind Google's UMP consent (EEA/UK/Switzerland and US state privacy laws) and, on iOS, the App Tracking
Transparency prompt. A "Privacy settings" button appears on the title screen wherever UMP requires one.

**Ad IDs live in [`config/ads.env`](config/ads.env)**, one file for both platforms, with a `_TEST` and a `_PROD`
value for each ID:
- **Debug builds** (Xcode Run, `./gradlew installDebug`) use the `_TEST` values, which are Google's test IDs.
- **Release builds** (App Store, Google Play) use the `_PROD` values. Paste your IDs from the AdMob console.

If a `_PROD` value is empty, release builds fall back to the test ID and the build prints a warning. Both Gradle
and Xcode pick up edits on the next build.

You also need to:
- Create the consent messages (GDPR and US states) in the AdMob console under **Privacy & messaging**.
- Declare advertising and the device/advertising ID in Google Play's **Data safety** form and Apple's **App
  Privacy** answers. The SDKs' privacy manifests are already bundled.

The iOS SDKs (Google Mobile Ads 13.11.0, UMP 3.1.0) are fetched by `scripts/bootstrap_deps.sh` and verified by
checksum.

### Tests

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
