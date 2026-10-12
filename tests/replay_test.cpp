// Share Replay, everything that runs off-device:
//  - the game's side (Game + IReplay): capture only during play, the crash tail and the finish celebration, when the
//    clip is requested, revived crashes cut, the 3 s minimum, the end card label, Share Replay sharing a ready video
//  - the end card renderer (EndCard.cpp, Android's card)
//  - the clip planner (ReplayClip.h, Android's export): pauses and revive cuts, the run's start, keyframes, the card
//  - the game-audio tap (ReplayAudioTap.h, iOS): gating, mark / rewind, the WAV it writes
// The iOS video recorder (VideoToolbox) has its own test, replay_recorder_test.mm (macOS only).
#include "TestUtil.h"
#include "core/ReplayClip.h"
#include "platform/ios/ReplayAudioTap.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>

using namespace cs;
using namespace test;

namespace {

struct Env {
    FakeStorage st = settled();
    // a player who has already answered the replay / notification offers, so no panel covers the results
    static FakeStorage settled() {
        FakeStorage s = FakeStorage::ready();
        s.kv["cluckstack-profile"] = "aud=2;f_replay_asked=1;f_notif_asked=1;";
        return s;
    }
    FakeClock clock;
    FakeAds ads;
    FakeReplay replay;
    GameServices svc() { GameServices s; s.storage = &st; s.clock = &clock; s.ads = &ads; s.replay = &replay; return s; }
};

// a run long enough for a clip: the surge makes the hen invulnerable for its first seconds
void longRun(Game& g) {
    g.press();
    step(g, 0.1f);
    g.debugStartSurge();
    for (int i = 0; i < 60 * 60 && g.state() == Game::State::Play; ++i) g.update(1.0 / 60.0);
}
// after a crash: let the tail play, then turn down any Continue
void endRun(Game& g) {
    step(g, Game::kCrashTail + 0.2f);
    if (g.screen() == Game::Screen::Continue) { g.tapButton("No thanks"); step(g, 0.1f); }
}

// ---------------------------------------------------------------- game side

void gameSide() {
    {   // ---- a run and its crash
        Env env;
        env.ads.state = RewardedState::Unavailable;
        Game g(env.svc());
        step(g, 0.5f);
        check(!env.replay.capturing, "title: not recording");
        longRun(g);
        check(g.state() == Game::State::Dead && g.runSeconds() > Game::kMinReplaySeconds, "a long run, then a crash");
        check(env.replay.runStarts == 1 && env.replay.crashes == 1, "the run's start and its crash reach the recorder");
        check(env.replay.capturing, "the crash itself is recorded");
        step(g, Game::kCrashTail - 0.3f);
        check(env.replay.capturing && env.replay.saves == 0, "...for its whole tail, before any clip is requested");
        check(g.screen() != Game::Screen::Continue, "no panel during the crash's tail");
        endRun(g);
        check(!env.replay.capturing, "capture stops once the tail has played (no panel gets recorded)");
        check(env.replay.saves == 1, "one clip, requested once the run is over");
        check(env.replay.last.label == "LEVEL 1" && env.replay.last.distance > 0, "the end card: the level and the distance");
        check(env.replay.last.newBest, "a first run is a new best");
        check(!g.replayTooShort(), "a long run isn't too short");

        // the results: Share Replay only with a ready video, and it shares the video only
        step(g, 1.f);
        check(!g.tapButton("Share replay"), "no button while the clip isn't ready");
        env.replay.st = ReplayState::Exporting;
        step(g, 0.1f);
        check(!g.tapButton("Share replay"), "\"Saving\" can't be tapped");
        env.replay.st = ReplayState::Ready;
        step(g, 0.1f);
        check(g.tapButton("Share replay") && env.replay.shares == 1, "a ready clip: Share Replay shares it");
        env.replay.st = ReplayState::Failed;
        step(g, 0.1f);
        check(!g.tapButton("Share replay"), "a failed clip: no button");

        // the next run starts a new clip
        env.replay.st = ReplayState::Recording;
        g.press();
        step(g, 0.2f);
        check(env.replay.runStarts == 2 && env.replay.capturing, "the next run: a fresh clip, recording again");
    }
    {   // ---- too short for a replay
        Env env;
        env.ads.state = RewardedState::Unavailable;
        Game g(env.svc());
        test::dieQuickly(g);
        check(g.runSeconds() < Game::kMinReplaySeconds, "a run under the minimum");
        endRun(g);
        check(env.replay.saves == 0 && g.replayTooShort(), "no clip for it; the results say it was too quick");
        step(g, 1.f);
        check(!g.tapButton("Share replay"), "...and there's no Share Replay");
        g.press();
        step(g, 0.1f);
        check(!g.replayTooShort(), "the next run starts fresh");
    }
    {   // ---- a revive cuts the crash out, and the clip is the whole run's
        Env env; // rewarded ads available: Continue offered
        Game g(env.svc());
        longRun(g);
        step(g, Game::kCrashTail + 0.2f);
        check(g.screen() == Game::Screen::Continue && !env.replay.capturing, "Continue panel: not recorded");
        check(env.replay.saves == 0, "a crash that can still be revived gets no clip of its own");
        g.tapButton("Watch ad");
        step(g, 0.2f);
        check(g.state() == Game::State::Play && env.replay.revives == 1, "revived: the crash is cut from the clip");
        check(env.replay.capturing && env.replay.runStarts == 1, "recording carries on in the same run");
        for (int i = 0; i < 60 * 60 && g.state() == Game::State::Play; ++i) g.update(1.0 / 60.0);
        check(env.replay.crashes == 2, "the second crash is marked too");
        endRun(g);
        if (g.screen() == Game::Screen::Continue) { g.tapButton("No thanks"); step(g, 0.1f); }
        check(env.replay.saves == 1 && env.replay.revives == 1, "one clip for the run, ending on its final crash");
        check(!env.replay.capturing, "capture off on the results");
    }
    {   // ---- a level clear: the celebration is recorded, then the clip
        Env env;
        env.ads.state = RewardedState::Unavailable;
        Game g(env.svc());
        g.debugNearFinish();
        g.press();
        g.debugStartSurge();
        int frames = 0;
        bool celebrating = false;
        for (; frames < 60 * 20 && g.state() == Game::State::Play; ++frames) {
            g.update(1.0 / 60.0);
            celebrating = celebrating || (env.replay.capturing && g.state() == Game::State::Play);
        }
        check(g.state() == Game::State::Dead && g.lastRunCleared(), "the finish line clears the level");
        check(celebrating && env.replay.crashes == 0, "the celebration is recorded (no crash)");
        g.update(1.0 / 60.0);
        check(env.replay.saves == 1 && env.replay.last.label == "LEVEL 1 CLEARED", "the clip right after it: \"LEVEL 1 CLEARED\"");
        check(!env.replay.capturing, "the Level Cleared screen isn't recorded");
    }
}

// ---------------------------------------------------------------- end card

void endCard() {
    Env env;
    Game g(env.svc());
    ReplayMeta m;
    m.distance = 215; m.label = "LEVEL 3";
    const std::vector<uint8_t> a = g.endCard(360, 640, m);
    check(a.size() == size_t(360) * 640 * 4, "end card: RGBA at the requested size");
    bool opaque = true, varied = false;
    for (size_t i = 0; i < a.size(); i += 4) {
        opaque = opaque && a[i + 3] == 255;
        varied = varied || a[i] != a[0] || a[i + 1] != a[1] || a[i + 2] != a[2];
    }
    check(opaque && varied, "end card: opaque, with something drawn on it");
    m.newBest = true;
    check(g.endCard(360, 640, m) != a, "end card: a new best has its own headline");
    m.newBest = false; m.label = "LEVEL 3 CLEARED";
    check(g.endCard(360, 640, m) != a, "end card: the label is drawn");
}

// ---------------------------------------------------------------- clip planner (Android's export)

void clipPlanner() {
    using namespace replay;
    constexpr int64_t S = 1'000'000, F = S / 30;
    // 20 s of frames at 30 fps, a keyframe every second; then the end card's frames from 21 s
    std::vector<Frame> video;
    for (int i = 0; i < 20 * 30; ++i) video.push_back({int64_t(i) * S / 30, i % 30 == 0});      // keyframes on the second
    for (int i = 0; i < 2 * 30; ++i) video.push_back({21 * S + int64_t(i) * S / 30, i % 30 == 0}); // the card
    std::vector<int64_t> audio;
    for (int64_t t = 0; t < 20 * S; t += 21'333) audio.push_back(t);
    ClipRequest r;
    r.cut = 20 * S; r.card = 21 * S - S / 10; r.to = 23 * S;

    {   // no pauses: the last 15 s, from a keyframe, then the card straight after
        const ClipPlan p = planClip(video, audio, {}, r);
        check(!p.video.empty() && video[p.video.front()].pts == 5 * S && video[p.video.front()].key, "clip: starts on the keyframe 15 s before the cut");
        check(p.videoOut.front() == 0, "clip: its timeline starts at 0");
        const size_t firstCard = size_t(std::find_if(p.video.begin(), p.video.end(), [&](size_t i) { return video[i].pts >= 21 * S; }) - p.video.begin());
        check(firstCard < p.video.size() && video[p.video[firstCard]].key, "clip: the card from its keyframe");
        check(p.videoOut[firstCard] == p.gameLength + F, "clip: the card follows the last game frame, no gap");
        check(p.gameLength > 15 * S - 2 * F && p.gameLength < 15 * S, "clip: 15 s of play");
        bool rising = true;
        for (size_t i = 1; i < p.videoOut.size(); ++i) rising = rising && p.videoOut[i] > p.videoOut[i - 1];
        check(rising, "clip: timestamps always rise");
        check(!p.audio.empty() && audio[p.audio.front()] >= 5 * S && p.audioOut.front() >= 0, "clip: audio from the same start");
        check(audio[p.audio.back()] < 20 * S, "clip: no audio past the cut (the card is silent)");
    }
    {   // the run started 12 s in: never earlier
        ClipRequest rr = r;
        rr.runStart = 12 * S - F / 2;
        const ClipPlan p = planClip(video, audio, {}, rr);
        check(video[p.video.front()].pts == 12 * S, "clip: starts at the run's start, not in the run before");
        check(p.gameLength < 8 * S + F, "clip: only this run's play");
    }
    {   // a capture pause (a menu): cut out, the timeline closes over it
        const std::vector<Pause> pauses = {{8 * S, 10 * S}};
        const ClipPlan p = planClip(video, audio, pauses, r);
        bool none = true;
        for (size_t i : p.video) none = none && !(video[i].pts >= 8 * S && video[i].pts < 23 * S && video[i].pts < 10 * S);
        check(none, "pause: no frames from it");
        check(video[p.video.front()].pts == 3 * S, "pause: the clip reaches back further for 15 s of play");
        int64_t maxGap = 0;
        for (size_t i = 1; i < p.videoOut.size(); ++i) maxGap = std::max(maxGap, p.videoOut[i] - p.videoOut[i - 1]);
        check(maxGap <= F + 1, "pause: no gap in the clip's timeline");
        bool audioOk = true;
        for (size_t i : p.audio) audioOk = audioOk && !(audio[i] >= 8 * S && audio[i] < 10 * S);
        check(audioOk, "pause: no audio from it");
    }
    {   // a revived crash, cut mid-second: the clip carries on from the next keyframe
        const std::vector<Pause> pauses = {{8 * S + 400'000, 10 * S + 300'000}};
        const ClipPlan p = planClip(video, audio, pauses, r);
        size_t after = 0;
        while (after < p.video.size() && video[p.video[after]].pts < 10 * S + 300'000) ++after;
        check(after < p.video.size() && video[p.video[after]].pts == 11 * S, "revive cut: resumes on a keyframe");
        check(video[p.video[after - 1]].pts < 8 * S + 400'000, "revive cut: nothing from the cut crash");
    }
    {   // too little in the window: nothing, rather than a broken clip
        const ClipPlan p = planClip({}, {}, {}, r);
        check(p.video.empty() && p.audio.empty(), "an empty window: an empty plan");
    }
    {   // played(): time minus the pauses before it
        const std::vector<Pause> pauses = {{2 * S, 3 * S}, {5 * S, 7 * S}};
        check(played(pauses, 1 * S) == 1 * S && played(pauses, 4 * S) == 3 * S && played(pauses, 8 * S) == 5 * S, "played(): pauses taken out");
        check(played(pauses, 6 * S) == 4 * S && inPause(pauses, 6 * S) && !inPause(pauses, 7 * S), "played() / inPause(): inside a pause");
    }
}

// ---------------------------------------------------------------- game-audio tap (iOS)

struct Wav { uint32_t rate = 0; uint16_t channels = 0, bits = 0; std::vector<int16_t> pcm; };
Wav readWav(const std::string& path) {
    Wav w;
    std::ifstream f(path, std::ios::binary);
    std::vector<char> b((std::istreambuf_iterator<char>(f)), {});
    if (b.size() < 44 || std::string(b.data(), 4) != "RIFF" || std::string(b.data() + 8, 4) != "WAVE") return w;
    std::memcpy(&w.channels, &b[22], 2); std::memcpy(&w.rate, &b[24], 4); std::memcpy(&w.bits, &b[34], 2);
    uint32_t data = 0;
    std::memcpy(&data, &b[40], 4);
    w.pcm.resize(std::min<size_t>(data, b.size() - 44) / 2);
    std::memcpy(w.pcm.data(), &b[44], w.pcm.size() * 2);
    return w;
}
void pushRamp(ReplayAudioTap& tap, int frames, float value) {
    std::vector<float> buf(512 * 2, value);
    for (int done = 0; done < frames; done += 512) tap.push(buf.data(), std::min(512, frames - done));
}

void audioTap() {
    const std::string path = "replay_test_audio.wav";
    auto tap = std::make_unique<ReplayAudioTap>(); // (~4 MB once enabled)
    tap->setRate(48000);
    pushRamp(*tap, 4800, 0.5f);
    check(!tap->writeWav(path, 1.0), "audio tap: nothing kept before replays are on");
    tap->enable();
    pushRamp(*tap, 4800, 0.5f);
    check(!tap->writeWav(path, 1.0), "audio tap: nothing kept while capture is paused");
    tap->setCapturing(true);
    pushRamp(*tap, 48000, 0.5f);
    check(tap->writeWav(path, 10.0), "audio tap: writes a WAV");
    Wav w = readWav(path);
    check(w.rate == 48000 && w.channels == 2 && w.bits == 16, "audio tap: 48 kHz, stereo, 16-bit");
    check(w.pcm.size() == size_t(48000) * 2, "audio tap: exactly what was captured (1 s), not more");
    check(!w.pcm.empty() && w.pcm.front() == int16_t(0.5f * 32767.f), "audio tap: the samples themselves");
    tap->writeWav(path, 0.25);
    check(readWav(path).pcm.size() == size_t(12000) * 2, "audio tap: the last N seconds only");
    // a revive: what was kept since the crash goes
    tap->mark();
    pushRamp(*tap, 24000, -0.25f);
    tap->setCapturing(false);
    tap->rewindToMark();
    tap->setCapturing(true);
    pushRamp(*tap, 4800, 2.f); // clipped
    tap->writeWav(path, 10.0);
    w = readWav(path);
    check(w.pcm.size() == size_t(48000 + 4800) * 2, "audio tap: a revive drops what came after the crash");
    bool noCrashAudio = true;
    for (int16_t v : w.pcm) noCrashAudio = noCrashAudio && v != int16_t(-0.25f * 32767.f);
    check(noCrashAudio, "audio tap: none of the cut crash's audio is left");
    check(w.pcm.back() == 32767, "audio tap: loud samples are clipped, not wrapped");
    // it's a rolling window: older audio falls out
    pushRamp(*tap, 48000 * 25, 0.1f);
    tap->writeWav(path, 60.0);
    w = readWav(path);
    check(w.pcm.size() / 2 < size_t(48000) * 20 && w.pcm.size() / 2 > size_t(48000) * 19, "audio tap: keeps about the last 20 s");
    std::remove(path.c_str());
}

} // namespace

int main() {
    gameSide();
    endCard();
    clipPlanner();
    audioTap();
    return test::finish("replay");
}
