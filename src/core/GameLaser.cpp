// Wall lasers (Level.h's Laser; the hazard's row: hazards/Laser.cpp): a turret set into a wall's or a block's side,
// firing along the course through its reach. As she comes near it charges: its lens heats and a laser pointer's thin
// line runs out along its line, its dot on whatever it's aimed at (her, once she's in its line), flickering faster as
// it's ready, a split second of warning with a rising whine. When its row meets her (in its reach, not hit yet this
// pass) it fires a thick beam that slices whatever it meets, level:
// - an egg: cut in two across its middle, the top half thrown up and on, the bottom half dropping, both cut faces
//   glowing red-hot, the yolk spilling; sparks spray off the cut. The eggs over it (and the hen) drop onto the one under
//   it (a cut right under her: a CLOSE SHAVE);
// - the hen: she's cut in half (Hen::slice), the top half sliding off.
// Its beam just over her head fires too: a CLOSE CALL. Then it cools, and charges again if she's still about. One hit
// a pass: she's not hit again till she's out of every laser's reach (as the maze solver has it: Gauntlet::step).
#include "Game.h"

#include <algorithm>
#include <cmath>

namespace cs {

namespace {

constexpr float kCharge = 0.45f; // its warning: the pointer, before it can fire
constexpr float kFire = 0.3f;    // the beam
constexpr float kRest = 0.8f;    // cooling, before it charges again

// a camera-facing quad from `a` to `b` (mesh uv.y = 1 at `b`), `width` wide
glm::mat4 beamQuad(glm::vec3 a, glm::vec3 b, float width, glm::vec3 cam) {
    const glm::vec3 c = (a + b) * 0.5f, axis = b - a;
    glm::vec3 side = glm::cross(axis, glm::normalize(cam - c));
    const float sl = glm::length(side);
    side = sl > 1e-5f ? side / sl : glm::vec3(1, 0, 0);
    glm::mat4 m(1.f);
    m[0] = glm::vec4(side * width, 0.f);
    m[1] = glm::vec4(axis, 0.f);
    m[2] = glm::vec4(glm::normalize(glm::cross(side, axis)), 0.f);
    m[3] = glm::vec4(c, 1.f);
    return m;
}

glm::vec3 beamRed() { return hexColor("#ff2b4a"); }

} // namespace

bool Game::debugLaser(double ahead, int height, int dir, double reach) {
    if (state_ != State::Play) return false;
    Laser z;
    z.x = x_ + ahead;
    z.row = level_.heightAt(z.x) + height;
    z.dir = dir < 0 ? -1 : 1;
    z.x0 = z.dir < 0 ? z.x - reach : z.x;
    z.x1 = z.dir < 0 ? z.x : z.x + reach;
    level_.lasers.insert(std::lower_bound(level_.lasers.begin(), level_.lasers.end(), z.x, [](const Laser& a, double v) { return a.x < v; }), z);
    return true;
}

// what its row meets of her now: -1 nothing (under her, or over her head), 0..n-1 that egg, n her body; *brush: one
// row over her head
int Game::laserMeets(const Laser& z, bool* brush) const {
    const double rel = (z.centre() - baseY_) / U;
    const int n = int(eggs_.size());
    if (brush) *brush = rel >= n + Gauntlet::kBodyRows && rel < n + Gauntlet::kBodyRows + 1;
    if (rel < 0 || rel >= n + Gauntlet::kBodyRows) return -1;
    return std::min(static_cast<int>(rel), n);
}

void Game::updateLasers(float dt) {
    // her front edge (in a maze, as she comes into a cell: as the solver has it)
    const double front = x_ + gDir_ * (0.22 + (gIn_.g ? U / 2 : 0.0));
    const bool play = state_ == State::Play;
    bool inAny = false;
    for (Laser& z : level_.lasers) {
        z.flash = std::max(0.f, z.flash - dt / 0.45f);
        z.t += dt;
        if (z.x - Level::kLaserRange > x_ + 60) break;
        if (!level_.traceLaser(z) || z.x1 - z.x0 < 0.1) continue;
        const bool in = play && front >= z.x0 && front <= z.x1;
        inAny = inAny || in;
        const double ahead = gDir_ * ((gDir_ > 0 ? z.x0 : z.x1) - front); // (> 0: her way, still to come)
        const bool near = play && (in || (ahead > 0 && ahead < speed_ * kCharge + 0.6));
        switch (z.state) {
        case Laser::State::Idle:
            if (near) { z.state = Laser::State::Charging; z.t = 0; z.hitX = -1; sfx(Sfx::LaserHum); }
            break;
        case Laser::State::Charging:
        case Laser::State::Charged: {
            if (!near) { z.state = Laser::State::Idle; break; }
            if (z.state == Laser::State::Charging && z.t >= kCharge) z.state = Laser::State::Charged;
            if (!in || zapped_) break;
            // its row meets her (fire, even if it's only just charging: it's her own doing) or passes just over her head
            bool brush = false;
            const int meets = laserMeets(z, &brush);
            if (meets < 0 && !(brush && !brushed_)) break;
            z.state = Laser::State::Firing; z.t = 0; z.flash = 1;
            sfx(Sfx::LaserCut, 2);
            shake_ = std::max(shake_, 0.08f);
            if (meets < 0) { // (over her head: the beam fires past her)
                brushed_ = true;
                closeCall(float(z.centre()), "CLOSE CALL");
                break;
            }
            if (smashing()) break; // (OVERDRIVE: it can't touch her)
            zapped_ = true;
            z.hitX = x_;
            if (meets < int(eggs_.size())) laserEgg(z, size_t(meets));
            else { laserHen(z); return; }
            break;
        }
        case Laser::State::Firing:
            if (z.t >= kFire) { z.state = Laser::State::Resting; z.t = 0; }
            break;
        case Laser::State::Resting:
            if (z.t >= kRest) { z.state = Laser::State::Idle; z.t = 0; }
            break;
        }
    }
    // (out of every laser's reach: she can be hit again; in a maze, out of every column its beams cross, the column her
    // front edge is in, as the solver has it and the bot asks it: Gauntlet::step)
    if (gIn_.g) inAny = gIn_.g->beamColumn(gIn_.col(x_ + gDir_ * 0.22));
    if (!inAny) zapped_ = brushed_ = false;
}

void Game::coolLasers(float dt) {
    for (Laser& z : level_.lasers) {
        z.flash = std::max(0.f, z.flash - dt / 0.45f);
        z.t += dt;
        if (z.state == Laser::State::Firing && z.t >= kFire) { z.state = Laser::State::Resting; z.t = 0; }
        else if (z.state == Laser::State::Resting && z.t >= kRest) { z.state = Laser::State::Idle; z.t = 0; }
        else if (z.state == Laser::State::Charging || z.state == Laser::State::Charged) { z.state = Laser::State::Idle; z.t = 0; }
    }
}

// The beam meets egg i of her stack: it's cut in two across its middle, level with the beam, and the eggs over it drop
void Game::laserEgg(Laser& z, size_t i) {
    const Egg e = eggs_[i];
    const glm::mat4 m = stackEggMatrix(i);
    eggs_.erase(eggs_.begin() + static_cast<long>(i));
    // the halves: each half's shell is the x >= 0 side of the egg mesh (MeshId::EggHalf), turned so its cut face lies
    // level (the beam's plane): the top half's dome up, the bottom half's down; the egg's long axis as it lay
    const glm::vec3 L = glm::normalize(glm::vec3(m[1])), up{0.f, 1.f, 0.f};
    const glm::vec3 N = glm::normalize(up - glm::dot(up, L) * L), B = glm::cross(N, L);
    const glm::vec3 scale{glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2]))};
    const float pace = gDir_ * speed_, push = float(z.dir) * 2.5f; // (the beam's push: the way it fires)
    for (int side = 0; side < 2; ++side) {
        const float sg = side == 0 ? 1.f : -1.f; // 0: the top half, thrown up and on; 1: the bottom one, dropping behind
        Debris d{DebrisKind::EggHalf, e.pos + N * (0.02f * sg), glm::vec3(0.f), scale,
                 side == 0 ? glm::vec3(pace * 0.75f + push + rng_.range(-0.3f, 0.3f), rng_.range(4.6f, 5.4f), rng_.range(0.8f, 1.6f))
                           : glm::vec3(pace * 0.4f + push * 0.5f + rng_.range(-0.3f, 0.3f), rng_.range(0.4f, 1.f), rng_.range(1.4f, 2.f)),
                 side == 0 ? glm::vec3(0.f, 0.f, -gDir_ * rng_.range(7.f, 10.f)) : glm::vec3(rng_.range(2.f, 4.f), 0.f, rng_.range(-2.f, 2.f)),
                 1.4f, 0, e.ring};
        d.basis = glm::mat3(N * sg, L, B * sg);
        d.heat = 1;
        debris_.push_back(d);
    }
    debris_.push_back({DebrisKind::Yolk, e.pos, glm::vec3(0.f), glm::vec3(1.f), {pace * 0.5f, rng_.range(0.5f, 1.2f), rng_.range(0.2f, 0.6f)}, glm::vec3(0.f), 1.5f, 0, 0});
    // sparks spraying off the cut, the way the beam goes, and a flash at it
    const glm::vec3 at{float(x_), float(z.centre()), 0.f};
    for (int i2 = 0; i2 < 34; ++i2)
        fxAdd_.emit(at + glm::vec3(rng_.range(-0.05f, 0.05f), rng_.range(-0.03f, 0.03f), 0.f),
                    {float(z.dir) * rng_.range(2.f, 8.f) + pace * 0.2f, rng_.range(-0.6f, 2.2f), rng_.range(-1.5f, 1.5f)}, rng_.range(0.15f, 0.35f),
                    rng_.range(0.04f, 0.08f), 0.01f, i2 % 3 ? glow("#fff1c8", 3.4f) : beamRed() * 3.4f, 1, 1.5f, 6);
    burst(at, 24, {glow("#ffc23a", 3.f), glow("#fff1c8", 2.8f), beamRed() * 3.f}, 1.5f, 4.5f, 0.4f, 0.07f, 0.5f, pace * 0.4f, false, 2.2f, 9);
    rings_.spawn(at + glm::vec3(0, 0, 0.3f), beamRed() * 2.5f, 1.f, 0.3f, true);
    sfx(Sfx::LaserCut, 0); buzz(20);
    shake_ = std::max(shake_, 0.16f); camKick_ = std::max(camKick_, 0.1f); chromaKick_ = std::max(chromaKick_, 0.55f);
    freeze_ = std::max(freeze_, 0.045f);
    if (time_ - sawCutT_ > kNearChain) { timeScale_ = std::min(timeScale_, 0.32f); sawZoom_ = 1.f; } // (a beat to see it fall apart)
    sawCutT_ = time_;
    // the eggs over the cut, and the hen, drop onto the one under it
    for (size_t k = i; k < eggs_.size(); ++k) { eggs_[k].drop += Uf; eggs_[k].dropV = 0; }
    henDrop_ += Uf; henDropV_ = 0;
    if (i == eggs_.size()) closeCall(float(baseY_) + float(eggs_.size()) * Uf + 0.2f, "CLOSE SHAVE"); // (right under her)
}

// The beam at her body: she's cut in half
void Game::laserHen(Laser& z) {
    const float cutY = float(z.centre());
    const glm::vec3 at{float(x_), cutY, 0.f};
    burst(at, 40, {glow("#fff1c8", 3.4f), beamRed() * 3.4f, glow("#ffc23a", 3)}, 3, 9, 0.5f, 0.06f, 0.6f, float(z.dir) * 3.f, false, 1.5f, 6);
    sfx(Sfx::LaserCut, 1);
    die(false, Death::Sliced, cutY);
}

void Game::emitLasers(RenderList& out, float pz) {
    if (level_.lasers.empty()) return;
    const glm::vec3 cam = camera_.position();
    LitMaterial housing; housing.color = hexColor("#1b1826"); housing.metalness = 0.85f; housing.roughness = 0.3f;
    housing.rimColor = beamRed(); housing.rimStrength = 0.7f; housing.rimPow = 3.f;
    LitMaterial barrel; barrel.color = hexColor("#2b2838"); barrel.metalness = 0.9f; barrel.roughness = 0.22f;
    for (const Laser& z : level_.lasers) {
        if (z.x < camX_ - 12 - Level::kLaserRange) continue;
        if (z.x > camX_ + 40) break;
        const float x = float(z.x), y = float(z.centre()), d = float(z.dir);
        const bool charging = z.state == Laser::State::Charging || z.state == Laser::State::Charged;
        const float charge = z.state == Laser::State::Charged ? 1.f : charging ? std::min(1.f, z.t / kCharge) : 0.f;
        const float firing = z.state == Laser::State::Firing ? 1.f - z.t / kFire : 0.f;
        const float cooling = z.state == Laser::State::Resting ? 1.f - z.t / kRest : 0.f;
        // the turret: a housing bolted to the wall's side, a barrel out its way, its lens glowing as it charges
        out.add(Pass::Lit, MeshId::Box) = makeLit(compose({x + d * 0.08f, y, 0.f}, {0.16f, 0.4f, 0.62f}), housing, x);
        out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose({x + d * 0.161f, y + 0.15f, 0.f}, {0.01f, 0.03f, 0.5f}), beamRed() * (0.8f + 2.f * charge));
        out.add(Pass::Lit, MeshId::Cylinder) = makeLit(compose({x + d * 0.26f, y, 0.f}, {0, 0, kPi / 2}, {0.085f, 0.22f, 0.085f}), barrel);
        const float hot = 0.6f + 3.f * charge * (0.75f + 0.25f * std::sin(time_ * (20.f + 40.f * charge))) + 4.f * firing + 1.2f * cooling;
        const glm::vec3 tip{x + d * 0.375f, y, 0.f};
        out.add(Pass::Lit, MeshId::Circle) = makeEmissive(compose(tip, {0, d * kPi / 2, 0}, glm::vec3(0.07f)), beamRed() * hot);
        out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose(tip + glm::vec3(0, 0, 0.05f), glm::vec3(0.3f + 0.5f * charge + 0.9f * firing)),
                                                           beamRed() * (0.5f + hot * 0.4f), 0.8f, Shape::RadialGlow);
        if (!z.traced()) continue;
        // where its line ends: her (struck, or in its line), else the far end of its reach
        const float far = d < 0 ? float(z.x0) : float(z.x1);
        float end = far;
        if (z.hitX >= 0 && firing > 0) end = float(z.hitX);
        else if (charging && state_ == State::Play && x_ >= z.x0 && x_ <= z.x1 && laserMeets(z, nullptr) >= 0) end = float(x_);
        const glm::vec3 a = tip, b{end, y, 0.f};
        if (charging) { // the pointer: a thin flickering line, its dot on what it's aimed at
            const float flick = 0.55f + 0.45f * std::sin(time_ * (30.f + 50.f * charge) + x);
            out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(beamQuad(a, b, 0.018f, cam), beamRed() * (1.2f + 2.f * charge) * flick, 0.9f, Shape::Laser, 0.f);
            const glm::vec3 dot{end, y, 0.32f};
            out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose(dot, glm::vec3(0.16f + 0.08f * charge)), beamRed() * (2.5f + 2.f * charge) * flick, 1.f, Shape::RadialGlow);
            out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose(dot, glm::vec3(0.05f)), glm::vec3(1.f, 0.85f, 0.85f) * 4.f * flick, 1.f, Shape::RadialGlow);
        }
        if (firing > 0) { // the beam: a white-hot core, thick red round it, a haze; its light on the floor under it
            const float flick = 0.9f + 0.1f * std::sin(time_ * 60.f);
            const float w = 0.2f + 0.14f * firing;
            out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(beamQuad(a, b, w * 0.45f, cam), glm::vec3(1.f, 0.9f, 0.9f) * 5.f * firing * flick, 1.f, Shape::Laser, 0.f);
            out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(beamQuad(a, b, w * 1.6f, cam), beamRed() * (3.2f + pz * 0.4f) * firing * flick, 1.f, Shape::Laser, 0.f);
            out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(beamQuad(a, b, w * 5.f, cam), beamRed() * 0.9f * firing, 0.6f, Shape::Laser, 0.f);
            out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose(b + glm::vec3(0, 0, 0.3f), glm::vec3(0.7f + 0.6f * firing)), glm::vec3(1.f, 0.6f, 0.5f) * 3.f * firing, 1.f, Shape::RadialGlow);
            const float floorY = float(level_.groundBelow((a.x + b.x) * 0.5, y)) + 0.02f;
            out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose({(a.x + b.x) * 0.5f, floorY, 0}, {-kPi / 2, 0, 0}, {std::abs(b.x - a.x) + 0.6f, DEPTH * 0.9f, 1}),
                                                               beamRed() * 0.5f * firing, 0.5f, Shape::RadialGlow);
        }
    }
}

} // namespace cs
