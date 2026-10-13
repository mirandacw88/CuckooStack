// Share Replay: choosing the clip out of a recorder's rolling window of encoded frames (Android's export; pure logic,
// so ReplayClip_test.cpp drives it on desktop). Times are microseconds on the recorder's clock.
//
// The window holds the run as it was captured, with holes: capture pauses (the Continue panel, menus) and revived
// crashes are listed as `pauses` and cut out, so the clip plays continuously. The clip is the last `clipUs` of play
// before `cut` (the clip request), starting on a keyframe and never before the run's start; the end card's frames
// (recorded live from `card`, starting on a keyframe) follow the last game frame without a gap.
#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace cs::replay {

using Pause = std::pair<int64_t, int64_t>; // [from, to)

inline bool inPause(const std::vector<Pause>& pauses, int64_t t) {
    for (const auto& [a, b] : pauses) if (t >= a && t < b) return true;
    return false;
}

// time actually played: `t` minus the paused time before it
inline int64_t played(const std::vector<Pause>& pauses, int64_t t) {
    int64_t p = t;
    for (const auto& [a, b] : pauses) p -= std::clamp<int64_t>(t, a, b) - a;
    return p;
}

struct Frame { int64_t pts; bool key; };

struct ClipPlan {
    std::vector<size_t> video, audio;      // indices into the inputs, in order
    std::vector<int64_t> videoOut, audioOut; // their timestamps in the clip (from 0)
    int64_t gameLength = 0;                // the last game frame's timestamp (the card follows it)
};

struct ClipRequest {
    int64_t runStart = 0;   // the run's start: the clip never reaches before it
    int64_t cut = 0;        // the clip request: game frames before it
    int64_t card = INT64_MAX; // the end card went on screen (its frames from the first keyframe after this)
    int64_t to = INT64_MAX;   // nothing after this
    int64_t clipUs = 15'000'000;
    int64_t frameUs = 1'000'000 / 30;
};

inline ClipPlan planClip(const std::vector<Frame>& video, const std::vector<int64_t>& audioPts, const std::vector<Pause>& pauses,
                         const ClipRequest& r) {
    ClipPlan plan;
    // the start: the last keyframe of this run at or before (cut - clipUs) of play, or the run's first one
    const int64_t from = played(pauses, r.cut) - r.clipUs;
    int64_t startKey = INT64_MIN;
    for (const Frame& f : video) {
        if (f.pts < r.runStart || f.pts >= r.cut || !f.key || inPause(pauses, f.pts)) continue;
        if (played(pauses, f.pts) <= from || startKey == INT64_MIN) startKey = f.pts;
        if (played(pauses, f.pts) > from) break;
    }
    if (startKey == INT64_MIN) return plan;
    const int64_t t0 = played(pauses, startKey);
    bool inCard = false, needKey = false;
    int64_t cardT0 = 0;
    for (size_t i = 0; i < video.size(); ++i) {
        const Frame& f = video[i];
        if (f.pts > r.to) break;
        if (f.pts < startKey) continue;
        if (f.pts < r.cut) {
            if (inPause(pauses, f.pts)) { needKey = true; continue; } // a cut: carry on from the next keyframe
            if (needKey && !f.key) continue;
            needKey = false;
            plan.video.push_back(i);
            plan.videoOut.push_back(plan.gameLength = played(pauses, f.pts) - t0);
            continue;
        }
        if (!inCard && !(f.pts >= r.card && f.key)) continue;
        if (!inCard) { inCard = true; cardT0 = f.pts - (plan.gameLength + r.frameUs); } // the card follows the last game frame
        plan.video.push_back(i);
        plan.videoOut.push_back(f.pts - cardT0);
    }
    for (size_t i = 0; i < audioPts.size(); ++i) {
        const int64_t t = audioPts[i];
        if (t >= startKey && t < r.cut && !inPause(pauses, t)) { plan.audio.push_back(i); plan.audioOut.push_back(played(pauses, t) - t0); }
    }
    return plan;
}

} // namespace cs::replay
