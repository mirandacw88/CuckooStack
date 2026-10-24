// Springs (Level.h's Spring; the hazard's row: hazards/Spring.cpp): a coiled pad on a surface, a dark base, a steel
// coil and a lime plate with chevrons sliding along its top. She runs onto it: the coil crushes down and fires, launching her
// (stack and all) `power` rows up with her speed kept (CourseRules.h's springLaunch: the arc the checker traced); a
// deep boing, green sparks and a ring off the plate, the plate overshooting and wobbling back. Wind streaks off her
// as she rises, wings up; she flies and lands as the game's gravity has her (Game::updatePlay).
#include "Game.h"

#include "CourseRules.h"

#include <algorithm>
#include <cmath>

namespace cs {

namespace {

glm::vec3 lime() { return hexColor("#7dff4a"); }
glm::vec3 mint() { return hexColor("#d6ffb8"); }
constexpr float kPadW = 1.1f;   // the plate's width, metres
constexpr float kCoilH = 0.42f; // the coil at rest, base to plate

} // namespace

bool Game::debugSpring(double ahead, int power) {
    if (state_ != State::Play) return false;
    Spring s;
    s.x = x_ + ahead; s.floor = level_.heightAt(s.x); s.power = std::clamp(power, 2, 12);
    level_.springs.insert(std::lower_bound(level_.springs.begin(), level_.springs.end(), s.x, [](const Spring& a, double v) { return a.x < v; }), s);
    return true;
}

void Game::updateSprings(float dt) {
    for (Spring& s : level_.springs) s.squash = std::max(0.f, s.squash - dt * 1.6f);
    for (Spring& s : level_.gsprings) s.squash = std::max(0.f, s.squash - dt * 1.6f);
    if (launched_ && vy_ <= 0 && baseY_ <= supportY() + 1e-4) launched_ = false; // (down: on a surface again)
    if (launched_ && vy_ > 1 && rng_.next01() < dt * 50.f) { // wind streaking off her as she rises
        const float top = float(baseY_) + eggs_.size() * Uf + 0.5f;
        fxAdd_.emit({float(x_) + rng_.range(-0.3f, 0.3f), rng_.range(float(baseY_), top), rng_.range(-0.4f, 0.4f)},
                    {-speed_ * 0.25f, -float(vy_) * 0.35f, 0}, rng_.range(0.15f, 0.3f), 0.05f, 0, mint() * 1.6f, 0.8f, 0.5f, 0);
    }
    if (state_ != State::Play || gIn_.g) return;
    for (Spring& s : level_.springs) {
        if (s.x > x_ + 1) break;
        // her centre over its middle, her feet (her stack's foot) on its plate, not already flying up
        if (!s.fired && x_ >= s.x - 0.05 && x_ < s.x + 0.6 && std::abs(baseY_ - s.floor * U) < 0.06 && vy_ <= 0) launchSpring(s);
    }
}

void Game::launchSpring(Spring& s) {
    s.fired = true; s.squash = 1;
    vy_ = rules::springLaunch(s.power);
    baseY_ += 1e-3; // (off the plate: in the air from now on)
    launched_ = true;
    springFx(s);
}

void Game::springFx(Spring& s) {
    s.squash = 1;
    ++springsFired_;
    squash_ = -0.35f; flap_ = 1;
    sfx(Sfx::Bounce, 2); buzz(25);
    shake_ = std::max(shake_, 0.12f);
    chromaKick_ = std::max(chromaKick_, 0.35f);
    const glm::vec3 pad{float(s.x), s.floor * Uf + 0.08f, 0};
    burst(pad, 26, {lime() * 3.f, mint() * 2.6f, glow("#ffffff", 2.f)}, 2.f, 5.f, 0.45f, 0.06f, 1.2f, 0.f, false, 2.5f, 3);
    rings_.spawn(pad, lime(), 1.4f, 0.4f, false);
    rings_.spawn(pad + glm::vec3(0, 0.3f, 0), mint(), 0.9f, 0.3f, false);
    puff(pad, 6, hexColor("#5a4a3a"), 1.2f, 0.4f);
    for (size_t i = 0; i < eggs_.size(); ++i) { eggs_[i].jig = 1; eggs_[i].jd = i * 0.03f; }
}

void Game::emitSprings(RenderList& out, float pz) {
    std::vector<const Spring*> pads; // (the course's, and a maze's: both in course order)
    for (const Spring& s : level_.springs) pads.push_back(&s);
    for (const Spring& s : level_.gsprings) pads.push_back(&s);
    for (const Spring* sp : pads) {
        const Spring& s = *sp;
        if (s.x < camX_ - 10 || s.x > camX_ + 40) continue;
        const float x = float(s.x), y = s.floor * Uf;
        // the plate's travel: crushed as it fires, overshooting high, then a damped wobble back to rest
        const float t = 1.f - s.squash; // 0 .. 1 since it fired
        const float fire = s.squash > 0 ? (t < 0.06f ? -0.75f * (t / 0.06f) : std::exp(-5.f * t) * std::cos((t - 0.06f) * 24.f) * 0.9f) : 0.f;
        const float coilH = std::max(0.07f, kCoilH * (1.f + fire));
        // the base: a dark steel frame bolted to the surface, lime trim along its top edge
        LitMaterial steel; steel.color = hexColor("#2b2f3a"); steel.roughness = 0.4f; steel.metalness = 0.8f;
        steel.rimColor = lime() * 0.6f; steel.rimStrength = 0.8f; steel.rimPow = 3.f;
        out.add(Pass::Lit, MeshId::Box) = makeLit(compose({x, y + 0.04f, 0}, {kPadW + 0.12f, 0.08f, DEPTH * 0.62f}), steel, x);
        out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose({x, y + 0.085f, DEPTH * 0.31f + 0.005f}, {kPadW + 0.12f, 0.012f, 0.01f}), lime() * 1.2f);
        // the coil: steel rings stacked from the base to the plate, a little apart, alternately tilted (a helix), its
        // core dark, its rims lit lime as it fires
        LitMaterial coil; coil.color = hexColor("#d9e2ef"); coil.roughness = 0.18f; coil.metalness = 1.f;
        coil.rimColor = lime() * (1.f + s.squash * 2.f); coil.rimStrength = 1.4f; coil.rimPow = 2.2f;
        LitMaterial core; core.color = hexColor("#0b0d14"); core.roughness = 0.9f;
        out.add(Pass::Lit, MeshId::Cylinder) = makeLit(compose({x, y + 0.08f + coilH * 0.5f, 0}, {0.16f, coilH, 0.16f}), core, x);
        const int turns = 6;
        for (int i = 0; i < turns; ++i) {
            const float h = y + 0.08f + coilH * (i + 0.5f) / turns;
            out.add(Pass::Lit, MeshId::EggRing) = makeLit(compose({x, h, 0}, {kPi / 2 + 0.16f * (i % 2 ? 1 : -1), 0, 0}, {1.55f, 1.55f, 2.2f}), coil, x + i);
        }
        // the plate: lime glass over a dark rim, its chevrons climbing its top toward where she flies (brighter, faster
        // as she nears)
        const float py = y + 0.08f + coilH;
        const float near = std::clamp(1.f - float(std::abs(s.x - x_)) / 8.f, 0.f, 1.f);
        const float pulse = 0.7f + 0.3f * std::sin(time_ * (5.f + near * 7.f)) + s.squash;
        out.add(Pass::Lit, MeshId::Box) = makeLit(compose({x, py + 0.03f, 0}, {kPadW + 0.06f, 0.06f, DEPTH * 0.6f}), steel, x);
        LitMaterial plate; plate.color = lime() * 0.35f; plate.roughness = 0.3f; plate.emissive = lime() * (0.25f + 0.25f * pulse);
        plate.rimColor = mint(); plate.rimStrength = 1.6f; plate.rimPow = 1.8f;
        out.add(Pass::Lit, MeshId::Box) = makeLit(compose({x, py + 0.075f, 0}, {kPadW * 0.92f, 0.04f, DEPTH * 0.52f}), plate, x);
        for (int c = 0; c < 3; ++c) { // chevrons on its top, pointing on and up, sliding forward
            const float ph = std::fmod(time_ * (0.9f + near * 1.2f) + c / 3.f, 1.f);
            const float cx = x - kPadW * 0.32f + ph * kPadW * 0.64f, a = std::sin(ph * kPi) * (0.6f + near * 0.6f) * pulse;
            for (int k = 0; k < 2; ++k)
                out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose({cx, py + 0.1f, (k ? 1.f : -1.f) * DEPTH * 0.09f}, {0, k ? -0.7f : 0.7f, 0}, {0.05f, 0.012f, DEPTH * 0.24f}),
                                                               mint() * 2.6f * a);
        }
        // its glow on the walls behind, and up under her as she flies off it
        out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose({x, py + 0.3f, DEPTH * 0.5f + 0.02f}, {1.8f + s.squash, 1.6f + s.squash * 2.f, 1}),
                                                           lime() * (0.35f + 0.2f * pulse + pz * 0.2f), 0.45f, Shape::RadialGlow);
        if (s.squash > 0) // the launch's column of light
            out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose({x, py + 1.4f, 0.3f}, {0.6f, 2.8f * s.squash + 0.4f, 1}), mint() * 1.4f * s.squash, 0.6f, Shape::BottomFade);
    }
}

} // namespace cs
