// The clip planner (ReplayClip.h, Android's export): pauses and revive cuts, the run's start, keyframes, the card.
#include "TestUtil.h"
#include "core/ReplayClip.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace cs;
using namespace test;

namespace {

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

} // namespace

int main() {
    clipPlanner();
    return finish("ReplayClip");
}
