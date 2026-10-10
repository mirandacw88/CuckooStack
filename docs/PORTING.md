# Cuckoo Stack: WebGL → Vulkan port notes

This document records the analysis of `cuckoo-stack.html` (Phase 1), how each web subsystem maps onto the native
engine (Phase 2), and what is not ported yet.

---

## 1. Source analysis (`cuckoo-stack.html`)

The web build is a single 1,920-line HTML file: CSS/DOM HUD, a `three@0.160` scene with `EffectComposer`
post-processing, and a Web Audio synthesizer. There are no external art or audio assets. Every texture is drawn
into a `<canvas>` at startup, and every sound is synthesized live.

### 1.1 Game loop and state

| Area | Web implementation | Native location |
|---|---|---|
| Loop | `requestAnimationFrame(frame)`, `rdt = min(0.033, raw)`; `dt = rdt × timeScale` (hit-stop `freeze` ⇒ ×0.04, slow-mo on death / close call) | `Game::update` |
| States | `'title' → 'play' → 'dead'`; restart allowed after `deadT > 0.8 s` | `Game::State` |
| Input | `pointerdown`, `touchstart` (prevented), `keydown` Space/Enter/ArrowUp (no repeat); mute button | `Game::press/pressAt`, platform layers |
| Constants | `U = 0.6` (egg/crate), `DEPTH = 1.3`, `MAXE = 8`, `SECTOR = 150 m`, `HEN_H = 1.05` | `Level.h`, `Game.h` |
| Difficulty | `curve(d)`: smoothstep over 350 m → speed 8→12.4 m/s, regen 0.5→0.92 s/egg, wall cap 4→8, segment length, drop/ceiling/corn chances | `Difficulty.cpp` (doubles, bit-exact) |
| Adaptive difficulty | `DDA`: assist rubber band to today's best, sticking-zone Gaussian relief, skill EMA; changes only speed and refill | `Dda` |
| Daily course | `mulberry32(seedFor('cluckstack:' + YYYY-MM-DD))` | `Math.h` + `Level::generateUntil` (**verified identical**, `tests/run_level_parity.sh`) |
| Physics | Hen moves at `speed`; wall ahead taller than stack top − 0.06 ⇒ death; otherwise the wall shears `k` eggs (`knock`); exact fit ⇒ `perfect` streak bonus (3 × streak); falling `vy -= 36·dt`; landing squash | `Game::updatePlay` |
| Barriers | Overhead girder at `ceil` eggs; head above underside ⇒ death; clearance < 0.35 ⇒ `closeCall` +2 | `Game::updatePlay` |
| Scoring | `floor(distance) + bonus`; disco ball +5 and +1.5 eggs; best / daily best / attempts in `localStorage` | `Game`, `FileStorage` |
| Juice | Screen shake, camera kick, chroma kick, flashes, popups, egg grow (`easeOutBack`), jiggle on landing, sway | `Game` |

### 1.2 Graphics state

| Web | Details | Native |
|---|---|---|
| Renderer | `WebGLRenderer`, `antialias:false`, DPR ≤ 1.5, sRGB output, PCF shadows (1024²), adaptive quality tiers | Vulkan 1.0 renderer; shadows/tiers pending (§3) |
| Camera | `PerspectiveCamera(50°, aspect, 0.1, 900)`; distance chosen so 9.2 m of track fits the width (clamped 9–18) | `Camera` (GLM, Y-flip, depth 0..1) |
| Lights | Hemisphere `#5d4cb0/#140a1e` 0.85; directional moon `#a9b8ff` 1.1 following the camera; point `pinkL` 14.4 (16 m); hero point light 7 (6.5 m); `cyanL` is created but never added to the scene | `FrameParams`, `lit.frag` |
| Environment | PMREM of a night-sky sphere, a dark floor and 5 neon panels | Analytic `envColor()` in `lit.frag` |
| Materials | `MeshStandard` / `MeshPhysical` (clearcoat, iridescence, sheen), `onBeforeCompile` fresnel rim (`addRim`) | GGX + Lambert + Karis env-BRDF + rim; clearcoat, iridescence and sheen approximated |
| Shaders (custom GLSL) | Sky gradient + moon + star hash; FX points (`gl_PointSize`); searchlight fresnel beams; final pass (chromatic aberration + vignette + ACES RRT/ODT fit + sRGB) | `sky.frag`, `particle.*`, `composite.frag` (formulae ported verbatim) |
| Geometry | Box, Sphere, Cylinder/Cone, Torus (arcs), Lathe (egg), Plane, Ring, Circle, Capsule | `Geometry.cpp`: three.js generators ported with identical winding |
| Instancing | `InstancedMesh` for crates, bales, tiles (≤ 2,400), girders, caps; merged skyline rows | One instanced draw per `(pass, mesh)` bucket; 128-byte instance stream |
| Canvas textures | Container steel and amber trim, hazard chevrons, wet asphalt and puddle roughness, window grid emissive, disco facets, feather, curtain, streak, radial pad, dot, puff, neon sign text, skyline silhouettes | Evaluated procedurally in `lit.frag` / `unlit.frag` / `particle.frag`; no texture uploads (signs and silhouettes: §3) |
| Render states | Opaque front-face culling; transparent with `depthWrite:false`; Normal (`SrcAlpha, 1−SrcAlpha`) and Additive (`SrcAlpha, One`) blending; `renderOrder` | Lit (cull back, depth write), UnlitAlpha / UnlitAdd / particles (no cull, depth test, no write) |
| Post | `RenderPass` → `UnrealBloomPass(1.0, 0.55, 1.15)` with a NaN/overflow clamp → chroma/ACES pass → FXAA | HDR pass → ¼-res bloom (prefilter + separable blur) → composite. FXAA pending |
| Fog | `FogExp2('#1d0b36', 0.016)`, colour drifts per sector grade | `lit.frag` |

### 1.3 Audio and assets

- **SFX** (`tone`, `noise`): oscillator sweeps with exponential envelopes and band-passed noise for lay (pentatonic
  scale by stack height), crack, perfect (arpeggio, pitched by streak), corn, land, squawk and empty.
- **Music**: a synthesized progressive-house loop (126 BPM, rising with speed; a 32-bar A/B/C/D arrangement with a
  filter sweep, riser, build and drop). It has a kick with a sidechain pump, clap, hats, saw stabs, a plucked arp
  through a feedback delay, bass, and a compressor on the master. `music.pulse()` (kick envelope) drives the
  visuals: disco floor, emissive breathing and the hen's head bob.
- **iOS audio unlock** hacks (silent WAV keep-alive, `navigator.audioSession`) are web-only and not needed natively.
- **Persistence**: `cluckstack-best`, `cluckstack-daily`, `cluckstack-dda`, `cluckstack-music`.
- **External assets**: three.js from jsDelivr and Google Fonts (Orbitron, Chakra Petch). Nothing else.

---

## 2. Native architecture

```
src/core      Game logic and procedural content. Pure C++20 + GLM. No OS headers, no Vulkan.
src/graphics  Vulkan 1.0 renderer (volk + VMA). No OS headers. Consumes core::RenderList.
src/platform  PlatformSurface implementations + entry points (Android, iOS, desktop main.cpp).
```

- **Contract**: `Game::update()` produces a `RenderList` holding instance buckets per `(pass, mesh)`, particle
  streams, HUD quads and `FrameParams`. The renderer never sees game rules, and the game never sees Vulkan.
- **Loader**: `PlatformSurface::loadVulkan()`. Android and desktop call `volkInitialize()`, which `dlopen`s the system
  loader. iOS `dlopen`s the embedded `MoltenVK.framework` from the bundle and calls `volkInitializeCustom`.
  `libvulkan` is never linked. Failure at any stage returns a readable error that the platform shows (an Android
  `AlertDialog`, an iOS `UIAlertController`, or desktop stderr with exit code 1) instead of crashing.
- **Portability**: if the instance reports `VK_KHR_portability_enumeration`, the context enables it together with
  `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR` and `VK_KHR_get_physical_device_properties2`. It also enables
  `VK_KHR_portability_subset` on the device whenever the device exposes it. This is runtime detection, not
  `#ifdef __APPLE__`.
- **Frame graph**:
  1. *Scene pass*: RGBA16F colour (CLEAR / STORE, sampled next) plus depth (CLEAR / DONT_CARE,
     `TRANSIENT_ATTACHMENT`, lazily-allocated memory). Draw order: sky (full-screen triangle at the far plane),
     lit opaque instances, alpha unlit, smoke, additive unlit, additive particles.
  2. *Bloom*: ¼-resolution prefilter (threshold 1.15, soft knee, NaN/overflow clamp), then horizontal and vertical
     9-tap blur. Every pass uses CLEAR on load.
  3. *Present pass*: composite (chroma, vignette, ACES, manual sRGB on UNORM swapchains) plus the HUD, both
     pre-rotated for Android's `currentTransform`.
- **Sync**: 2 frames in flight (fence + `imageAvailable` per frame), plus a `renderFinished` semaphore per
  swapchain image. FIFO present, 3 images. `OUT_OF_DATE` / `SUBOPTIMAL` trigger recreation, and `SURFACE_LOST`
  tears down until the window returns.
- **Memory**: VMA everywhere. Static meshes live in one device-local VB/IB filled through a staging buffer.
  Per-frame UBO, instance and particle streams are persistently mapped `HOST_ACCESS_SEQUENTIAL_WRITE` and flushed
  explicitly, which matters on non-coherent heaps.
- **Shaders**: GLSL 450 compiled to SPIR-V 1.0 (`--target-env vulkan1.0`) and embedded as `uint32_t[]`. CMake
  compiles them with `glslangValidator` (or `glslc` from the Vulkan SDK or NDK), and falls back to the checked-in
  `assets/shaders/generated/*.cpp`. Only core features are used, with no optional device features enabled.
  Particles are instanced quads rather than `gl_PointSize`, because points larger than 1 px need the optional
  `largePoints` feature.

## 3. Verification status

| Check | Result |
|---|---|
| Course generator vs. the web build (5 dates × 5 km) | Byte-identical (`tests/run_level_parity.sh`, also `ctest`) |
| Desktop build (macOS, Apple clang, Ninja) | Clean; the only suppressed warning is `-Wmissing-field-initializers` for `{VK_STRUCTURE_TYPE_…}` |
| Desktop run (MoltenVK 1.4.2, Apple M2) | Title and gameplay frames captured with `--capture`; visually matches the web build |
| Missing Vulkan driver | Clean diagnostic, exit code 1 |
| iOS device build (Xcode 26.2, `-G Xcode`, iOS 15.0 min) | Builds and links; `MoltenVK.framework` embedded in `Frameworks/` |
| iOS Simulator (iPhone 17 Pro Max, iOS 26.2) | Runs: title, gameplay and game-over screens, touch input, safe-area insets |
| Android build (AGP 8.13.2, Gradle 8.13, NDK r28c, JDK 21) | `assembleDebug` succeeds for arm64-v8a, armeabi-v7a and x86_64 |
| Android emulator (Pixel 10 Pro XL, API 37, 16 KB pages, Vulkan via gfxstream) | Runs: gameplay renders, swapchain recreation on resize works |
| SPIR-V | All 13 shaders compile as SPIR-V 1.0 |

### Audio

**Soundtrack (recorded).** The background music is now a recorded track in `assets/music/`, embedded in the build
and streamed by `src/core/audio/Music.{h,cpp}` (miniaudio's MP3 decoder, cubic resampling to the device rate, mixed
in stereo under the synth's sound effects). Midnight City Sprint plays on the title screen and during runs; a surge
eases it up to 1.15× speed (about two semitones higher, like a fast-forwarded tape) and back down afterwards; a crash
winds the tape down. The synthesized songs below remain as the fallback when no tracks are built in, and every sound effect is
still synthesized. Visuals pulse on the track's beat grid (`MusicTracks.inc`), scaled by the playback speed. To change the music,
replace the file and run `python3 scripts/audio/analyze_music.py` (tempo, first beat and loudest section).

Apart from the soundtrack, there are no audio assets: like the web build, every sound is synthesized live. `src/core/audio/` ports the Web
Audio graph the game uses onto a small engine. It has `AudioParam` automation (set, linear and exponential ramps,
set-target, cancel), PolyBLEP band-limited oscillators, `BiquadFilterNode` with the spec's coefficient formulas
(Q in dB for lowpass/highpass), the feedback delay, sidechain pump, master filter, and the
`DynamicsCompressorNode` static curve as the spec defines it. The `sfx` and `music` code is translated line for
line: the 32-bar A/B/C/D arrangement, tempo following speed, build, drop and power-down.

- **Threading**: the game thread enqueues commands into a lock-free SPSC queue. The audio callback drains it and
  runs the music scheduler per block, with the same 150 ms lookahead the web build gets from `setInterval`.
- **Outputs**: Oboe on Android (AAudio on API 27+, OpenSL ES on 24–26; reopens on device change, stops when paused);
  AVAudioEngine plus `AVAudioSourceNode` on iOS (session category Playback, as the web build sets
  `navigator.audioSession.type`, with interruption and route recovery); miniaudio on desktop.
- **Verified against the web code**: `tests/web_audio_reference.mjs` renders the HTML's own audio functions through
  a real Web Audio implementation (`node-web-audio-api`). `tests/render_audio.cpp` renders the native synth with
  the same script, and `tests/compare_audio.py` compares them. With the master compressor bypassed in both, the
  music matches within 0.0–0.3 dB in every 6 s window, and every sound effect matches in level and peak. With the
  compressor on, native is about 1.2 dB louder: the reference library uses a simplified quadratic-knee
  compressor, while this port follows the spec's exponential knee, which Chrome and Safari implement.

### Performance, heat and battery

- **Internal resolution:** the 3D scene and bloom render at about 2 px per point (`src/graphics/Quality.h`), 0.67
  on 3× phones, and are upscaled in the composite pass. The HUD and text stay at native resolution.
- **Frame rate:** capped at 60 fps. 120 Hz ProMotion would double the work for little visible gain.
  - iOS: `CADisplayLink` frame-rate range.
  - Android: `ANativeWindow_setFrameRate` plus CPU frame pacing.
- **Thermal response:** quality steps down with the OS's thermal state (iOS `thermalState`, Android
  `PowerManager` thermal status): 0.9 → 0.75 → 0.6 scale, and 30 fps at critical.
- **Baked patterns:** constant procedural patterns are computed once at startup. The wet-asphalt puddle loop
  (14 ellipse tests per pixel) is now one texture lookup (`BakedPatterns.cpp`), which cut scene GPU time by about 25%.
- **Pipeline cache:** the `VkPipelineCache` is saved between launches (`vk_pipeline_cache.bin` in the app's
  caches or data folder), so warm starts skip driver shader compilation. It's ignored if the GPU or driver changed.
- **Debug builds stay optimised:** the game's own code is built with `-O2` even in Debug builds. Unoptimised
  builds spent about 25× more CPU per frame.
- **Profiling (desktop):**
  - `cuckoo_stack --gpu-timing [--render-scale 0.67]` logs GPU milliseconds per pass, fps and CPU time.
  - `CS_UNCAPPED=1` (debug builds) disables vsync.

### Surge mode (native-only feature)

Collect `surge::NEED` (10) disco balls in a run (they don't have to be in a row) to start a 5 s surge. The hen runs ×1.35 faster, is invulnerable,
smashes walls and barriers (+1 point per block), and the game switches to party visuals while the music speeds up (the synth's party-music pattern when no recorded
track is built in).
Every tuning value is in `src/core/Surge.h`. The logic is in `src/core/GameSurge.cpp`, the party music in
`Synth::schedulePartyStep`, and the party kick map in `BeatClock`.

- **Tests**: `tests/surge_test.cpp` (ctest `surge`) drives the real `Game` and checks the acceptance rules:
  invulnerability, smashing, ×1.35 speed, the 5 s timer plus 0.7 s grace, death afterwards, missed balls keeping
  the count, and a clean reset on restart.
- **Debug trigger** (debug builds only), which starts a surge 4 m into every run:
  - desktop: `cuckoo_stack --surge`
  - iOS: `xcrun simctl launch <device> com.cuckoostack.aerospheregames --surge`
  - Android: `adb shell setprop debug.cuckoo.surge 1`

### Text and fonts

The web build's DOM text is reproduced natively. This covers the title, game-over and HUD overlays, the popups,
the neon sign words and the today's-best gate label. Each element carries the web's CSS sizes, letter-spacing,
uppercase transforms, line-heights, max-width wrapping, text-shadows, stacking order and entrance animations
(see `src/core/GameHud.cpp`).

- **Fonts**: the same families the page loads from Google Fonts. Orbitron 900 ships renamed "Cuckoo Display",
  because Orbitron's OFL Reserved Font Name forbids reusing the name on a modified copy. Chakra Petch is used at
  500 and 700, and Noto Sans JP 700 covers the katakana and kanji on the signs. They are instanced at the CSS
  weights and subset to about 40 KB by `scripts/fetch_fonts.sh`. Sources and licences are in `assets/fonts/`.
- **Rendering**: `FontAtlas` bakes a 2048×1024 signed-distance-field atlas with stb_truetype at startup (0.87 s to
  ready on the emulator, including process start). `text.frag` draws crisp edges at any size, plus the CSS glow
  from the same distance field. HUD glyphs and quads are interleaved in DOM paint order (`RenderList::hudIsText`).

### Portability findings from bring-up

- **Base vertex**: the iOS Simulator GPU (Metal "Apple 2" family) cannot draw with a non-zero `vertexOffset`, and
  MoltenVK drops those draws, leaving a black screen. Every mesh's vertex offset is now baked into the shared 16-bit
  index buffer, so all draws use `vertexOffset = 0`.
- **Unsigned frameworks**: the simulator refuses to load an unsigned embedded `MoltenVK.framework`. Build with
  ad-hoc signing (`CODE_SIGN_IDENTITY=-`), never with `CODE_SIGNING_ALLOWED=NO`.
- **JDK**: Gradle 8.13 cannot run on JDK 25, which is the JDK bundled with current Android Studio. Use JDK 17 or 21
  for command-line builds.
- **Emulator screenshots**: with `-gpu host`, `adb screencap` returns black for Vulkan surfaces. Use
  `adb emu screenrecord screenshot <file>` instead.

## 4. Not ported yet (in priority order)

1. **Shadows**: the moonlight PCF shadow map (1024²). It would add one depth-only pass, reusing `Box` / `Egg` meshes.
2. **Searchlight and lamp cone beams**, plus **distant skyline silhouettes**. All are cheap unlit draws; they need
   one more shape mode.
3. **FXAA** and the **adaptive quality tiers** (render scale 1.5 → 0.8, bloom off, half the ambient density).
4. **MeshPhysical extras**: clearcoat and iridescence on eggs, sheen on the hen.
5. Android validation-layer packaging for debug builds.
