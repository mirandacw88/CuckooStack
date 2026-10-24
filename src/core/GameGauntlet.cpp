// Maze gauntlets (Gauntlet.h; Level lays them on the course): the hen's run inside one, and how it looks.
// Inside, she runs on her own at the run's full speed, turning back only off bumpers (and gates set against her). As
// her front edge reaches each new column of the grid, what's there decides it, by the rules the solver checks (and in
// the same order): anything in her way at her body bounces her (a bumper, a gate against her) or crashes her (any
// other wall); anything in her way at her stack is a step she climbs if it rises from her floor (the eggs below its top
// knocked off, as on any wall), else it crashes her. She drops straight down holes (out of the run's flow till she
// lands; gates are no floor), and stacking into a ceiling crashes her as under a barrier. Saws work as on the run,
// either way. The entry's door shuts behind her (it bounces her) and the exit's opens onto a porch, where the run
// carries on heading right. No OVERDRIVE inside, no disco balls, no droid raids.
#include "Game.h"

#include <algorithm>
#include <cmath>

namespace cs {

namespace {
constexpr double kHb = 0.22; // half her footprint (as on the run)
// the game's neon palette (Game.cpp's C())
glm::vec3 cyan() { return glow("#29e7ff", 3); }
glm::vec3 white() { return glow("#ffffff", 2.4f); }
glm::vec3 volt() { return glow("#f4ff5a", 3); }
glm::vec3 ember() { return glow("#ff6a2b", 3); }
glm::vec3 steam() { return hexColor("#8a78b0"); }
}

void Game::enterGauntlet(const PlacedGauntlet& p) {
    gIn_ = p;
    gDir_ = 1;
    gFly_ = false;
    gMoves_ = 0; gBotLay_ = -99; gBounced_ = false;
    if (surging_) endSurge();
    graceT_ = 0; // (no smashing through a maze)
    // the droid peels off; raids due inside wait till she's out
    if (raid_.on && raid_.leaving < 0) raid_.leaving = 0, raid_.leaveFrom = droidPose().pos - glm::vec3(float(x_), 0.f, 0.f);
    for (double& r : raidPlan_) if (r >= p.x0 - 5 && r < p.x1() + 10) r = p.x1() + 10;
    if (p.g->isZone()) return; // (a bounce zone on the course: no door, no banner)
    // the door slams shut behind her
    sfx(Sfx::Bounce, 1); buzz(30);
    shake_ = std::max(shake_, 0.22f);
    const glm::vec3 door{float(p.x0), float(baseY_) + 0.6f, 0.4f};
    puff(door, 10, hexColor("#5a4a3a"), 1.4f, 0.5f);
    burst(door, 24, {glow("#ff2a3a", 2.5f), glow("#ffb43a", 2.5f)}, 1.5f, 4, 0.5f, 0.06f, 0.6f, 0, false, 2.f, 3);
    popup(gauntletTitle(p.g->name()), {float(x_) + 2.5f, float(baseY_) + 2.6f, 0}, PopKind::Sector);
}

void Game::endSqueeze() {
    if (gSqueezeFrom_ >= 0 && gSqueezeCols_ >= 2 && nearMissT_ < gSqueezeFrom_ && state_ == State::Play)
        closeCall(float(baseY_ + eggs_.size() * U + HEN_H), "CLOSE CALL");
    gSqueezeFrom_ = -1; gSqueezeCols_ = 0;
}

void Game::leaveGauntlet() {
    endSqueeze();
    const glm::vec3 door{float(gIn_.x1()), float(baseY_) + 0.8f, 0.3f};
    const bool zone = gIn_.g->isZone();
    gIn_ = {};
    gDir_ = 1;
    gFly_ = false;
    if (zone) { // out of a bounce zone: a spark of light, on she runs
        rings_.spawn(door, glow("#ff2bd6", 2.5f), 1.6f, 0.4f, true);
        burst(door, 20, {glow("#ff2bd6", 3.f), volt()}, 1.5f, 4, 0.5f, 0.06f, 0.6f, 2.f, false, 2.5f, 3);
        return;
    }
    popup("ESCAPED!", door + glm::vec3(1.5f, 1.6f, 0.f), PopKind::Surge);
    sfx(Sfx::SurgeStart); buzz(25);
    rings_.spawn(door, volt(), 2.6f, 0.6f, true);
    burst(door, 50, {volt(), cyan(), white()}, 2, 7, 0.8f, 0.09f, 0.8f, 2.f, false, 2.5f, 4);
    flashScreen(srgbColor("#f4ff5a"), 0.2f);
}

void Game::gauntletBounce(int c, int r) {
    gDir_ = -gDir_;
    gBounced_ = true; // (the move was counted as her front edge came into the cell: a bounce is one solver step, not two)
    squash_ = 0.25f;
    gFlash_ = 1.f;
    gFlashAt_ = {float(gIn_.x0 + (c + 0.5) * U), float((r + 0.5) * U), 0.f};
    sfx(Sfx::Bounce); buzz(10);
    const glm::vec3 hit{float(x_) - gDir_ * 0.05f, float(baseY_ + eggs_.size() * U) + 0.6f, 0.3f};
    rings_.spawn(hit, glow("#ff2bd6", 2.5f), 1.f, 0.3f, true);
    burst(hit, 14, {glow("#ff2bd6", 3.f), white()}, 1.5f, 4, 0.3f, 0.06f, 0.3f, gDir_ * 2.f, false, 2.5f, 2);
}

Spring* Game::mazeSpringAt(int col, int row) {
    const double x = gIn_.x0 + (col + 0.5) * U;
    for (std::vector<Spring>* v : {&level_.gsprings, &level_.springs})
        for (Spring& s : *v)
            if (s.floor == row + 1 && std::abs(s.x - x) < U / 2 + 1e-6) return &s;
    return nullptr;
}

// A spring cell under her middle: she's launched. The flight is the solver's (Gauntlet::flightFrom: the arc, the walls
// and roofs, the bounces and the landing worked out ahead), played out in time, so she comes down where the solver says.
void Game::launchGauntlet(int col, int row, double over) {
    const Gauntlet& g = *gIn_.g;
    gPlan_ = g.flightFrom(col, row + 1, static_cast<int>(eggs_.size()), gDir_);
    gFly_ = true;
    gFlyT_ = over;
    gFlyBounce_ = 0;
    launched_ = true;
    endSqueeze();
    if (Spring* s = mazeSpringAt(col, row)) springFx(*s); else { ++springsFired_; sfx(Sfx::Bounce, 2); }
    x_ = gIn_.x0 + gPlan_.xAt(over) * U;
    baseY_ = gPlan_.rowAt(over) * U;
    vy_ = gPlan_.vyAt(over);
    baseY_ = std::max(baseY_, (row + 1) * U + 1e-3);
    gBotLay_ = -99;
}

void Game::flyGauntlet(float dt) {
    const Gauntlet::Flight& f = gPlan_;
    gFlyT_ += dt;
    const double t = std::min(gFlyT_, f.seconds);
    while (gFlyBounce_ < f.bounces.size() && f.bounces[gFlyBounce_].t <= t) { // a bumper or a gate against her, in the air
        const Gauntlet::Flight::Bounce& b = f.bounces[gFlyBounce_++];
        x_ = gIn_.x0 + f.xAt(b.t) * U;
        baseY_ = f.rowAt(b.t) * U;
        gauntletBounce(b.c, b.r);
    }
    x_ = gIn_.x0 + f.xAt(t) * U;
    baseY_ = f.rowAt(t) * U;
    vy_ = f.vyAt(t);
    if (gFlyT_ < f.seconds) return;
    gFly_ = false;
    if (f.end != Gauntlet::Flight::End::Landed) { // into a wall or a roof (the solver's crash)
        launched_ = false;
        die(f.end == Gauntlet::Flight::End::Ceiling);
        return;
    }
    // down: on the floor the plan landed her on, as the ground rules find it (floorUnder)
    baseY_ = f.floor * U;
    if (vy_ < -4) {
        squash_ = 0.24f; sfx(Sfx::Land); shake_ = std::max(shake_, 0.12f);
        puff({float(x_), float(baseY_) + 0.05f, 0}, 7, steam(), 1.6f, 0.45f);
        rings_.spawn({float(x_), float(baseY_) + 0.02f, 0}, cyan(), 1.2f, 0.35f, false);
        for (size_t i = 0; i < eggs_.size(); ++i) { eggs_[i].jig = 1; eggs_[i].jd = i * 0.035f; }
    }
    vy_ = 0;
    launched_ = false;
    gDir_ = f.dir;
    gBounced_ = false;
    gMoves_ += Gauntlet::kLayGap + 1;
    gBotLay_ = -99;
}

void Game::updateGauntlet(float dt) {
    const Gauntlet& g = *gIn_.g;
    if (gFly_) { flyGauntlet(dt); return; }
    // the floor under her: over either side of her footprint, the highest solid top at or below her stack's base
    auto floorUnder = [&](double x) {
        double best = 0;
        for (double fx : {x - kHb, x + kHb}) {
            const int c = gIn_.col(fx);
            for (int r = std::min(g.rows(), static_cast<int>(std::floor(baseY_ / U + 0.05))) - 1; r >= -1; --r)
                if (r < 0 || g.solid(c, r)) { best = std::max(best, (r + 1) * U); break; }
        }
        return best;
    };
    const bool airborne = baseY_ > floorUnder(x_) + 1e-4;
    if (!airborne) {
        double nx = x_ + gDir_ * speed_ * dt;
        // over a spring: launched as her middle reaches the cell's middle (she's run in to it first: the rules of the column
        // her front edge came into have been met, as the solver has it)
        if (vy_ == 0) {
            const int mc = gIn_.col(x_), mr = static_cast<int>(std::lround(baseY_ / U));
            if (mc >= 0 && mc < g.cols() && g.springAt(mc, mr - 1)) {
                const double xs = gIn_.x0 + (mc + 0.5) * U;
                if (gDir_ * (nx - xs) >= 0) { // (reached it this frame, or already on it: the rest of the frame is in the air)
                    const double over = std::max(0.0, gDir_ * (nx - xs)) / std::max(1.f, speed_);
                    x_ = xs;
                    launchGauntlet(mc, mr - 1, over);
                    return;
                }
            }
        }
        // stepping off a ledge, she drops in the first cell past it, as the solver has her: a frame that takes her
        // off it stops where her back edge leaves it (her front edge, kHb * 2 on, still in that cell), so her front
        // edge never reaches the cell after it before she's dropped (with a lower roof there, it would crash her)
        if (baseY_ > floorUnder(nx) + 1e-4) {
            const double back = x_ - gDir_ * kHb;
            const double edge = gIn_.x0 + (gIn_.col(back) + (gDir_ > 0 ? 1 : 0)) * U; // the line her back edge crosses
            const double leave = edge + gDir_ * (kHb + 1e-6);
            if (gDir_ * (leave - x_) > 0 && gDir_ * (nx - leave) > 0) nx = leave;
        }
        const int c = gIn_.col(x_ + gDir_ * kHb), a = gIn_.col(nx + gDir_ * kHb);
        if (a != c) { // her front edge reaches the next column: what's there
            ++gMoves_;
            gBounced_ = false;
            const int b = static_cast<int>(std::lround(baseY_ / U)), feet = b + static_cast<int>(eggs_.size());
            for (int r = feet; r < feet + Gauntlet::kBodyRows; ++r)
                if (g.blocks(a, r, gDir_)) {
                    if (g.bounces(a, r, gDir_)) { gauntletBounce(a, r); return; }
                    die(false); // (a wall at her body)
                    return;
                }
            int top = b;
            while (top < feet && g.blocks(a, top, gDir_)) ++top;
            for (int r = top; r < feet; ++r)
                if (g.blocks(a, r, gDir_)) { die(true); return; } // (in the way of her stack, not rising from her floor)
            if (top > b) { // a step she climbs: the eggs below its top knocked off, as on any wall
                const int k = top - b;
                if (k == static_cast<int>(eggs_.size())) perfect(float(top * U)); else streak_ = 0;
                knock(k);
                baseY_ = top * U; vy_ = 0;
                for (int i = 0; i < 12; ++i)
                    fxAdd_.emit({float(x_) + gDir_ * 0.3f, float(baseY_) + 0.02f, rng_.range(-0.6f, 0.6f)},
                                {-gDir_ * rng_.range(1.f, 3.f), rng_.range(0.5f, 2.f), rng_.range(-0.5f, 0.5f)}, rng_.range(0.2f, 0.4f), 0.07f, 0,
                                i % 2 ? cyan() : ember(), 1, 3, 8);
            }
            // a squeeze: her head brushing what's over her in the column she's coming into. When she has room again,
            // after two columns or more, it was a CLOSE CALL (unless a near miss already came in it: a PERFECT, a
            // CLOSE SHAVE)
            const int head = static_cast<int>(std::lround(baseY_ / U)) + static_cast<int>(eggs_.size()) + Gauntlet::kBodyRows;
            if (g.solid(a, head)) {
                if (gSqueezeFrom_ < 0) { gSqueezeFrom_ = time_; gSqueezeCols_ = 0; }
                ++gSqueezeCols_;
            } else {
                endSqueeze();
            }
        }
        x_ = nx;
    }
    // down a hole: straight down (her way through is held till she lands)
    const double floor = floorUnder(x_);
    if (baseY_ > floor + 1e-4) {
        if (vy_ == 0) endSqueeze(); // (dropping away from what was over her)
        vy_ -= 36 * dt; baseY_ += vy_ * dt;
    }
    if (vy_ != 0 && baseY_ <= floor + 1e-4) { // landed (a step that ends a hair above the floor counts: else she'd never stop falling)
        if (vy_ < -4) {
            squash_ = 0.24f; sfx(Sfx::Land); shake_ = std::max(shake_, 0.12f);
            puff({float(x_), float(floor) + 0.05f, 0}, 7, steam(), 1.6f, 0.45f);
            rings_.spawn({float(x_), float(floor) + 0.02f, 0}, cyan(), 1.2f, 0.35f, false);
            for (size_t i = 0; i < eggs_.size(); ++i) { eggs_[i].jig = 1; eggs_[i].jd = i * 0.035f; }
        }
        baseY_ = floor; vy_ = 0;
    }
    // stacking into a ceiling crashes her, as under a barrier (over where she is and where she's running: not the side
    // she's leaving, a door or a wall's edge behind her)
    const int feet = static_cast<int>(std::lround(baseY_ / U)) + static_cast<int>(eggs_.size());
    for (double fx : {x_, x_ + gDir_ * kHb}) {
        const int c = gIn_.col(fx);
        if (c < 0 || c >= g.cols()) continue; // (the doorways: the door's frame isn't a ceiling)
        for (int r = feet; r < feet + Gauntlet::kBodyRows; ++r)
            if (g.solid(c, r)) { die(true); return; }
    }
    // out through the exit: the run carries on
    if (x_ - kHb >= gIn_.x1()) leaveGauntlet();
}

void Game::emitGauntlets(RenderList& out, float pz) {
    if (level_.gauntlets.empty() && level_.zones.empty()) return;
    LitMaterial stone, cube;
    float courses = 1.f;
    wallMaterials(pz, stone, cube, courses);
    stone.color *= 1.7f; // (the maze's walls a shade lighter than the run's, against its dark back wall)
    // its back wall: near black with the faintest lines, so the passages read as dark channels between lit walls
    LitMaterial back = stone;
    back.color = hexColor("#07060b"); back.emissive *= 0.05f; back.rimStrength = 0; back.metalness = 0.2f; back.roughness = 0.8f;
    LitMaterial bump; bump.color = hexColor("#160a20"); bump.metalness = 0.7f; bump.roughness = 0.3f; bump.pattern = Pattern::Stone;
    const glm::vec3 pink = hexColor("#ff2bd6");
    bump.emissive = glow("#ff2bd6", 1.f) * (1.2f + pz * 0.5f); bump.rimColor = pink; bump.rimStrength = 0.6f; bump.rimPow = 2.5f;
    const glm::vec3 cell{Uf * 0.985f, Uf * 0.985f, DEPTH};
    const float lo = camX_ - 14, hi = camX_ + 30;
    for (const glm::vec3& p : level_.gback)
        if (p.x > lo && p.x < hi)
            (out.add(Pass::Lit, MeshId::Box) = makeLit(compose(p - glm::vec3(0, 0, DEPTH / 2 + 0.06f), {Uf, Uf, 0.1f}), back, p.x)).color.a = 1.f;
    const float flash = gFlash_ * gFlash_;
    for (const Level::GCell& k : level_.gcells) {
        if (k.pos.x < lo || k.pos.x > hi) continue;
        switch (k.kind) {
        case Gauntlet::Cell::Bumper: {
            const float hit = glm::length(k.pos - gFlashAt_) < 0.1f ? flash : 0.f;
            LitMaterial m = bump;
            m.emissive = glm::mix(bump.emissive, glow("#ffffff", 3.f), hit);
            (out.add(Pass::Lit, MeshId::Box) = makeLit(compose(k.pos, cell), m, k.pos.x)).color.a = 1.f;
            // chevrons on its open sides, pointing the way it sends her
            for (int side = 0; side < 2; ++side) {
                if (!(k.open & (1 << side))) continue;
                const float dir = side == 0 ? -1.f : 1.f;
                const float beat = 0.6f + 0.4f * std::sin(time_ * 6.f + k.pos.y * 2.f) + hit * 2.f;
                for (int arm = -1; arm <= 1; arm += 2)
                    out.add(Pass::Lit, MeshId::Box) = makeEmissive(
                        compose(k.pos + glm::vec3(dir * 0.08f, arm * 0.07f, DEPTH / 2 + 0.012f), {0, 0, dir * arm * -0.8f}, {0.2f, 0.045f, 0.01f}),
                        glm::mix(pink, glm::vec3(1.f), 0.3f) * (2.4f * beat));
            }
            break;
        }
        case Gauntlet::Cell::CubeSaw:
        case Gauntlet::Cell::Cube:
            (out.add(Pass::Lit, MeshId::Box) = makeLit(compose(k.pos, cell), cube, k.pos.x)).color.a = courses;
            break;
        case Gauntlet::Cell::GateR:
        case Gauntlet::Cell::GateL: {
            // a one-way gate: a field of light she passes the way its arrows run (from the other side, a bumper)
            const float dir = k.kind == Gauntlet::Cell::GateR ? 1.f : -1.f;
            const glm::vec3 field = glow("#2bff9a", 1.f);
            out.add(Pass::UnlitAdd, MeshId::Box) = makeUnlit(compose(k.pos, {Uf * 0.22f, Uf, DEPTH * 0.9f}), field * 0.9f, 0.35f + 0.1f * pz);
            const float flow = std::fmod(time_ * 1.6f + k.pos.y * 0.7f, 1.f); // (the arrows run its way)
            for (int arm = -1; arm <= 1; arm += 2)
                out.add(Pass::Lit, MeshId::Box) = makeEmissive(
                    compose(k.pos + glm::vec3(dir * (flow - 0.5f) * Uf * 0.5f, arm * 0.06f, DEPTH / 2 + 0.012f), {0, 0, dir * arm * 0.8f}, {0.16f, 0.04f, 0.01f}),
                    field * (2.6f * (1.f - std::abs(flow - 0.5f))));
            break;
        }
        case Gauntlet::Cell::Lava: { // basalt glowing through its cracks (the molten top: emitLava)
            LitMaterial m = stone;
            m.color = hexColor("#2a140c");
            m.emissive = glow("#ff5a14", 0.45f + 0.15f * std::sin(time_ * 2.f + k.pos.x * 3.f));
            (out.add(Pass::Lit, MeshId::Box) = makeLit(compose(k.pos, cell), m, k.pos.x)).color.a = 1.f;
            break;
        }
        default:
            (out.add(Pass::Lit, MeshId::Box) = makeLit(compose(k.pos, cell), stone, k.pos.x)).color.a = 1.f;
            break;
        }
    }
    emitInscriptions(out, lo, hi);
    // the doors: the entry's, cyan and open till she's through, then shut behind her (red bars); the exit's, lit gold
    for (const PlacedGauntlet& p : level_.gauntlets) {
        if (p.x1() < lo || p.x0 > hi) continue;
        auto opening = [&](int c, int row) { int top = row; while (top < p.g->rows() && !p.g->solid(c, top)) ++top; return top; };
        const bool shut = gIn_.g == p.g || x_ > p.x1();
        const int et = opening(0, p.g->entryRow()), xt = opening(p.g->cols() - 1, p.g->exitRow());
        const float ey0 = p.g->entryRow() * Uf, ey1 = et * Uf, xy0 = p.g->exitRow() * Uf, xy1 = xt * Uf;
        const float pulse = 0.75f + 0.25f * std::sin(time_ * 5.f);
        const glm::vec3 entryCol = shut ? glow("#ff2a3a", 2.6f) : glow("#29e7ff", 2.4f) * pulse, exitCol = glow("#f4ff5a", 2.6f) * pulse;
        const float zf = DEPTH / 2 + 0.03f;
        out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose({float(p.x0) + Uf * 0.5f, ey1 + 0.03f, zf}, {Uf * 1.3f, 0.06f, 0.04f}), entryCol);
        out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose({float(p.x1()) - Uf * 0.5f, xy1 + 0.03f, zf}, {Uf * 1.3f, 0.06f, 0.04f}), exitCol);
        for (float side : {0.f, 1.f}) {
            out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose({float(p.x0) + side * Uf, (ey0 + ey1) / 2, zf}, {0.05f, ey1 - ey0, 0.04f}), entryCol);
            out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose({float(p.x1()) - side * Uf, (xy0 + xy1) / 2, zf}, {0.05f, xy1 - xy0, 0.04f}), exitCol);
        }
        if (shut) // the door's bars, across the opening
            for (int i = 0; i < 3; ++i)
                out.add(Pass::UnlitAdd, MeshId::Box) =
                    makeUnlit(compose({float(p.x0) + 0.02f, (ey0 + ey1) / 2, -0.4f + 0.4f * i}, {0.03f, ey1 - ey0, 0.03f}), glow("#ff2a3a", 2.f), 0.9f);
        out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose({float(p.x1()) - Uf * 0.5f, (xy0 + xy1) / 2, zf + 0.02f}, glm::vec3(2.2f)), exitCol * 0.4f,
                                                           0.5f * pulse, Shape::RadialGlow);
    }
}

// A tomb's inscriptions (Level::inscribe): neon hieroglyphs on its back walls, dim gold in the dark till she comes near,
// when they light up round her as if her lamp had woken them, and a glint runs down each column now and then. Some
// signs are inlaid turquoise; the stars are white gold; the cartouches, "Cuckoo", turquoise in a gold frame.
void Game::emitInscriptions(RenderList& out, float lo, float hi) {
    if (level_.gsigns.empty() || world_.zone() != Zone::Egypt || !fonts_.built()) return;
    lo = std::max(lo, camX_ - 13); hi = std::min(hi, camX_ + 13); // (the signs reach no further than the screen does)
    const glm::vec2 lamp{float(x_), float(baseY_) + float(eggs_.size()) * Uf + 0.55f};
    auto lit = [&](const glm::vec3& p) { // her light: full within a metre, gone by four (less far up and down)
        const float d = glm::length(glm::vec2(p.x - lamp.x, (p.y - lamp.y) * 1.3f));
        return smoothstep01((4.f - d) / 3.f);
    };
    const glm::vec3 gold = hexColor("#ffb43a"), turquoise = hexColor("#29e7ff"), star = hexColor("#ffe7b0");
    TextStyle st;
    st.font = FontId::Hieroglyphs;
    st.weight = 0.04f; // (the font's hairlines, filled out to neon tubes)
    for (const Level::GSign& s : level_.gsigns) {
        if (s.pos.x < lo || s.pos.x > hi) continue;
        const float l = lit(s.pos);
        const float glint = std::pow(std::max(0.f, std::sin(time_ * 1.4f - s.pos.y * 0.9f + s.seed * 40.f)), 24.f);
        const glm::vec3 c = s.kind == 1 ? star : s.kind == 2 || s.seed < 0.2f ? turquoise : gold;
        const float on = std::max(l, glint);
        st.color = c * (0.13f + 1.5f * l + 0.5f * glint);
        st.opacity = 0.45f + 0.55f * on;
        if (lite_) st.shadows.clear();
        else st.shadows = {{{0.f, 0.f}, 0.07f, c * (0.4f + 1.2f * l), 0.15f + 0.6f * on}};
        text_->drawGlyphFit(out.worldText, s.cp, st, {s.pos.x, s.pos.y}, s.kind == 1 ? 0.26f : 0.36f, compose({0.f, 0.f, s.pos.z}, glm::vec3(1.f)));
    }
    for (const Level::GRule& r : level_.grules) {
        if (r.pos.x < lo || r.pos.x > hi) continue;
        out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose(r.pos - glm::vec3(0, 0, 0.002f), glm::vec3(r.size, 1.f)), gold * (0.12f + 0.8f * lit(r.pos)), 0.5f);
    }
    for (const Level::GCartouche& k : level_.gcartouches) {
        if (k.pos.x < lo || k.pos.x > hi) continue;
        Instance frame = makeUnlit(compose(k.pos, glm::vec3(k.size, 1.f)), gold * (0.4f + 1.3f * lit(k.pos)), 0.9f, Shape::PillOutline, 0.06f);
        frame.params.x = k.size.x; frame.params.y = k.size.y;
        out.add(Pass::UnlitAlpha, MeshId::Plane) = frame;
    }
}

} // namespace cs
