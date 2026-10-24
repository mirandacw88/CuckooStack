// Lava (Level.h's Lava; the hazard's row: hazards/Lava.cpp): a stretch of floor molten and churning, dark crust
// drifting on it, bubbles popping, embers and heat rising. On it the egg under her stack burns away a cell at a time
// (fast: an egg every 0.6 m): it glows as she crosses the cell, then drops into the lava, charring black, flaming,
// and crumbles to ash in a puff of smoke; the stack drops onto the next. With no egg under her, the hen catches fire,
// chars and crumbles to ash (Hen::burn). A low sizzle plays as one comes up ahead.
#include "Game.h"

#include <algorithm>
#include <cmath>

namespace cs {

namespace {

glm::vec3 molten() { return hexColor("#ff5a14"); }
glm::vec3 hot() { return hexColor("#ffc23a"); }

} // namespace

bool Game::debugLava(double ahead, int cells) {
    if (state_ != State::Play) return false;
    Lava f{x_ + ahead, x_ + ahead + cells * U, level_.heightAt(x_ + ahead)};
    level_.lavas.insert(std::lower_bound(level_.lavas.begin(), level_.lavas.end(), f.x0, [](const Lava& a, double v) { return a.x0 < v; }), f);
    return true;
}

void Game::updateLava(float dt) {
    // on screen: bubbles popping, embers rising off it
    for (Lava& f : level_.lavas) {
        if (f.x1 < camX_ - 10) continue;
        if (f.x0 > camX_ + 26) break;
        const float len = float(f.x1 - f.x0), y = f.floor * Uf + 0.06f;
        if (rng_.next01() < dt * 9.f * len) { // a bubble swelling and popping: a splash of glowing drops
            const glm::vec3 p{float(f.x0) + rng_.range(0.f, len), y, rng_.range(-DEPTH * 0.4f, DEPTH * 0.4f)};
            for (int i = 0; i < 6; ++i)
                fxAdd_.emit(p, {rng_.range(-0.8f, 0.8f), rng_.range(1.2f, 2.6f), rng_.range(-0.8f, 0.8f)}, rng_.range(0.25f, 0.5f),
                            rng_.range(0.03f, 0.06f), 0.01f, i % 2 ? molten() * 3.f : hot() * 3.f, 1, 1.f, 9);
        }
        if (rng_.next01() < dt * 14.f * len) // embers drifting up
            fxAdd_.emit({float(f.x0) + rng_.range(0.f, len), y, rng_.range(-DEPTH * 0.45f, DEPTH * 0.45f)},
                        {rng_.range(-0.2f, 0.2f), rng_.range(0.8f, 2.f), rng_.range(-0.2f, 0.2f)}, rng_.range(0.6f, 1.2f), rng_.range(0.02f, 0.04f),
                        0.f, glow("#ff8a2b", 3.f), 1, 0.6f, -1.5f);
    }
    // the warning: its sizzle, a second or so out
    for (Lava& f : level_.lavas) {
        if (f.x0 > x_ + 40) break;
        const double rel = gDir_ * (x_ - (gDir_ > 0 ? f.x0 : f.x1));
        if (!f.warned && rel < 0 && rel > -(speed_ * 1.1 + 2)) { f.warned = true; sfx(Sfx::LavaSizzle); }
    }
    // the cell under her: a new one burns an egg (with none, her); the next egg heats as she crosses it
    // (her front edge: the stack's foot comes onto each cell as it does; as the solver has it, before the egg laid for
    // the next)
    const double front = x_ + gDir_ * 0.22;
    const Lava* on = nullptr;
    for (const Lava& f : level_.lavas) {
        if (f.x0 > front + 1) break;
        if (front >= f.x0 && front < f.x1 && std::abs(baseY_ - f.floor * U) < 0.05) { on = &f; break; }
    }
    if (!on || state_ != State::Play) {
        lavaAt_ = -1; lavaCell_ = -1;
        for (Egg& e : eggs_) e.burn = 0;
        return;
    }
    const int cell = std::clamp(static_cast<int>(std::floor((front - on->x0) / U)), 0, on->cells() - 1);
    if (on->x0 != lavaAt_ || cell != lavaCell_) {
        lavaAt_ = on->x0;
        lavaCell_ = cell;
        if (smashing()) return; // (OVERDRIVE: it can't touch her)
        if (eggs_.empty()) { burnHen(*on); return; }
        burnEgg(*on);
    }
    if (!eggs_.empty()) { // the next egg down heats through the cell; flames lick up round the stack's foot
        const double into = gDir_ > 0 ? front - (on->x0 + cell * U) : (on->x0 + (cell + 1) * U) - front;
        eggs_.front().burn = std::clamp(float(into / U), 0.f, 1.f);
    }
    if (rng_.next01() < dt * 40.f)
        fxAdd_.emit({float(x_) + rng_.range(-0.25f, 0.25f), float(baseY_) + 0.05f, rng_.range(-0.2f, 0.2f)},
                    {rng_.range(-0.5f, 0.5f) - gDir_ * speed_ * 0.2f, rng_.range(1.5f, 3.f), rng_.range(-0.3f, 0.3f)}, rng_.range(0.2f, 0.4f),
                    rng_.range(0.08f, 0.14f), 0.f, rng_.next01() < 0.5f ? molten() * 3.f : hot() * 3.f, 1, 1.2f, -2.f);
}

// The egg under her burns: it drops into the lava, charring, flaming, crumbling to ash; the stack drops onto the next
void Game::burnEgg(const Lava& f) {
    const Egg e = eggs_.front();
    eggs_.erase(eggs_.begin());
    Debris d{DebrisKind::Burnt, e.pos, e.rot, e.scale, {gDir_ * speed_ * 0.1f, -0.6f, rng_.range(-0.3f, 0.3f)},
             {rng_.range(-2.f, 2.f), rng_.range(-2.f, 2.f), rng_.range(-2.f, 2.f)}, 0.9f, float(f.floor) * Uf, e.ring};
    debris_.push_back(d);
    const glm::vec3 at{float(x_), f.floor * Uf + 0.1f, 0.f};
    burst(at, 22, {molten() * 3.2f, hot() * 3.f, glow("#fff1c8", 2.6f)}, 1.5f, 4.f, 0.35f, 0.07f, 1.2f, -gDir_ * 0.5f, false, 2.f, 4);
    puff(at, 4, hexColor("#3a3150"), 0.8f, 0.5f);
    sfx(Sfx::LavaBurn, 0); buzz(12);
    shake_ = std::max(shake_, 0.08f);
    sawDrop_ = Uf; sawDropV_ = 0; // the stack (or the hen) drops onto what's left
    if (eggs_.empty()) closeCall(float(baseY_) + 0.2f, "CLOSE SHAVE"); // (it took her last egg, right under her)
}

// No egg under her: the hen catches fire
void Game::burnHen(const Lava& f) {
    const glm::vec3 at{float(x_), f.floor * Uf + 0.3f, 0.f};
    burst(at, 50, {molten() * 3.4f, hot() * 3.2f, glow("#fff1c8", 3)}, 2, 6, 0.6f, 0.08f, 2.f, 0, false, 1.5f, -2);
    sfx(Sfx::LavaBurn, 1);
    die(false, Death::Burned);
}

void Game::emitLava(RenderList& out, float pz) {
    for (const Lava& f : level_.lavas) {
        if (f.x1 < camX_ - 10) continue;
        if (f.x0 > camX_ + 40) break;
        const float x0 = float(f.x0), x1 = float(f.x1), y = f.floor * Uf;
        // the molten surface: a glowing slab over the floor, its colour churning cell by cell
        const int n = std::max(1, static_cast<int>(std::lround((x1 - x0) / (Uf * 0.5f))));
        const float w = (x1 - x0) / float(n);
        for (int i = 0; i < n; ++i) {
            const float cx = x0 + (i + 0.5f) * w;
            const float churn = 0.5f + 0.5f * std::sin(time_ * 2.3f + cx * 4.1f) * std::sin(time_ * 1.3f - cx * 2.7f);
            const glm::vec3 c = glm::mix(molten(), hot(), churn * 0.7f) * (2.2f + 0.8f * churn + pz * 0.4f);
            out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose({cx, y + 0.035f, 0}, {w * 1.01f, 0.07f, DEPTH * 0.98f}), c);
        }
        // dark crust drifting on it, cracked, glowing at its edges
        LitMaterial crust; crust.color = hexColor("#1a0f0b"); crust.roughness = 0.95f;
        crust.rimColor = molten() * 1.5f; crust.rimStrength = 1.4f; crust.rimPow = 2.f;
        const int plates = std::max(1, static_cast<int>((x1 - x0) / 0.45f));
        for (int i = 0; i < plates; ++i) {
            const float seed = std::fmod(x0 * 3.7f + i * 1.618f, 1.f);
            const float px = x0 + std::fmod(seed * (x1 - x0) + time_ * 0.07f * (seed - 0.5f), x1 - x0 - 0.15f) + 0.08f;
            const float pzz = (std::fmod(seed * 7.3f, 1.f) - 0.5f) * DEPTH * 0.7f;
            const float s = 0.1f + 0.12f * std::fmod(seed * 13.1f, 1.f);
            out.add(Pass::Lit, MeshId::Box) = makeLit(compose({px, y + 0.075f, pzz}, {0, seed * 6.f + time_ * 0.1f, 0}, {s * 1.4f, 0.025f, s}), crust, px);
        }
        // its glow: under it on the walls and the hen, and a heat haze over it
        const float pulse = 0.85f + 0.15f * std::sin(time_ * 3.f + x0);
        out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose({(x0 + x1) * 0.5f, y + 0.6f, DEPTH * 0.5f + 0.02f}, {(x1 - x0) * 1.6f + 1.f, 2.2f, 1}),
                                                           molten() * 1.1f * pulse, 0.55f, Shape::RadialGlow);
        out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose({(x0 + x1) * 0.5f, y + 0.08f, 0}, {-kPi / 2, 0, 0}, {(x1 - x0) * 1.3f + 0.6f, DEPTH * 1.6f, 1}),
                                                           hot() * 0.9f * pulse, 0.6f, Shape::RadialGlow);
    }
}

// An egg burning in the lava: sinking into it as it chars black, embers glowing in its cracks, flames licking it, a
// puff of smoke as it crumbles
void Game::emitBurnt(RenderList& out, const Debris& d) {
    const float t = 1.f - d.life / 0.9f; // 0 .. 1 through its burning
    const float charred = glm::smoothstep(0.f, 0.55f, t);
    const float ember = (0.6f + 0.4f * std::sin(time_ * 31.f + d.pos.x * 7.f)) * (1.f - glm::smoothstep(0.6f, 1.f, t));
    LitMaterial m; m.color = glm::mix(hexColor("#f6f3ff"), hexColor("#120c0a"), charred); m.roughness = glm::mix(0.28f, 1.f, charred);
    m.emissive = glm::mix(glm::vec3(0.12f), molten() * 1.8f * ember, charred);
    m.rimColor = molten() * (1.f + ember); m.rimStrength = 1.8f; m.rimPow = 1.8f;
    const float shrink = 1.f - glm::smoothstep(0.7f, 1.f, t) * 0.8f;
    const glm::mat4 g = compose(d.pos, d.rot, d.scale * glm::vec3(1.1f * shrink, 1.f * shrink * (1.f - 0.35f * t), 1.1f * shrink));
    out.add(Pass::Lit, MeshId::Egg) = makeLit(g, m);
    out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose(d.pos + glm::vec3(0, 0.1f, 0.3f), glm::vec3(0.7f)), molten() * 1.6f * ember, 0.7f, Shape::RadialGlow);
}

} // namespace cs
