# Android Share Replay: status and findings (paused 2026-10-08)

Status: **built, not yet working end to end.** The game ships with it switched on, but it falls back safely. If no clip
is produced, the results screen's Share button sends the caption and store link as text, so players never see a
broken button. iOS Share Replay (ReplayKit) is separate and complete.

## Design (as implemented)

No MediaProjection, so there's no system consent dialog and no casting icon. Everything is recorded inside the engine.

| Piece | Where | What it does |
|---|---|---|
| GPU record target | `src/graphics/Renderer.{h,cpp}`: `startRecording`, `recordBlit`, `setRecordOverlay` | A second `VulkanSwapchain` on the encoder's input `ANativeWindow`. Every other frame (30 fps) the finished swapchain image, HUD included, is `vkCmdBlitImage`-scaled into it. Acquire uses timeout 0, so the game never waits on the encoder. While an overlay is set (the end card), that image is blitted instead of the screen. |
| Swapchain on any surface | `src/graphics/VulkanSwapchain.{h,cpp}` | `create(ctx, extent, surface, extraUsage)`; the recorder adds `TRANSFER_DST`. |
| Encoders + rolling window | `src/platform/android/AndroidReplay.{h,cpp}` | NDK `AMediaCodec` H.264 (720 px wide, phone aspect, 4 Mbps, 1 s I-frames, no B-frames), input surface via `AMediaCodec_createInputSurface` (API 26, looked up with dlsym). AAC-LC 128 kbps fed from the synth via `AndroidAudio::setTap` → SPSC ring → `audioLoop`. Encoded samples are kept in a ~22 s window per track. |
| End card | `src/core/EndCard.{h,cpp}` (`Game::endCard`) | CPU-rasterised with the game's SDF fonts and icon atlas on a worker thread, uploaded once, shown to the encoder for 1.8 s. Rendering is verified on desktop: `build/cuckoo_stack --endcard out.ppm`. |
| Clip export | `AndroidReplay::exportClip` | At the crash (+0.62 s): request an IDR (`request-sync`), show the card, then mux [last keyframe ≤ crash+0.6 s−15 s … cut] + [card keyframe … end] into `<cache>/replays/cuckoo-stack-replay.mp4` with `AMediaMuxer`, closing the gap between cut and card. |
| Share sheet | `CuckooActivity.replayShare` + `ShareTargetReceiver` + FileProvider (`res/xml/replay_paths.xml`) | `ACTION_SEND video/mp4` (or `text/plain` without a clip) through a chooser. The chosen app is reported as `replay_shared` analytics. |
| Safety | `AndroidReplay::frame` | Watchdog: if the encoder produces ≤2 samples in 4 s, recording is turned off for the session (`available_ = false`). Thermal Serious or worse also pauses recording. |

## What was observed (Pixel 10 Pro XL emulator, API 36, gfxstream/llvmpipe, `c2.android.avc.encoder` software)

1. The recorder swapchain is created fine: `Recorder: 720x1600, format 37`, with 19 images.
2. **First session (right after the share chooser had paused the activity):** the encoder output a single frame, then
   stopped taking buffers. Logcat filled with `vulkan: dequeueBuffer timed out` from our zero-timeout acquires. All
   swapchain buffers stayed held by the consumer.
3. **Fresh launches:** the encoder does consume frames, but only at about **1.5–2 fps** (software encoder at
   720×1600 on a software GPU). Keyframes arrive rarely as a result.
4. Export attempts, in order:
   - `0 video, 170 audio`: before the keyframe-selection fix (no keyframe fell inside the window).
   - `28 video, 870 audio` → **`export failed`**: the last run. Samples were selected, so the failure is inside the
     muxer: `AMediaMuxer_start`, `writeSampleData` or `stop` returned an error. Which call is unknown, because the
     return codes aren't logged individually yet.

## Next steps (in order)

1. **Test on a real Android phone** (hardware encoder, real GPU). The emulator's software encoder plus gfxstream
   buffers is the most likely cause of items 2–3. Check `adb logcat -s CuckooStack` for the `Replay:` lines.
2. Log each muxer call's `media_status_t` in `exportClip` (start, every write failure, stop) to find item 4's cause.
   Suspects:
   - Audio samples written with pts before the first video sample (they're skipped, but check the interleave loop).
   - A `vfmt` borrowed from the track that lacks `csd-0/csd-1`.
   - Non-monotonic pts after the gap remap (card pts − gap could fall below the last game-frame pts if the card's
     IDR came very soon after the cut).
   - `AMediaMuxer_stop` failing because a track got no samples: guard `at >= 0` only when audio samples remain after
     the remap.
3. If a real device also stalls after the app is paused, restart the session on `APP_CMD_RESUME` (today it restarts
   only after `TERM_WINDOW`) and/or try `VK_PRESENT_MODE_MAILBOX_KHR` for the recorder chain.
4. Consider recording at 540 px wide on low-end devices (and when `Quality.h` lowers the render scale).
5. Once a clip exports, verify that the MP4 plays with sound, is about 15 s + 1.8 s card, has no frame of the Continue
   panel or an ad, and shares to Instagram, TikTok and Messages through the chooser.

## How to reproduce

```bash
cd platforms/android && JAVA_HOME=$(/usr/libexec/java_home -v 21) ./gradlew installStagingDebug
```

```bash
adb logcat -c && adb logcat -s CuckooStack
```

In the game:
1. 13+ age group.
2. Play one run.
3. On the results screen, tap **Share**. This turns replays on; dismiss the chooser.
4. Play another run and let the hen crash.

Expect `Replay: window … / clip saved`. On a working device the results screen's Share button turns into **Replay**.
