// The title-screen hen's idle loop (HenIdle.h): three moves with rests between them, each easing out to the
// standing pose, the double jump's knee bends, and Game playing it only on the title screen.
#include "TestUtil.h"
#include "core/HenIdle.h"

#include <cmath>

using namespace cs;
using namespace test;

namespace {

float magnitude(const idle::Pose& p) {
    float m = std::abs(p.lean) + std::abs(p.bodyY) + std::abs(p.stretch) + std::abs(p.air) + std::abs(p.tail);
    for (int k = 0; k < 2; ++k) m += std::abs(p.hip[k]) + std::abs(p.knee[k]) + std::abs(p.wing[k]);
    return m;
}

void schedule() {
    int moves = 0;
    bool restBetween = true, seen[4] = {};
    const size_t n = std::size(idle::kLoop);
    for (size_t i = 0; i < n; ++i) {
        const idle::Step& s = idle::kLoop[i];
        if (s.move == idle::Move::Rest) continue;
        moves++;
        seen[int(s.move)] = true;
        // the step before and after each move (wrapping round the loop) is a rest of at least 2 s
        const idle::Step& before = idle::kLoop[(i + n - 1) % n];
        const idle::Step& after = idle::kLoop[(i + 1) % n];
        restBetween = restBetween && before.move == idle::Move::Rest && after.move == idle::Move::Rest &&
                      before.length >= 2.f && after.length >= 2.f;
    }
    check(moves == 3 && seen[int(idle::Move::Jog)] && seen[int(idle::Move::Stretch)] && seen[int(idle::Move::Jumps)],
          "three moves: jog in place, stretch, jumps");
    check(restBetween, "a rest of 2 s or more between every move, including round the loop");
    check(idle::kLoop[0].move == idle::Move::Rest, "the loop opens with a rest");
    const float L = idle::loopLength();
    bool loops = true;
    for (float t = 0; t < L; t += 0.37f) loops = loops && idle::at(t).move == idle::at(t + L).move && idle::at(t).move == idle::at(t + 2 * L).move;
    check(loops, "it loops");
    check(idle::at(-1.f).move == idle::Move::Rest, "before the start: resting");
}

void poses() {
    bool restStill = true, moving[4] = {}, smooth = true, edges = true;
    const float L = idle::loopLength();
    idle::Pose prev = idle::pose(0);
    for (float t = 0; t < 2 * L; t += 1.f / 120.f) {
        const idle::Pose p = idle::pose(t);
        const idle::Move m = idle::at(t).move;
        if (m == idle::Move::Rest) restStill = restStill && magnitude(p) == 0.f;
        if (magnitude(p) > 0.5f) moving[int(m)] = true;
        // no pops: nothing jumps more than a frame's worth at 120 fps
        for (int k = 0; k < 2; ++k)
            smooth = smooth && std::abs(p.hip[k] - prev.hip[k]) < 0.15f && std::abs(p.knee[k] - prev.knee[k]) < 0.2f &&
                     std::abs(p.wing[k] - prev.wing[k]) < 0.15f;
        smooth = smooth && std::abs(p.air - prev.air) < 0.05f && std::abs(idle::hipDrop(p) - idle::hipDrop(prev)) < 0.02f;
        prev = p;
    }
    // each move starts and ends at the standing pose, so the rests around it don't snap
    float t0 = 0;
    for (const idle::Step& s : idle::kLoop) {
        if (s.move != idle::Move::Rest)
            edges = edges && magnitude(idle::pose(t0 + 0.001f)) < 0.05f && magnitude(idle::pose(t0 + s.length - 0.001f)) < 0.05f;
        t0 += s.length;
    }
    check(restStill, "rests: she just stands (no offsets)");
    check(moving[int(idle::Move::Jog)] && moving[int(idle::Move::Stretch)] && moving[int(idle::Move::Jumps)], "every move visibly moves her");
    check(edges, "each move eases in from and out to the standing pose");
    check(smooth, "no pops frame to frame");

    idle::Pose standing;
    check(std::abs(idle::hipDrop(standing)) < 1e-6f, "straight legs: hips at standing height");
}

void jog() {
    int swaps = 0;
    bool lastFwd = false, oneLift = true, grounded = true;
    for (float t = 0.4f; t < 2.8f; t += 1.f / 120.f) {
        const idle::Pose p = idle::jog(t, 3.2f);
        const bool fwd = p.hip[0] > 0;
        if (t > 0.4f && fwd != lastFwd) swaps++;
        lastFwd = fwd;
        oneLift = oneLift && !(p.knee[0] < -0.2f && p.knee[1] < -0.2f); // never both feet up
        grounded = grounded && p.air == 0.f;
    }
    check(swaps >= 6, "jog: the legs alternate, several steps");
    check(oneLift && grounded, "jog: in place, one foot lifted at a time");
}

void jogSpeed() {
    // the average time between forward swings of one leg, mid-jog
    float first = -1, last = -1;
    int swings = 0;
    bool prev = false;
    for (float t = 0.5f; t < 2.7f; t += 1.f / 960.f) {
        const bool fwd = idle::jog(t, 3.2f).hip[0] > 0;
        if (fwd && !prev && t > 0.5f) { if (first < 0) first = t; last = t; swings++; }
        prev = fwd;
    }
    const float rate = swings > 1 ? (swings - 1) / (last - first) : 0.f;
    check(rate > 2.2f && rate < 2.6f, "jog: a quick pace, about 2.4 strides a second");
}

// The legs have a knee: hip (legs) -> knee -> foot, and the bent leg keeps the foot on the ground
glm::mat4 worldOf(const Hen& h, int n) {
    glm::mat4 m(1.f);
    for (int i = n; i >= 0; i = h.nodes[size_t(i)].parent) {
        const Hen::Node& nd = h.nodes[size_t(i)];
        m = compose(nd.pos, nd.rot, nd.scale) * m;
    }
    return m;
}

void legs() {
    Hen h;
    bool chain = true;
    for (int k = 0; k < 2; ++k)
        chain = chain && h.nodes[size_t(h.knees[k])].parent == h.legs[k] && h.nodes[size_t(h.feet[k])].parent == h.knees[k] &&
                h.nodes[size_t(h.legs[k])].parent == h.root;
    check(chain, "legs: hip -> knee -> foot");
    const float footY = glm::vec3(worldOf(h, h.feet[0])[3]).y;
    check(std::abs(footY - 0.02f * 1.1f) < 1e-4f, "legs: standing, the foot is at the ground (toes 0.02 below the hip line)");
    // bend the knee by hand with the pose maths: hips lower by hipDrop and the foot stays put, toes level
    idle::Pose p;
    p.hip[0] = p.hip[1] = -0.9f;
    p.knee[0] = p.knee[1] = 1.8f;
    for (int k = 0; k < 2; ++k) {
        h.nodes[size_t(h.legs[k])].rot.z = p.hip[k];
        h.nodes[size_t(h.knees[k])].rot.z = p.knee[k];
        h.nodes[size_t(h.feet[k])].rot.z = -(p.hip[k] + p.knee[k]);
    }
    h.nodes[size_t(h.root)].pos.y = -idle::hipDrop(p) * 1.1f;
    const glm::mat4 foot = worldOf(h, h.feet[0]);
    const glm::vec3 kneeP = glm::vec3(worldOf(h, h.knees[0])[3]), hipP = glm::vec3(worldOf(h, h.legs[0])[3]);
    check(std::abs(foot[3].y - 0.02f * 1.1f) < 1e-4f, "legs: crouched, the foot stays on the ground");
    check(std::abs(glm::vec3(foot[0]).y) < 1e-4f, "legs: crouched, the toes stay flat");
    check(kneeP.x < hipP.x - 0.05f, "legs: the joint points backward, like a bird's");
}

void stretch() {
    const idle::Pose up = idle::stretch(1.0f, 3.0f), side = idle::stretch(2.3f, 3.0f);
    check(up.wing[0] > 1.f && up.wing[1] > 1.f && up.stretch > 0.05f && up.air > 0.f, "stretch: both wings high, drawn tall, on tiptoes");
    check(side.wing[0] > 0.6f && side.wing[1] < 0.3f && side.hip[0] < -0.5f, "stretch: then one wing and the same-side leg reach back");
}

void jumps() {
    // two separate airborne spells, each loaded by a deep bend and landed into one
    int takeoffs = 0;
    bool wasAir = false, deepBefore[2] = {}, softLanding[2] = {}, birdKnee = true, feetDown = true;
    float maxBend = 0, peak = 0;
    for (float t = 0; t < 2.4f; t += 1.f / 240.f) {
        const idle::Pose p = idle::jumps(t, 2.4f);
        const bool air = p.air > 0.f;
        if (air && !wasAir) takeoffs++;
        // before each takeoff (the load) and after each landing, on the ground
        if (!air && p.knee[0] > 1.6f && takeoffs < 2) deepBefore[takeoffs] = true;
        if (!air && takeoffs > 0 && p.knee[0] > 0.8f) softLanding[takeoffs - 1] = true;
        maxBend = std::max(maxBend, p.knee[0]);
        peak = std::max(peak, p.air);
        if (!air && p.knee[0] > 0.1f) {
            birdKnee = birdKnee && p.hip[0] < 0 && p.knee[0] > 0; // thigh back, shin forward: the joint points back
            feetDown = feetDown && idle::hipDrop(p) > 0.f;
        }
        wasAir = air;
    }
    check(takeoffs == 2, "jumps: she leaves the ground exactly twice");
    check(deepBefore[0] && deepBefore[1], "jumps: a deep knee bend loads each jump");
    check(softLanding[0] && softLanding[1], "jumps: each landing sinks back into the knees");
    check(maxBend > 1.9f, "jumps: the knees bend a lot (the emphasis)");
    check(birdKnee, "jumps: a bird's bend, the joint pointing back");
    check(feetDown, "jumps: crouching lowers her hips; the feet stay on the ground");
    check(peak > 0.3f && peak < 0.6f, "jumps: a good hop, not a launch");
    check(idle::hipDrop(idle::jumps(0.31f, 2.4f)) > 0.08f, "jumps: the full crouch drops her visibly");
}

// Game: in every grounded moment of the loop the lower foot touches the ground (bent knees lower the hen; they
// don't sink her feet or float her)
void grounded() {
    FakeStorage st = FakeStorage::ready();
    FakeClock clock;
    FakeAds ads;
    GameServices svc; svc.storage = &st; svc.clock = &clock; svc.ads = &ads;
    Game g(svc);
    const Hen& h = g.hen();
    step(g, 0.5f);
    bool onGround = true, flew = false;
    for (int i = 0; i < int(idle::loopLength() * 60) + 60; ++i) {
        g.update(1.0 / 60.0);
        const float low = std::min(glm::vec3(worldOf(h, h.feet[0])[3]).y, glm::vec3(worldOf(h, h.feet[1])[3]).y);
        if (g.henHeight() > 0.005f) { flew = flew || low > 0.1f; continue; } // lifted: on tiptoes or jumping
        onGround = onGround && std::abs(low - 0.022f) < 0.02f;
    }
    check(onGround, "title: grounded, the lower foot is on the street (no sinking, no floating; the push-off keeps it down)");
    check(flew, "title: in the jumps, the feet leave the ground");
}

// Game: the loop plays on the title screen only, and a run starts from the plain running pose
void game() {
    FakeStorage st = FakeStorage::ready();
    FakeClock clock;
    FakeAds ads;
    GameServices svc; svc.storage = &st; svc.clock = &clock; svc.ads = &ads;
    Game g(svc);
    const Hen& h = g.hen();
    step(g, 0.5f);
    check(g.state() == Game::State::Title, "title screen");
    const float ground = g.henHeight();
    float high = ground, low = ground, knee = 0, wing = 0, hipSwing = 0;
    for (int i = 0; i < int(idle::loopLength() * 60) + 60; ++i) {
        g.update(1.0 / 60.0);
        high = std::max(high, g.henHeight());
        low = std::min(low, g.henHeight());
        knee = std::max(knee, h.nodes[size_t(h.knees[0])].rot.z);
        wing = std::max(wing, std::abs(h.nodes[size_t(h.wings[0])].rot.x));
        hipSwing = std::max(hipSwing, std::abs(h.nodes[size_t(h.legs[0])].rot.z));
    }
    check(high > ground + 0.3f, "title: she jumps");
    check(low < ground - 0.08f, "title: she crouches into her knees");
    check(knee > 1.9f && hipSwing > 0.4f, "title: the knees bend and the legs swing");
    check(wing > 1.f, "title: the wings lift in the stretch");
    g.press();
    step(g, 0.2f);
    check(g.state() == Game::State::Play, "a run starts");
    bool kneesStraight = true;
    for (int i = 0; i < 60; ++i) {
        g.update(1.0 / 60.0);
        for (int k = 0; k < 2; ++k)
            kneesStraight = kneesStraight && h.nodes[size_t(h.knees[k])].rot.z == 0.f && h.nodes[size_t(h.feet[k])].rot.z == 0.f;
    }
    check(kneesStraight, "in a run: no idle pose left on the legs");
}

} // namespace

int main() {
    test::useFixtures(); // (the levels and gauntlets these tests are written round: tests/fixtures)
    schedule();
    poses();
    jog();
    jogSpeed();
    legs();
    stretch();
    jumps();
    game();
    grounded();
    return test::finish("HenIdle");
}
