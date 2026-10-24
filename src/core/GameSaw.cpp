// Saw blades (levels 11 on; Level.h places them): the stack crossing one, and how it all looks.
// A blade stands half out of a slot in the surface, spinning clockwise as seen (its top turning the hen's way), sparks
// jetting up where it comes out of the slot; a whine plays as one comes up ahead, and it glows red, brighter as she
// nears, when she'd reach it with no egg under her. The stack's bottom egg meets it first and the blade saws through
// it as it slides over: a bite (a hit-pause, sparks spraying out of the cut, the egg shuddering) and, once it's across,
// the egg falls apart lengthwise in two halves, one tumbling toward the camera and one away, each showing its cut face
// (white, a gold yolk, ringed in its neon), the yolk spilling out between them. The stack settles one egg down and the
// spent blade sinks into its slot. With no egg under her, the hen herself meets the blade: a crash. OVERDRIVE (and its
// grace) smashes blades off their slots instead.
#include "Game.h"

#include <algorithm>
#include <cmath>

namespace cs {

namespace {
constexpr float kSinkTime = 0.3f;  // a spent blade sinking into its slot, s
// in a maze, the cut is done this far short of the blade's hub (her front edge, kHb 0.22 on, still in the blade's cell:
// a cell is U 0.6 wide, the hub at its middle)
constexpr double kMazeCutDone = 0.1;
const float kR = float(Level::kSawR);

// a point on a blade spinning clockwise (seen from +z): its velocity, at `speed` on the rim
glm::vec3 rimVelocity(glm::vec2 fromHub, float speed) { return glm::vec3(fromHub.y, -fromHub.x, 0.f) / kR * speed; }
} // namespace

// Red-hot: a blade has to read as danger at a glance, against every zone's walls (Neon Egypt's gold and cyan stone
// swallowed a gold blade; Neon Japan's are pink). Per zone, so a later zone can pick its own.
glm::vec3 Game::sawColor() const {
    switch (world_.zone()) {
    default: return hexColor("#ff4426");
    }
}

glm::mat4 Game::stackEggMatrix(size_t i) const {
    const Egg& e = eggs_[i];
    const float sgn = i % 2 ? 1.f : -1.f;
    const float roll = sgn * (float(x_) / 0.3f) + e.sway;
    const glm::vec3 s{e.scale.x * 1.12f, e.scale.z * 1.12f, e.scale.y * 1.3f}; // x, height, long axis
    glm::vec3 p = e.pos;
    if (e.cut > 0) p += glm::vec3(std::sin(sawSpin_ * 9.7f) * 0.03f, std::cos(sawSpin_ * 12.3f) * 0.022f, 0.f); // shuddering on the blade
    return compose(p, {0, sgn * 1.22f, 0}, glm::vec3(1.f)) * compose({}, {0, 0, roll}, glm::vec3(1.f)) *
           compose({}, {kPi / 2, 0, 0}, glm::vec3(1.f)) * compose({}, {s.x, s.z, s.y});
}

bool Game::debugSaw(double ahead, bool cube) {
    if (state_ != State::Play) return false;
    Saw s;
    if (!cube) {
        s.x = x_ + ahead;
        s.y = level_.heightAt(s.x);
    } else { // the next wall she'd climb onto, long enough to hold one
        bool found = false;
        for (const Segment& sg : level_.segs)
            if (sg.x0 > x_ + 1 && sg.x0 < x_ + 40 && sg.h > level_.heightAt(sg.x0 - 0.01) && !sg.ceil && sg.x1 - sg.x0 > 2.5 * U + Level::kSawReach) {
                s.x = sg.x0 + 2.5 * U; s.y = sg.h; s.cube = sg.kind != WallKind::Crate;
                found = true;
                break;
            }
        if (!found) return false;
    }
    auto at = std::lower_bound(level_.saws.begin(), level_.saws.end(), s.x, [](const Saw& a, double x) { return a.x < x; });
    level_.saws.insert(at, s);
    return true;
}

void Game::animateSaws(float rdt) {
    sawSpin_ += rdt * kSawSpin;
    for (Saw& s : level_.saws) if (s.state == Saw::State::Cut) s.sink = std::min(1.f, s.sink + rdt / kSinkTime);
    if (sawSlash_.t >= 0 && (sawSlash_.t += rdt) > 0.3f) sawSlash_.t = -1;
    sawZoom_ = std::max(0.f, sawZoom_ - rdt / 0.5f);
}

void Game::updateSaws(float dt) {
    const double reach = Level::kSawReach;
    // in a maze she may come back over a blade: once she's out of its reach, one that has cut is armed again, so every
    // crossing costs an egg, as the solver has it (Gauntlet::step). (Checked before this update's cuts: one that
    // finished cutting stays cut till the next. A blade crossed twice wants her turning 3 cells or more past it, out
    // of its reach)
    if (gIn_.g)
        for (Saw& s : level_.saws)
            if (s.state == Saw::State::Cut && !cutting(s) && s.x > gIn_.x0 && s.x < gIn_.x1() && std::abs(x_ - s.x) > reach + 0.05)
                s.state = Saw::State::Armed, s.warned = false;
    // sparks jetting up where each blade on screen comes out of its slot (its leading edge: it turns clockwise)
    for (const Saw& s : level_.saws) {
        if (s.x < camX_ - 10 || s.state == Saw::State::Smashed || s.sink > 0) continue;
        if (s.x > camX_ + 22) break;
        if (rng_.next01() < dt * 28)
            fxAdd_.emit({float(s.x) - kR * 0.93f, s.y * Uf + 0.03f, rng_.range(-0.03f, 0.03f)},
                        {rng_.range(-0.7f, 0.5f), rng_.range(2.5f, 4.5f), rng_.range(-0.4f, 0.8f)}, rng_.range(0.25f, 0.45f),
                        rng_.range(0.035f, 0.06f), 0.01f, rng_.next01() < 0.5f ? glow("#fff1c8", 3.f) : sawColor() * 3.f, 1, 1.5f, 9);
    }
    // a cut under way: the blade saws through the bottom egg as it slides over; once across, the egg falls apart
    if (cutSawX_ >= 0) {
        Saw* s = nullptr;
        for (Saw& w : level_.saws) if (cutting(w)) { s = &w; break; }
        if (!s || eggs_.empty() || eggs_.front().cut <= 0 || smashing()) { // (the egg went another way; OVERDRIVE)
            if (!eggs_.empty()) eggs_.front().cut = 0;
            cutSawX_ = -1;
        } else if (gDir_ * (x_ - s->x) < -reach) { // (turned back off it, in a maze: the egg's spared)
            eggs_.front().cut = 0;
            cutSawX_ = -1;
        } else {
            // across it, the egg falls apart; in a maze, quicker: before her front edge reaches the next cell, so she
            // comes into it one egg down, as the solver has it (Gauntlet::step: the blade's egg is gone in its own
            // cell). Else a roof the blade makes room for, or a step just past it, would meet the egg it's cutting
            const double done = gIn_.g ? -kMazeCutDone : reach;
            const float k = float((gDir_ * (x_ - s->x) + reach) / (done + reach));
            eggs_.front().cut = std::clamp(k, 0.01f, 1.f);
            // sparks spraying out of the cut, where the blade's rim goes into the shell and comes out of it
            const glm::vec3 hub{float(s->x), s->y * Uf, 0.f};
            const glm::vec3 c = sawColor();
            for (float edge : {0.27f, -0.27f}) {
                const float dx = std::clamp(float(x_) + edge - hub.x, -kR * 0.95f, kR * 0.95f);
                const glm::vec2 r{dx, std::sqrt(std::max(0.f, kR * kR - dx * dx))};
                for (int i = 0; i < 8; ++i) {
                    const glm::vec3 v = rimVelocity(r, rng_.range(4.f, 9.f)) + glm::vec3(rng_.range(-1.f, 1.f), rng_.range(1.f, 3.5f), rng_.range(-1.5f, 3.f));
                    fxAdd_.emit(hub + glm::vec3(r, rng_.range(-0.05f, 0.05f)), v, rng_.range(0.2f, 0.45f), rng_.range(0.06f, 0.11f), 0.015f,
                                i % 3 ? glow("#fff1c8", 3.4f) : c * 3.4f, 1, 1.2f, 11);
                }
            }
            if (k >= 1) sawEgg(*s);
        }
    }
    for (Saw& s : level_.saws) {
        if (s.state != Saw::State::Armed) continue;
        if (s.x - kR > x_ + 60) break;
        const double rel = gDir_ * (x_ - s.x); // how far past it she is, her way (< 0: still to come)
        const bool sameFloor = std::abs(baseY_ - s.y * U) < Level::kSawR - 0.06; // (a maze's other floors are out of its reach)
        // its whine, about a second out (heard before it's on screen for long)
        if (!s.warned && rel < 0 && rel > -(speed_ * 1.1 + 2) && (sameFloor || !gIn_.g)) { s.warned = true; if (!smashing()) sfx(Sfx::SawWhirr); }
        if (smashing()) { if (s.x - kR < x_ + surge::SMASH_AHEAD + 0.3) smashSaw(s); continue; }
        if (cutting(s) || rel < -reach) continue;               // being sawn through; not there yet
        if (cutSawX_ >= 0) continue;                             // (another blade is sawing the bottom egg: next, the one above)
        if (rel > reach) { if (!gIn_.g) s.state = Saw::State::Passed; continue; } // over it in the air (a maze: she may be back)
        if (!sameFloor) continue;                                // in the air above it, or on another floor
        if (!eggs_.empty()) { // the bottom egg meets the blade: the bite
            cutSawX_ = s.x; cutSawY_ = s.y;
            eggs_.front().cut = 0.01f;
            sfx(Sfx::SawCut, 0); buzz(14);
            freeze_ = std::max(freeze_, 0.06f); // a hit-pause
            shake_ = std::max(shake_, 0.12f); chromaKick_ = std::max(chromaKick_, 0.45f);
            const glm::vec3 at{float(x_) + gDir_ * 0.27f, s.y * Uf + kR * 0.7f, 0.f};
            burst(at, 18, {glow("#fff1c8", 3.4f), sawColor() * 3.2f}, 2.5f, 7, 0.3f, 0.05f, 0.8f, -gDir_ * 1.f, false, 1.5f, 9);
        } else if (rel >= -(kR + 0.22)) { // ...or, with no egg under her, the hen does
            sawHen(s);
            return;
        }
    }
}

// The bottom egg is across the blade: it falls apart in two halves, the yolk spilling, and the stack settles one egg down
void Game::sawEgg(Saw& s) {
    const Egg e = eggs_.front();
    const glm::mat4 m = stackEggMatrix(0);
    eggs_.erase(eggs_.begin());
    cutSawX_ = -1;
    s.state = Saw::State::Cut;
    // cut lengthwise by the blade's plane: each half's shell is the x >= 0 side of the egg mesh (MeshId::EggHalf), turned
    // so its long axis lies where the egg's did and its cut face is the blade's plane (the camera's: one dome toward
    // it, one away)
    const float pace = gDir_ * speed_; // (her velocity: the halves keep near it)
    const glm::vec3 L = glm::normalize(glm::vec3(m[1])), z{0.f, 0.f, 1.f};
    const glm::vec3 N = glm::normalize(z - glm::dot(z, L) * L), B = glm::cross(N, L);
    const glm::vec3 scale{glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2]))};
    for (int side = 0; side < 2; ++side) {
        const float sg = side == 0 ? 1.f : -1.f; // 0: the dome toward the camera, its cut face away; 1: the other way
        // they part: the near half drops away in front, spinning round to show its face; the far one rises behind the
        // hen, its face toward us, tumbling slowly. Both keep near her pace a moment (in view through the slow motion)
        Debris d{DebrisKind::EggHalf, e.pos + N * (0.012f * sg), glm::vec3(0.f), scale,
                 side == 0 ? glm::vec3(pace * 0.72f + rng_.range(-0.3f, 0.3f), rng_.range(1.6f, 2.2f), rng_.range(2.4f, 3.0f))
                           : glm::vec3(pace * 0.68f + rng_.range(-0.3f, 0.3f), rng_.range(5.0f, 5.6f), -rng_.range(1.6f, 2.2f)),
                 side == 0 ? glm::vec3(0.f, rng_.range(8.f, 11.f), 0.f) + L * rng_.range(1.f, 2.f) : L * rng_.range(-4.f, -2.5f), 1.4f, 0, e.ring};
        d.basis = glm::mat3(N * sg, L, B * sg);
        debris_.push_back(d);
    }
    Debris yolk{DebrisKind::Yolk, e.pos - glm::vec3(0, 0.04f, 0), glm::vec3(0.f), glm::vec3(1.f),
                {pace * 0.45f, rng_.range(0.6f, 1.2f), rng_.range(-0.3f, 0.3f)}, glm::vec3(0.f), 1.5f, 0, 0};
    debris_.push_back(yolk);
    const glm::vec3 gold = glow("#ffc23a", 3.f);
    burst(e.pos, 26, {gold, glow("#fff1c8", 2.6f), sawColor() * 3.f}, 1.5f, 4.5f, 0.45f, 0.07f, 0.5f, pace * 0.4f, false, 2.2f, 9); // yolk and sparks
    spawnShards(e.pos, 4, pace * 0.3f);
    rings_.spawn(e.pos + glm::vec3(0, 0, 0.3f), sawColor() * 2.5f, 1.1f, 0.35f, true);
    sfx(Sfx::SawCut, 1); buzz(22);
    shake_ = std::max(shake_, 0.2f); camKick_ = std::max(camKick_, 0.12f); chromaKick_ = std::max(chromaKick_, 0.6f);
    freeze_ = std::max(freeze_, 0.045f);
    sawSlash_ = {e.pos, std::atan2(L.y, L.x), 0.f}; // a slash of light along the cut
    if (time_ - sawCutT_ > kNearChain) {            // (blades in quick succession: only the first slows it all)
        timeScale_ = std::min(timeScale_, 0.3f);    // a beat of slow motion, to see it fall apart
        sawZoom_ = 1.f;                             // and the camera punches in
    }
    sawCutT_ = time_;
    burst(e.pos + glm::vec3(0, 0, 0.3f), 30, {glow("#ffffff", 3.f), glow("#fff1c8", 3.f)}, 4, 9, 0.3f, 0.06f, 0.2f, pace * 0.5f, true, 3.f, 0); // a starburst
    sawDrop_ = Uf; sawDropV_ = 0; // the stack (or the hen) settles onto what's left
    // the near miss the saws are built for: the blade took her last egg, right under her
    if (eggs_.empty()) closeCall(float(baseY_) + 0.2f, "CLOSE SHAVE");
}

// No egg under her: the blade catches the hen
void Game::sawHen(Saw& s) {
    s.state = Saw::State::Cut;
    const glm::vec3 at{float(x_) + gDir_ * 0.22f, s.y * Uf + kR * 0.6f, 0.f};
    burst(at, 40, {glow("#fff1c8", 3.4f), sawColor() * 3.2f, glow("#ff2b4a", 3)}, 3, 9, 0.5f, 0.06f, 1.2f, -2.f, false, 1.5f, 9);
    sfx(Sfx::SawCut, 2);
    die(false);
}

// OVERDRIVE: knocked off its slot, the blade flies off spinning
void Game::smashSaw(Saw& s) {
    s.state = Saw::State::Smashed;
    const glm::vec3 hub{float(s.x), s.y * Uf, 0.f};
    debris_.push_back({DebrisKind::Blade, hub + glm::vec3(0, 0.1f, 0), glm::vec3(0.f), glm::vec3(1.f),
                       {speed_ * 0.9f + rng_.range(1.f, 3.f), rng_.range(5.f, 7.5f), -rng_.range(0.5f, 2.f)},
                       {rng_.range(-5.f, 5.f), rng_.range(-5.f, 5.f), -rng_.range(18.f, 26.f)}, 1.6f, 0, 0});
    burst(hub + glm::vec3(0, kR * 0.6f, 0), 30, {glow("#fff1c8", 3.4f), sawColor() * 3.2f}, 3, 9, 0.5f, 0.06f, 1.f, speed_ * 0.4f, false, 1.5f, 9);
    smashFeedback(hub + glm::vec3(0, 0.5f, 0), 1);
    bonus_ += 3 * surge::POINTS_PER_BLOCK;
}

// ---------------------------------------------------------------- drawing

// a blade, its hub at `hub` (the blade's plane its XY), turned `spin` clockwise: a polished steel disc with a red-hot
// toothed rim, bright etched spokes and dark vents (so the spin reads even small on screen), a hub, and a faint blur
// round the teeth
void Game::emitSawBlade(RenderList& out, const glm::mat4& hub, float spin, glm::vec3 teeth, float glowK) {
    const glm::mat4 b = hub * compose({}, {0, 0, -spin}, glm::vec3(1.f));
    LitMaterial steel; steel.color = hexColor("#dfe6f2"); steel.metalness = 0.85f; steel.roughness = 0.18f;
    steel.emissive = (glm::vec3(0.22f) + teeth * 0.12f) * glowK; steel.rimColor = teeth; steel.rimStrength = 0.9f * glowK; steel.rimPow = 2.2f;
    out.add(Pass::Lit, MeshId::Cylinder) = makeLit(b * compose({}, {kPi / 2, 0, 0}, {kR * 0.8f, 0.04f, kR * 0.8f}), steel);
    out.add(Pass::Lit, MeshId::SawTeeth) = makeEmissive(b * compose({}, {kR, kR, 0.5f}), teeth * (3.f * glowK));
    const glm::vec3 etch = glm::mix(teeth, glm::vec3(1.f), 0.55f) * (2.2f * glowK);
    // three etched spokes, and between them thin dark expansion slots running in from the rim (as on a real blade)
    LitMaterial slot; slot.color = hexColor("#05050a"); slot.roughness = 1;
    for (int k = 0; k < 3; ++k) {
        const float a = float(k) * 2 * kPi / 3, v = a + kPi / 3;
        out.add(Pass::Lit, MeshId::Box) =
            makeEmissive(b * compose({std::cos(a) * kR * 0.42f, std::sin(a) * kR * 0.42f, 0.022f}, {0, 0, a}, {kR * 0.5f, 0.04f, 0.006f}), etch);
        out.add(Pass::Lit, MeshId::Box) =
            makeLit(b * compose({std::cos(v) * kR * 0.66f, std::sin(v) * kR * 0.66f, 0.022f}, {0, 0, v}, {kR * 0.26f, 0.014f, 0.006f}), slot);
    }
    out.add(Pass::Lit, MeshId::Cylinder) = makeLit(b * compose({}, {kPi / 2, 0, 0}, {kR * 0.16f, 0.07f, kR * 0.16f}), steel);
    out.add(Pass::Lit, MeshId::RingFlat) = makeEmissive(b * compose({0, 0, 0.037f}, glm::vec3(kR * 0.17f)), etch);
    out.add(Pass::UnlitAdd, MeshId::RingFlat) = makeUnlit(hub * compose({0, 0, 0.03f}, glm::vec3(kR * 1.03f)), teeth * 0.8f, 0.55f * glowK);
}

void Game::emitSaws(RenderList& out, float pz) {
    if (level_.saws.empty()) return;
    const glm::vec3 base = sawColor(), hot = hexColor("#ff1e3c");
    const bool play = state_ == State::Play && !smashing();
    const double topNow = std::min(baseY_, supportY()) + eggs_.size() * U;
    const float strobe = std::sin(time_ * 22) > 0 ? 1.f : 0.35f;
    LitMaterial slotMat; slotMat.color = hexColor("#07060d"); slotMat.metalness = 0.3f; slotMat.roughness = 0.6f;
    LitMaterial plate; plate.color = hexColor("#1a1826"); plate.metalness = 0.8f; plate.roughness = 0.3f; plate.rimColor = base; plate.rimStrength = 0.5f; plate.rimPow = 3;
    // the blades still to come her way on her floor, nearest first: each needs an egg more than the one before
    std::vector<const Saw*> ahead;
    for (const Saw& s : level_.saws)
        if (s.state == Saw::State::Armed && gDir_ * (s.x - x_) > 0 && !cutting(s) && std::abs(s.y * U - baseY_) < 0.3 && std::abs(s.x - x_) < 20)
            ahead.push_back(&s);
    std::sort(ahead.begin(), ahead.end(), [&](const Saw* a, const Saw* b) { return gDir_ * a->x < gDir_ * b->x; });
    for (const Saw& s : level_.saws) {
        if (s.state == Saw::State::Smashed || s.x < camX_ - 14) continue;
        if (s.x > camX_ + 40) break;
        const float surf = s.y * Uf, top = surf + (s.y > 0 && !level_.gauntletAt(s.x) ? 0.05f : 0.f); // (wall tops: over the dance-floor tiles)
        // when she'd reach it with no egg under her it strobes, hotter and brighter as she nears
        float danger = 0;
        const auto at = std::find(ahead.begin(), ahead.end(), &s);
        const int before = int(at - ahead.begin()) + (cutSawX_ >= 0 ? 1 : 0);
        if (play && at != ahead.end() && topNow - before * U < s.y * U + U - 0.05)
            danger = clampf(1.f - float((std::abs(s.x - x_) - kR) / (speed_ * 1.2 + 0.01)), 0.f, 1.f) * 0.7f + 0.3f;
        const float alive = 1.f - s.sink;
        const glm::vec3 teeth = glm::mix(base * (1.f + pz * 0.3f), glm::mix(hot, glm::vec3(1.f), 0.25f) * (1.6f + 1.4f * danger) * strobe, danger);
        // the slot: a dark trench lined with light; in a cyber cube, set in a steel plate with glowing hazard dashes
        const float slotL = 2 * kR + 0.12f;
        if (s.cube) {
            out.add(Pass::Lit, MeshId::Box) = makeLit(compose({float(s.x), top + 0.012f, 0}, {slotL + 0.3f, 0.03f, 0.46f}), plate);
            for (int i = 0; i < 6; ++i)
                for (float zs : {-0.2f, 0.2f})
                    out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose({float(s.x) - slotL * 0.5f - 0.1f + i * (slotL + 0.2f) / 5.f, top + 0.03f, zs},
                                                                           {0, 0.6f, 0}, {0.07f, 0.008f, 0.03f}), teeth * (1.6f * alive + 0.3f));
        }
        const float slotTop = top + (s.cube ? 0.03f : 0.f);
        out.add(Pass::Lit, MeshId::Box) = makeLit(compose({float(s.x), slotTop + 0.006f, 0}, {slotL, 0.012f, 0.13f}), slotMat);
        for (float zs : {-0.072f, 0.072f})
            out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose({float(s.x), slotTop + 0.008f, zs}, {slotL, 0.014f, 0.014f}), teeth * (1.6f * alive + 0.4f));
        // a pool of its light on the surface round the slot
        out.add(Pass::UnlitAdd, MeshId::Plane) =
            makeUnlit(compose({float(s.x), slotTop + 0.02f, 0}, {-kPi / 2, 0, 0}, {kR * 4.f, 1.2f, 1}), teeth, (0.3f + danger * 0.35f) * alive, Shape::RadialGlow);
        if (s.sink >= 1.f) continue;
        // the blade: half out of its slot (sinking once spent), spinning, haloed
        const glm::mat4 hub = compose({float(s.x), surf - s.sink * (kR + 0.08f), 0}, glm::vec3(1.f));
        emitSawBlade(out, hub, sawSpin_ + float(s.x), teeth, alive);
        out.add(Pass::UnlitAdd, MeshId::Plane) =
            makeUnlit(compose({float(s.x), surf + kR * 0.35f, -0.06f}, glm::vec3(kR * (3.2f + danger * 1.8f))), teeth, (0.5f + danger * 0.45f) * alive, Shape::RadialGlow);
        // the bite: a white-hot glow where the blade is in the shell
        if (cutting(s)) {
            const float cx = std::clamp(float(x_) + 0.27f - float(s.x), -kR * 0.9f, kR * 0.9f);
            const float flick = 0.75f + 0.25f * std::sin(sawSpin_ * 7.3f);
            out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose({float(s.x) + cx, surf + std::sqrt(std::max(0.f, kR * kR - cx * cx)) * 0.8f, 0.3f},
                                                                       glm::vec3(1.1f)), glow("#fff1c8", 2.2f), 0.9f * flick, Shape::RadialGlow);
        }
    }
    // a slash of light along the cut as an egg falls apart, fading
    if (sawSlash_.t >= 0) {
        const float k = 1.f - sawSlash_.t / 0.3f;
        out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose(sawSlash_.pos + glm::vec3(0, 0, 0.3f), {0, 0, sawSlash_.angle - kPi / 2},
                                                                   {0.14f * (0.4f + k), 1.9f * (1.5f - 0.5f * k), 1}), glow("#fff6dc", 3.f), k, Shape::HBeam);
        out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose(sawSlash_.pos + glm::vec3(0, 0, 0.29f), glm::vec3(1.3f * (1.6f - 0.6f * k))), sawColor(), 0.7f * k, Shape::RadialGlow);
    }
}

void Game::emitSawDebris(RenderList& out, const Debris& d) {
    const float k = std::min(1.f, d.life / 0.3f); // shrinking out at the end
    switch (d.kind) {
    case DebrisKind::EggHalf: {
        const float a = glm::length(d.rot);
        glm::mat4 g = glm::translate(glm::mat4(1.f), d.pos);
        if (a > 1e-5f) g = glm::rotate(g, a, d.rot / a);
        g = g * glm::mat4(d.basis) * glm::scale(glm::mat4(1.f), d.scale * k);
        LitMaterial shell; shell.color = hexColor("#f6f3ff"); shell.metalness = 0.15f; shell.roughness = 0.28f; shell.emissive = glm::vec3(0.12f);
        shell.rimColor = hexColor("#ffb8f4"); shell.rimStrength = 1.8f; shell.rimPow = 2.2f;
        LitMaterial white; white.color = hexColor("#fffaf0"); white.roughness = 0.6f; white.emissive = glow("#fff1d6", 0.45f);
        if (d.heat > 0) { // (a laser's half: its cut face red-hot, cooling)
            white.color = glm::mix(white.color, hexColor("#ff7a2a"), d.heat);
            white.emissive = glm::mix(white.emissive, glow("#ff5a14", 3.f), d.heat);
            shell.rimColor = glm::mix(shell.rimColor, glow("#ff6a2b", 2.f), d.heat);
        }
        out.add(Pass::Lit, MeshId::EggHalf) = makeLit(g, shell);
        out.add(Pass::Lit, MeshId::EggHalfCap) = makeLit(g, white);
        // the yolk in the cut face (toward the blunt end), and the egg's neon ring round the cut
        out.add(Pass::Lit, MeshId::Circle) = makeEmissive(g * compose({-0.004f, -0.03f, 0}, {0, -kPi / 2, 0}, {0.18f, 0.22f, 1}), glow("#ffb000", 2.2f));
        const glm::vec3 ring = d.ring ? glow("#ff2bd6", 4.2f) : glow("#29e7ff", 4.2f);
        out.add(Pass::Lit, MeshId::EggRing) = makeEmissive(g * compose({}, {0, kPi / 2, 0}, {1.02f, 1.17f, 1.02f}), ring);
        break;
    }
    case DebrisKind::Yolk:
        out.add(Pass::Lit, MeshId::Sphere) = makeEmissive(compose(d.pos, glm::vec3(0.085f, 0.1f, 0.085f) * k), glow("#ffc23a", 3.f));
        break;
    case DebrisKind::Blade:
        emitSawBlade(out, compose(d.pos, d.rot, glm::vec3(k)), 0.f, sawColor(), 1.f);
        break;
    default: break;
    }
}

} // namespace cs
