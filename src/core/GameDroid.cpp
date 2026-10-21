// The jet droid (DroidModel.h). On the title screen it circles the hen, passing behind her and in front, nose first
// and banking into the turn, its fans spinning and its exhaust flames flickering (the Blender animation, replayed),
// with a soft shadow on the street below; when a run starts it peels off and boosts away.
// In a run it raids the stack: at a random point of the level, when the hen carries 2 eggs or more, it warps in from
// any direction, circles high over her while its laser charges, zaps the bottom egg (or two, one beam each), and
// zips off. Fair by design: only when the meter can replace what it takes, never in OVERDRIVE or past the finish,
// and never with a wall coming up too soon to re-stack what it takes (it waits, then fires).
#include "Game.h"
#include "DroidModel.h"

#include <glm/gtc/matrix_transform.hpp>

namespace cs {

namespace {
constexpr float kOrbitRx = 2.5f, kOrbitRz = 1.9f;  // the ellipse around the hen (x across the screen, z in depth)
constexpr float kOrbitShiftX = 0.9f;              // its centre, right of the hen: the camera looks 1.7 m ahead of her
constexpr float kOrbitSpeed = 0.9f;               // rad/s: a lap in ~7 s
constexpr float kHeight = 1.8f, kScale = 1.7f;    // flying height (body centre) and size (the model is ~1 m wide)
constexpr float kFaceCamera = 0.55f;              // turns its nose partly toward the camera: a 3/4 view, not a flat profile
constexpr float kBank = 0.38f;                    // roll into the turn (rad)
constexpr float kExit = 1.4f;                     // seconds to boost off screen when a run starts

// the raid
constexpr float kRaidEnter = 0.75f;   // seconds to fly in from where it warped in
constexpr float kRaidCharge = 0.55f;  // ... when the laser starts charging
constexpr float kRaidShot = 1.2f;     // ... first shot (the second 0.3 s later)
constexpr float kRaidHold = 6.f;      // a shot still held (her last egg, a wall at her feet) by now is called off
constexpr float kRaidLeave = 0.8f;    // seconds to zip off screen
constexpr float kRaidHigh = 2.7f;     // circling this high over the hen's stack top (clear of her, at full size)
const glm::vec3 kFireSpot{1.5f, 2.4f, 0.9f}; // where it holds to shoot (over the stack top): above her, a little ahead and toward the camera, always in view
constexpr float kRaidR = 1.6f, kRaidSpin = 2.2f; // the circle's radius and turn rate (rad/s)
constexpr float kBeamLife = 0.5f;     // a laser beam: snaps on, holds, then thins out and fades

// a cheap flicker in 0.75..1.25: two detuned sines (the Blender flames' noise modifier, by eye)
float flicker(float t, float seed) {
    return 1.f + 0.13f * std::sin(t * 37.f + seed) * std::sin(t * 23.f + seed * 1.7f) + 0.08f * std::sin(t * 61.f + seed * 3.1f);
}
} // namespace

// The raid's circle over the hen, relative to her stack top
glm::vec3 Game::raidOrbit(float t) const {
    const float th = raid_.theta0 + kRaidSpin * t;
    return {kRaidR * std::cos(th), kRaidHigh + 0.12f * std::sin(t * 5.f), 1.2f * kRaidR * std::sin(th)};
}

// (eased, so it doesn't jump when an egg is zapped or laid)
glm::vec3 Game::raidAnchor() const { return {float(x_), raid_.topY, 0.f}; }
static float stackTop(double baseY, size_t eggs) { return float(baseY) + float(eggs) * Uf; }

Game::DroidPose Game::droidPose() const {
    if (raid_.on) {
        const glm::vec3 h = raidAnchor();
        glm::vec3 off = raidOrbit(raid_.orbitT);
        if (raid_.lock > 0) { const float l = raid_.lock * raid_.lock * (3 - 2 * raid_.lock); off = glm::mix(off, kFireSpot, l); } // gliding to its firing spot
        float k = 1.f;
        if (raid_.t < kRaidEnter) { // flying in from where it warped in
            const float e = raid_.t / kRaidEnter, s = e * e * (3 - 2 * e);
            off = glm::mix(raid_.spawn, off, s);
        }
        if (raid_.leaving >= 0) { // zipping off: away from the circle and up
            const float e = raid_.leaving;
            off = raid_.leaveFrom + raid_.leaveDir * (5.f * e + 16.f * e * e);
            k = std::max(0.f, 1.f - e / kRaidLeave);
        }
        const glm::vec3 pos = h + off;
        // nose on the hen's stack: it's watching its target (and the laser leaves from its eye)
        glm::vec3 look = (raid_.leaving >= 0 ? raid_.leaveDir : glm::normalize(h - pos));
        const float pitch = std::asin(std::clamp(-look.y, -1.f, 1.f));
        look.y = 0;
        const glm::vec3 dir = glm::length(look) > 1e-4f ? glm::normalize(look) : glm::vec3(-1, 0, 0);
        return {pos, dir, 0.25f * std::sin(raid_.t * 3.f), k, pitch, kScale, false}; // (the same size as on the title)
    }
    const glm::vec3 hp = hen_.nodes[hen_.root].pos;
    const float th = droidT_ * kOrbitSpeed;
    const glm::vec3 c{hp.x + kOrbitShiftX, 0.f, -0.2f};
    glm::vec3 pos = c + glm::vec3(kOrbitRx * std::cos(th), kHeight + 0.12f * std::sin(droidT_ * 1.7f) + 0.25f * std::sin(th * 2.f),
                                  kOrbitRz * std::sin(th));
    glm::vec3 dir = glm::normalize(glm::vec3(-kOrbitRx * std::sin(th), 0.f, kOrbitRz * std::cos(th))); // along the path
    float bank = kBank, k = 1.f;
    if (droidExitT_ >= 0) { // peel off: accelerate along the path and climb out
        const float e = droidExitT_;
        pos = droidExitFrom_ + droidExitDir_ * (4.f * e + 9.f * e * e) + glm::vec3(0.f, 3.f * e * e, 0.f);
        dir = droidExitDir_;
        bank = kBank * (1.f - std::min(1.f, e * 2.f));
        k = std::max(0.f, 1.f - e / kExit);
    }
    return {pos, dir, bank, k, -0.08f, kScale, true};
}

void Game::beginDroidExit() {
    const DroidPose p = droidPose();
    droidExitFrom_ = p.pos;
    droidExitDir_ = glm::normalize(p.dir + glm::vec3(0.6f, 0.f, 0.f)); // (veering ahead, the way the run goes)
    droidExitT_ = 0;
}

bool Game::droidVisible() const {
    if (!droidModel().ok || showroom()) return false;
    if (raid_.on) return true;
    return state_ == State::Title || (droidExitT_ >= 0 && droidExitT_ < kExit);
}

// A run's raids: none before level 5 (Remote Config "droid_level"), then where along the level the droid tries to
// strike (one on its first two levels, then one or two; or as many as the level says: LevelData.h), spread over its
// middle; an endless course gets one every 180-260 m
void Game::planRaids() {
    raid_ = DroidRaid{};
    raidPlan_.clear();
    raidNext_ = 0;
    if (!droidModel().ok) return;
    if (debugRaids_) { // developer menu: one every ~35 m, from 20 m
        debugRaids_ = false;
        for (double x = 20; x < 5000; x += 35) raidPlan_.push_back(x);
        return;
    }
    const int authored = Campaign::raidsOf(campaign_.level()); // (a level may say how many: LevelData.h)
    if (!tune_.droidRaids || authored == 0 || (authored < 0 && campaign_.level() < tune_.droidFirstLevel)) return; // (it's introduced a few levels in)
    const double finish = level_.finish();
    if (finish <= 0) {
        for (double x = 180 + 80 * rng_.next01(); x < 5000; x += 180 + 80 * rng_.next01()) raidPlan_.push_back(x);
        return;
    }
    const int n = authored > 0 ? authored
                               : campaign_.level() < tune_.droidFirstLevel + 2 ? 1 : (rng_.next01() < 0.5f ? 2 : 1); // (one raid on its first two levels)
    for (int i = 0; i < n; ++i) {
        // (each in its own part of the level, so two never come close together)
        const double lo = 0.18 + 0.67 * double(i) / n, hi = 0.18 + 0.67 * double(i + 1) / n;
        raidPlan_.push_back(finish * (lo + (hi - lo) * rng_.next01()));
    }
}

bool Game::raidAllowed(int shots) const {
    return state_ == State::Play && !smashing() && clearT_ < 0 && !raid_.on && !gIn_.g && eggs_.size() >= 2 && meter_ >= float(shots) + 0.5f;
}

void Game::startRaid(int shots) {
    raid_ = DroidRaid{};
    raid_.on = true;
    raid_.shots = std::clamp(shots, 1, 2);
    raid_.fireAt[0] = kRaidShot;
    raid_.fireAt[1] = kRaidShot + 0.3f;
    // from any direction: around the hen, from level with her to high above, well outside the screen
    const float yaw = rng_.range(0.f, 2 * kPi), elev = rng_.range(-0.05f, 1.0f);
    raid_.spawn = 11.f * glm::vec3(std::cos(elev) * std::cos(yaw), std::sin(elev), std::cos(elev) * std::sin(yaw)) + glm::vec3(0, 2.f, 0);
    raid_.theta0 = yaw; // (it joins the circle on the side it came from)
    raid_.topY = stackTop(baseY_, eggs_.size());
    sfx(Sfx::DroidWarp);
    flashScreen(srgbColor("#3a8cff"), 0.12f);
    track("droid_raid", {{"level", std::to_string(campaign_.level())}, {"shots", std::to_string(raid_.shots)}});
}

void Game::debugDroidRaid(int shots) { if (droidModel().ok && raidAllowed(shots)) startRaid(shots); }

// the laser hits the bottom egg: it flashes and swells, then pops out of the stack sideways (back from her and toward
// the camera, so it's seen going), tumbling and crackling blue, whole till it splats on the ground behind her; the
// eggs above settle down into its place (the settle after a saw), so the gap it leaves is seen too. It leaves the stack
// at once (her egg count, and so the run, is as before): only how it looks takes its time
void Game::zapEgg(int shot) {
    if (eggs_.empty()) return;
    const Egg e = eggs_.front();
    eggs_.erase(eggs_.begin());
    const glm::vec3 p = e.pos;
    Debris d{DebrisKind::Egg, p, e.rot, e.scale, {speed_ - 5.5f, 5.5f, 3.f}, {rng_.range(-5, 5), rng_.range(-3, 3), -11.f}, 3, 0, e.ring};
    d.zapped = true;
    debris_.push_back(d);
    burst(p, 34, {glow("#3a8cff", 2.5f), glow("#9fe7ff", 2.f), glm::vec3(1.f)}, 3, 9, 0.5f, 0.08f, 0, -speed_ * 0.2f);
    rings_.spawn({p.x, p.y, 0.6f}, glow("#3a8cff", 2.f), 1.1f, 0.3f, true);
    puff({p.x, p.y - 0.15f, 0}, 4, hexColor("#3a3150"), 1, 0.5f);
    sawDrop_ = std::max(sawDrop_, Uf); sawDropV_ = 0; // (the stack settles into the gap, then jiggles: Game::update)
    sfx(Sfx::DroidLaser, shot); sfx(Sfx::Crack); buzz(22);
    shake_ = std::max(shake_, 0.24f); chromaKick_ = std::max(chromaKick_, 0.45f); // (a light fringe: the pop must read)
    flashScreen(srgbColor("#59b3ff"), 0.14f);
    popup(shot ? "ZAP ×2!" : "ZAP!", {p.x + 0.4f, p.y + 0.9f, 0}, PopKind::Groove);
}

void Game::updateDroid(float rdt) {
    droidT_ += rdt;
    if (droidExitT_ >= 0) droidExitT_ += rdt;
    if (state_ == State::Title) { droidExitT_ = -1; raid_ = DroidRaid{}; } // back on the title: circling again

    // a planned raid falls due: strike if it's fair now, or keep watching for 40 m, then let it go
    if (state_ == State::Play && raidNext_ < raidPlan_.size() && x_ >= raidPlan_[raidNext_]) {
        const int shots = rng_.next01() < 0.45f ? 2 : 1;
        if (raidAllowed(shots)) { startRaid(shots); ++raidNext_; }
        else if (raidAllowed(1)) { startRaid(1); ++raidNext_; }
        else if (x_ > raidPlan_[raidNext_] + 40) ++raidNext_;
    }
    if (!raid_.on) return;
    raid_.t += rdt;
    // it holds still to shoot: from just before a shot until the beam has faded it stops circling, and while a beam is
    // on it doesn't rise or sink with the stack either, so the beam stays put between its eye and the egg
    // (it glides from its circle to its firing spot in the 0.45 s before a shot, and back after the beam's gone; as it
    // glides in it keeps up with the stack, so it fires from just over it however she's climbed)
    const bool beamOn = raid_.beamT[0] >= 0 || raid_.beamT[1] >= 0;
    bool firing = beamOn;
    if (raid_.fired < raid_.shots && raid_.t >= raid_.fireAt[raid_.fired] - 0.45f) firing = true;
    raid_.lock = std::clamp(raid_.lock + (firing && raid_.leaving < 0 ? rdt / 0.4f : -rdt / 0.5f), 0.f, 1.f);
    const float top = stackTop(baseY_, eggs_.size()) + sawDrop_; // (the stack as drawn: settling after a zap)
    if (!firing || raid_.leaving >= 0) {
        raid_.orbitT += rdt;
        raid_.topY += (top - raid_.topY) * std::min(1.f, rdt * 3.f);
    } else if (!beamOn) {
        raid_.topY += (top - raid_.topY) * std::min(1.f, rdt * 8.f);
    }
    for (float& b : raid_.beamT) if (b >= 0) { b += rdt; if (b > kBeamLife) b = -1; }
    if (raid_.leaving >= 0) {
        raid_.leaving += rdt;
        if (raid_.leaving > kRaidLeave) raid_ = DroidRaid{};
        return;
    }
    auto leave = [&] {
        raid_.leaving = 0;
        raid_.leaveFrom = glm::mix(raidOrbit(raid_.orbitT), kFireSpot, raid_.lock * raid_.lock * (3 - 2 * raid_.lock));
        const float th = raid_.theta0 + kRaidSpin * raid_.orbitT;
        raid_.leaveDir = glm::normalize(glm::vec3(-std::sin(th), 0.7f, 1.2f * std::cos(th)) + glm::vec3(0.4f, 0, 0));
    };
    if (state_ != State::Play || smashing() || clearT_ >= 0) { leave(); return; } // a crash, OVERDRIVE, the finish: off it goes
    if (!raid_.charged && raid_.t >= kRaidCharge) { raid_.charged = true; sfx(Sfx::DroidCharge); }
    if (raid_.fired < raid_.shots && raid_.t >= raid_.fireAt[raid_.fired]) {
        // hold fire if it would leave her short for a wall coming up too soon to re-stack: within a third of a second
        // (0.8 s if it takes her last egg: she has more to lay back). With eggs to spare it fires even right before a
        // wall. Otherwise it waits, then fires.
        // Saws count too: from each one she meets on the way, she needs an egg more (it saws one off). And it never
        // zaps the egg a blade is sawing
        bool wallAhead = eggs_.empty() || cutSawX_ >= 0;
        const double reach = std::max(1.0, double(speed_) * (eggs_.size() < 2 ? 0.8 : 0.35));
        const double land = std::min(baseY_, std::max(level_.heightAt(x_ - 0.22), level_.heightAt(x_ + 0.22)) * U); // (where a drop lands her)
        const double after = land + double(eggs_.size() - (eggs_.empty() ? 0 : 1)) * U; // her stack top once it's zapped
        for (double d = 0.22; d < reach && !wallAhead; d += 0.15) {
            int saws = 0;
            for (const Saw& w : level_.saws) {
                if (w.x - Level::kSawReach > x_ + d) break;
                saws += w.state == Saw::State::Armed && w.x + Level::kSawReach >= x_;
            }
            wallAhead = (level_.heightAt(x_ + d) + saws) * U > after + 0.05;
        }
        if (wallAhead && raid_.t < kRaidHold) {
            raid_.fireAt[raid_.fired] = raid_.t + 0.05f;
            if (raid_.fired + 1 < raid_.shots) raid_.fireAt[raid_.fired + 1] = raid_.fireAt[raid_.fired] + 0.3f;
        } else if (wallAhead) {
            raid_.shots = raid_.fired; // called off
        } else {
            const int i = raid_.fired++;
            const glm::vec3 h{float(x_), 0.f, 0.f};
            raid_.beamTo[i] = eggs_.front().pos - h;                        // to the bottom egg
            raid_.beamT[i] = 0;
            zapEgg(i);
        }
    }
    if (raid_.fired >= raid_.shots && raid_.t >= raid_.fireAt[std::max(0, raid_.shots - 1)] + kBeamLife + 0.1f) leave(); // (once the beam's gone)
}

void Game::emitDroid(RenderList& out, bool onStreet) {
    if (!droidVisible()) return;
    const DroidModel& d = droidModel();
    const DroidPose p = droidPose();
    // the droid's nose is its -x: turn it to fly along `dir` (biased toward the camera), then bank about its nose axis
    const glm::vec3 face = p.faceCamera ? glm::normalize(p.dir + glm::vec3(0.f, 0.f, kFaceCamera)) : p.dir;
    const float yaw = std::atan2(face.z, -face.x);
    glm::mat4 m = glm::translate(glm::mat4(1.f), p.pos);
    m = glm::rotate(m, yaw, {0, 1, 0});
    m = glm::rotate(m, -p.bank, {1, 0, 0});
    m = glm::rotate(m, p.pitch, {0, 0, 1}); // + noses it down (its nose is -x)
    m = glm::scale(m, glm::vec3(p.scale));

    const auto mark = out.litMark();
    auto lit = [&](const DroidModel::Mesh& part, float glowBoost) {
        LitMaterial mat;
        mat.color = part.color;
        mat.metalness = part.metal;
        mat.roughness = part.rough;
        if (part.textured) {
            mat.pattern = Pattern::DroidTexture;
            mat.emissive = glm::vec3(part.glow * glowBoost);
        }
        mat.rimColor = hexColor("#ff5ad9"); mat.rimStrength = 0.3f; mat.rimPow = 3.f; // a neon edge against the night
        return mat;
    };
    out.add(Pass::Lit, MeshId::DroidBody) = makeLit(m, lit(d.parts[DroidModel::Body], 2.4f));
    out.add(Pass::Lit, MeshId::DroidEye) = makeLit(m, lit(d.parts[DroidModel::Eye], 1.6f));
    out.add(Pass::Lit, MeshId::DroidShroud) = makeLit(m, lit(d.parts[DroidModel::Shroud], 1.f));
    out.add(Pass::Lit, MeshId::DroidBackplate) = makeLit(m, lit(d.parts[DroidModel::Backplate], 1.f));
    for (const DroidModel::Part fp : {DroidModel::FanL, DroidModel::FanR}) {
        const DroidModel::Mesh& fan = d.parts[size_t(fp)];
        const glm::mat4 f = glm::rotate(glm::translate(m, fan.pivot), fan.spin * droidT_, fan.axis);
        out.add(Pass::Lit, fp == DroidModel::FanL ? MeshId::DroidFanL : MeshId::DroidFanR) = makeLit(f, lit(fan, 1.4f));
    }
    if (onStreet) out.reflectSince(mark);

    // the exhausts: a pink flame with a cyan core (unit flame along +x, scaled to the Blender sizes and flickering),
    // and a soft glow at each nozzle; the eye's blue glow
    for (int s = 0; s < 2; ++s) {
        const DroidModel::Anchor& a = d.exhaust[s];
        const glm::vec3 dir = glm::normalize(a.dir);
        const glm::vec3 up = std::abs(dir.y) > 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        const glm::vec3 side = glm::normalize(glm::cross(dir, up)), up2 = glm::cross(side, dir);
        glm::mat4 basis(1.f);
        basis[0] = glm::vec4(dir, 0); basis[1] = glm::vec4(up2, 0); basis[2] = glm::vec4(side, 0);
        const glm::mat4 nozzle = glm::translate(m, a.pos) * basis;
        const float f1 = 1.6f * flicker(droidT_, float(s) * 2.3f), f2 = 1.6f * flicker(droidT_ + 0.37f, float(s) * 5.1f); // (longer than in Blender: they read at a distance)
        const float boost = droidExitT_ >= 0 ? 1.f + 1.5f * std::min(1.f, droidExitT_ * 3.f) : 1.f; // full thrust leaving
        out.add(Pass::UnlitAdd, MeshId::DroidFlame) =
            makeUnlit(glm::scale(nozzle, glm::vec3(a.size.x * f1 * boost, a.size.y, a.size.y)), glow("#ff2bd6", 2.2f), p.k, Shape::BottomFade);
        out.add(Pass::UnlitAdd, MeshId::DroidFlame) =
            makeUnlit(glm::scale(nozzle, glm::vec3(a.size.z * f2 * boost, a.size.w, a.size.w)), glow("#59b3ff", 2.8f), p.k, Shape::BottomFade);
        const glm::vec3 g = glm::vec3(m * glm::vec4(a.pos, 1.f));
        out.add(Pass::UnlitAdd, MeshId::Plane) =
            makeUnlit(compose(g, glm::vec3(0.55f * p.scale * f1)), glow("#ff4fd8", 1.1f), 0.8f * p.k, Shape::RadialGlow);
    }
    const glm::vec3 eye = glm::vec3(m * glm::vec4(d.eye.pos, 1.f));
    // the eye: brighter and bigger while the laser charges
    const float charge = raid_.on && raid_.charged && raid_.fired < raid_.shots ? 1.f + 0.6f * (0.5f + 0.5f * std::sin(raid_.t * 30.f)) : 1.f;
    out.add(Pass::UnlitAdd, MeshId::Plane) =
        makeUnlit(compose(eye, glm::vec3(0.42f * p.scale * charge)), glow("#3a8cff", 1.3f * charge), 0.9f * p.k, Shape::RadialGlow);

    // the laser: while it charges, a thin flickering sight line from its eye to the bottom egg (the warning); then
    // each shot a thick beam, a white-hot core in two layers of blue glow, that snaps on, pulses, then thins and fades,
    // with a flare at its eye and a burst on the egg, and glow sprites along it so it reads from any angle
    if (raid_.on) {
        const glm::vec3 h{float(x_), 0.f, 0.f};
        auto beamBox = [&](glm::vec3 a, glm::vec3 b, float w, glm::vec3 col, float alpha) {
            const glm::vec3 along = b - a;
            const float len = glm::length(along);
            if (len < 1e-3f || alpha <= 0.f) return;
            const glm::vec3 x = along / len;
            const glm::vec3 y = glm::normalize(glm::cross(std::abs(x.z) > 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1), x));
            glm::mat4 m(1.f);
            m[0] = glm::vec4(x * len, 0); m[1] = glm::vec4(y * w, 0); m[2] = glm::vec4(glm::cross(x, y) * w, 0); m[3] = glm::vec4((a + b) * 0.5f, 1);
            out.add(Pass::UnlitAdd, MeshId::Box) = makeUnlit(m, col, alpha);
        };
        // the sight line, while the next shot charges
        if (raid_.charged && raid_.fired < raid_.shots && !eggs_.empty() && raid_.leaving < 0) {
            const float until = raid_.fireAt[raid_.fired] - raid_.t;
            const float k = std::clamp(1.f - until / 0.7f, 0.15f, 1.f) * (0.6f + 0.4f * std::sin(raid_.t * 55.f));
            const DroidPose dp = droidPose();
            beamBox(dp.pos + glm::vec3(0, -0.1f, 0), eggs_.front().pos, 0.03f + 0.03f * k, glow("#59b3ff", 2.f), 0.55f * k);
            out.add(Pass::UnlitAdd, MeshId::Plane) =
                makeUnlit(compose(eggs_.front().pos + glm::vec3(0, 0, 0.3f), glm::vec3(0.5f + 0.3f * k)), glow("#3a8cff", 2.f), 0.5f * k, Shape::RadialGlow);
        }
        for (int i = 0; i < 2; ++i) {
            const float t = raid_.beamT[i];
            if (t < 0) continue;
            const float on = std::min(1.f, t / 0.04f), off = 1.f - std::clamp((t - 0.22f) / (kBeamLife - 0.22f), 0.f, 1.f);
            const float pulse = 1.f + 0.18f * std::sin(t * 70.f);
            const float w = on * (0.35f + 0.65f * off) * pulse, f = on * off;
            const glm::vec3 a = droidPose().pos + glm::vec3(0, -0.1f, 0), b = h + raid_.beamTo[i]; // (from its eye, wherever it is)
            beamBox(a, b, 0.12f * w, glow("#ffffff", 3.5f), f);           // white-hot core
            beamBox(a, b, 0.32f * w, glow("#59b3ff", 2.6f), 0.85f * f);   // blue body
            beamBox(a, b, 0.8f * w, glow("#3a8cff", 1.6f), 0.35f * f);    // outer haze
            for (int s = 1; s < 8; ++s) // glow beads along it (camera-facing, so it's thick even end-on)
                out.add(Pass::UnlitAdd, MeshId::Plane) =
                    makeUnlit(compose(glm::mix(a, b, float(s) / 8.f), glm::vec3(0.9f * w)), glow("#3a8cff", 1.8f), 0.45f * f, Shape::RadialGlow);
            out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose(a, glm::vec3(1.4f * w + 0.2f)), glow("#9fe7ff", 2.6f), f, Shape::RadialGlow);  // muzzle
            out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose(b, glm::vec3(2.6f * w + 0.4f)), glow("#59b3ff", 2.8f), f, Shape::RadialGlow); // impact
            out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose(b, glm::vec3(1.f * w)), glow("#ffffff", 3.f), f, Shape::RadialGlow);
        }
    }

    // its shadow on the street, right below it: a dark core with a soft edge (the street is near-black at night, so it
    // has to be strong to read), smaller and fainter the higher it flies
    // (in a run, on whatever is below it: the street or a wall top)
    const float floorY = state_ == State::Title ? 0.f : float(level_.heightAt(p.pos.x)) * Uf;
    const float h = std::max(0.f, p.pos.y - floorY);
    const float spread = 1.0f * p.scale * (1.f + 0.1f * h), dark = std::clamp(1.f - h / 8.f, 0.f, 1.f) * p.k;
    const glm::vec3 ground{p.pos.x, floorY + 0.012f, p.pos.z};
    out.add(Pass::UnlitAlpha, MeshId::Plane) =
        makeUnlit(compose(ground, {-kPi / 2, 0, 0}, glm::vec3(spread * 1.3f, spread, 1)), glm::vec3(0.f), 0.95f * dark, Shape::RadialGlow);
    out.add(Pass::UnlitAlpha, MeshId::Plane) =
        makeUnlit(compose(ground + glm::vec3(0, 0.002f, 0), {-kPi / 2, 0, 0}, glm::vec3(spread * 0.55f, spread * 0.42f, 1)), glm::vec3(0.f),
                  0.6f * dark, Shape::SoftDisc);
}

} // namespace cs
