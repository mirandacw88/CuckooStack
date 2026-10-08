// Surge mode: disco-chain trigger, wall smashing, pooled block debris and the party-mode visuals.
// Tuning values: Surge.h. Called from Game::update / updatePlay / buildRenderList.
#include "Game.h"
#include "Materials.h"

#include <cstdio>

namespace cs {

using namespace surge;

namespace {

const glm::vec3& kPink() { static const glm::vec3 c = glow("#ff2bd6", 3); return c; }
const glm::vec3& kCyan() { static const glm::vec3 c = glow("#29e7ff", 3); return c; }
const glm::vec3& kVolt() { static const glm::vec3 c = glow("#f4ff5a", 3); return c; }
const glm::vec3& kWhite() { static const glm::vec3 c = glow("#ffffff", 2.4f); return c; }

// laser palette: pink, cyan, yellow, green, violet, red-orange
glm::vec3 laserColor(int i) {
    static const glm::vec3 c[6] = {hexColor("#ff2bd6"), hexColor("#29e7ff"), hexColor("#f4ff5a"),
                                   hexColor("#2bff9a"), hexColor("#8a5bff"), hexColor("#ff5a2b")};
    return c[i % 6];
}

glm::vec3 hue(float h) { return hueColor(h); }

// A camera-facing quad spanning from `bottom` to `top` (mesh uv.y = 1 at `top`), `width` wide.
glm::mat4 beamMatrix(glm::vec3 top, glm::vec3 bottom, float width, glm::vec3 camPos) {
    const glm::vec3 c = (top + bottom) * 0.5f, axis = top - bottom;
    const glm::vec3 toCam = glm::normalize(camPos - c);
    glm::vec3 side = glm::cross(axis, toCam);
    const float sl = glm::length(side);
    side = sl > 1e-5f ? side / sl : glm::vec3(1, 0, 0);
    const glm::vec3 normal = glm::normalize(glm::cross(side, axis));
    glm::mat4 m(1.f);
    m[0] = glm::vec4(side * width, 0.f);
    m[1] = glm::vec4(axis, 0.f);
    m[2] = glm::vec4(normal, 0.f);
    m[3] = glm::vec4(c, 1.f);
    return m;
}

float easeOutCubic(float t) { t = clampf(t, 0.f, 1.f); return 1.f - (1.f - t) * (1.f - t) * (1.f - t); }

} // namespace

// ---------------------------------------------------------------- flow

void Game::resetSurge() {
    chain_ = 0; smashed_ = 0; surging_ = false; autoSurgeFired_ = false; surgeT_ = 0; graceT_ = 0; surgeSpeedMul_ = 1;
    partyK_ = 0; confettiAcc_ = 0; raveTimer_ = 0;
    for (SmashBlock& b : smashPool_) b.life = 0;
    for (PendingHaptic& h : haptics_) h.delay = -1;
    beat_.setParty(false);
    camera_.setFovBoost(0.f);
}

void Game::startSurge() {
    surging_ = true; surgeT_ = TIME; graceT_ = 0; smashed_ = 0;
    const glm::vec3 hp = hen_.nodes[hen_.root].pos;
    popup("SURGE!", hp + glm::vec3(0.6f, 1.9f, 0), PopKind::Surge);
    flashScreen(srgbColor("#ff2bd6"), 0.32f);
    rings_.spawn(hp + glm::vec3(0, 0.6f, 0.3f), kPink(), 3.2f, 0.6f, true);
    rings_.spawn(hp + glm::vec3(0, 0.6f, 0.3f), kCyan(), 2.2f, 0.5f, true);
    chromaKick_ = std::max(chromaKick_, 0.8f);
    sfx(Sfx::SurgeStart);
    svc_.audio->musicSurge(true);
    beat_.setParty(!muted_); // the synth only switches to the party pattern while music is audible
    queueHaptic(0.f, 20); queueHaptic(0.09f, 20); queueHaptic(0.18f, 20); queueHaptic(0.32f, 45);
}

void Game::endSurge() {
    surging_ = false; graceT_ = GRACE;
    const glm::vec3 hp = hen_.nodes[hen_.root].pos;
    popup("SMASHED " + std::to_string(smashed_) + " · +" + std::to_string(smashed_ * POINTS_PER_BLOCK), hp + glm::vec3(0.8f, 1.6f, 0),
          PopKind::Smashed);
    sfx(Sfx::SurgeEnd);
    svc_.audio->musicSurge(false);
    beat_.setParty(false);
}

void Game::queueHaptic(float delay, int ms) {
    for (PendingHaptic& h : haptics_)
        if (h.delay < 0) { h = {delay, ms}; return; }
}

// ---------------------------------------------------------------- smashing

int Game::collapseSegment(Segment& sg, int level) {
    if (sg.h <= level) return 0;
    const int cols = jsRound((sg.x1 - sg.x0) / U), rows = sg.h - level, removed = cols * rows;
    // sample the removed blocks evenly into the debris pool (same placement/look as Level::rebuildBlocks)
    const int n = std::min(DEBRIS_PER_SMASH, removed);
    for (int k = 0; k < n; ++k) {
        const int idx = static_cast<int>(int64_t(k) * removed / n);
        const int i = idx / rows, j = level + idx % rows;
        const double bx = sg.x0 + (i + 0.5) * U, by = (j + 0.5) * U;
        const bool hay = sg.kind == WallKind::Hay || (sg.kind == WallKind::Mix && j == sg.h - 1);
        spawnSmashBlock({float(bx), float(by), 0.f}, hay, hash(bx * 3.1 + j * 7.7) >= 0.5f, 0.82f + 0.18f * hash(bx * 9.3 + j * 1.9));
    }
    sg.h = level;
    return removed;
}

void Game::removeBarrier(Segment& sg) {
    const int cols = jsRound((sg.x1 - sg.x0) / U);
    const int n = std::min(DEBRIS_PER_SMASH, cols);
    for (int k = 0; k < n; ++k) {
        const int i = k * cols / n;
        spawnSmashBlock({float(sg.x0 + (i + 0.5) * U), float(sg.ceil * U) + 0.385f, 0.f}, true, false, 1.f);
    }
    smashFeedback({float(x_) + 0.5f, float(sg.ceil * U), 0.f}, cols);
    bonus_ += cols * POINTS_PER_BLOCK;
    smashed_ += cols;
    sg.ceil = 0;
}

void Game::smashAhead() {
    const double probe = x_ + SMASH_AHEAD;
    const int level = static_cast<int>(std::floor(baseY_ / U + LEVEL_EPS));
    const double henTop = baseY_ + eggs_.size() * U + HEN_H;
    bool changed = false;
    for (Segment& sg : level_.segs) {
        if (sg.x1 <= x_ - 0.3) continue;
        if (sg.x0 > probe + 0.05) break;
        // the column directly ahead: collapse the whole segment down to the hen's base level
        if (probe >= sg.x0 && probe < sg.x1 && sg.h > level) {
            const int removed = collapseSegment(sg, level);
            smashFeedback({float(std::max(sg.x0, x_ + 0.4)), float(level * U) + 0.5f, 0.f}, removed);
            bonus_ += removed * POINTS_PER_BLOCK;
            smashed_ += removed;
            changed = true;
        }
        // an overhead barrier she would hit, on the segment ahead or the one she is under
        if (sg.ceil && x_ + 0.25 + 0.15 >= sg.x0 && x_ + 0.22 < sg.x1 && henTop > sg.ceil * U + 0.02) {
            removeBarrier(sg);
            changed = true;
        }
    }
    if (changed) level_.rebuildBlocks();
}

void Game::smashFeedback(glm::vec3 at, int removed) {
    if (removed <= 0) return;
    burst(at, SMASH_PARTICLES, {kPink(), kVolt(), kCyan(), kWhite()}, 3, 9, 0.7f, 0.1f, 0.6f, speed_ * 0.4f, false, 2.2f, 6);
    rings_.spawn(at + glm::vec3(0, 0, 0.7f), kPink(), 2.4f, 0.4f, true);
    puff({at.x, std::max(0.05f, at.y - 0.5f), 0}, 5, hexColor("#3a3150"), 1.6f, 0.5f);
    shake_ = std::max(shake_, SMASH_SHAKE);
    camKick_ = std::max(camKick_, SMASH_KICK);
    freeze_ = std::max(freeze_, SMASH_FREEZE);
    chromaKick_ = std::max(chromaKick_, SMASH_CHROMA);
    sfx(Sfx::Smash);
    buzz(SMASH_HAPTIC_MS);
}

void Game::spawnSmashBlock(glm::vec3 pos, bool hay, bool flipped, float tint) {
    SmashBlock& b = smashPool_[size_t(smashNext_)];
    smashNext_ = (smashNext_ + 1) % DEBRIS_POOL;
    b.pos = pos - glm::vec3(0, 0, 0.35f); // start just behind the lane plane
    b.v = {speed_ * DEBRIS_FWD + rng_.range(DEBRIS_FWD_MIN, DEBRIS_FWD_MAX), rng_.range(DEBRIS_UP_MIN, DEBRIS_UP_MAX),
           rng_.range(-DEBRIS_SIDE, DEBRIS_TOWARD_CAMERA)};
    b.rot = glm::vec3(0.f);
    b.w = {rng_.range(-DEBRIS_SPIN, DEBRIS_SPIN), rng_.range(-DEBRIS_SPIN, DEBRIS_SPIN), rng_.range(-DEBRIS_SPIN, DEBRIS_SPIN)};
    b.life = rng_.range(DEBRIS_LIFE_MIN, DEBRIS_LIFE_MAX);
    b.hay = hay; b.flipped = flipped; b.tint = tint;
}

void Game::updateSmashDebris(float dt) {
    const float half = Uf * 0.5f;
    for (SmashBlock& b : smashPool_) {
        if (b.life <= 0) continue;
        b.life -= dt;
        b.v.y -= DEBRIS_GRAVITY * dt;
        b.pos += b.v * dt;
        b.rot += b.w * dt;
        const float floorY = std::abs(b.pos.z) < DEPTH / 2 ? level_.heightAt(b.pos.x) * Uf : 0.f;
        if (b.pos.y - half < floorY && b.v.y < 0) { // bounce on the floor / wall tops
            b.pos.y = floorY + half;
            b.v.y = -b.v.y * DEBRIS_BOUNCE;
            b.v.x *= 0.7f; b.v.z *= 0.7f; b.w *= 0.6f;
        }
    }
}

// ---------------------------------------------------------------- party mode

void Game::updateParty(float rdt) {
    // partyK: fast in on surge start, slower out afterwards
    const float target = surging_ ? 1.f : 0.f;
    const float tau = target > partyK_ ? PARTY_IN : PARTY_OUT;
    partyK_ += (target - partyK_) * (1.f - std::exp(-rdt / tau));
    if (partyK_ < 0.001f && !surging_) partyK_ = 0.f;
    partyTime_ += rdt;

    for (PendingHaptic& h : haptics_) {
        if (h.delay < 0) continue;
        h.delay -= rdt;
        if (h.delay <= 0) { buzz(h.ms); h.delay = -1; }
    }
    if (partyK_ <= 0.f) return;

    // rave disco floor: step every FLOOR_STEP instead of once per beat
    if (partyK_ > 0.5f) {
        raveTimer_ += rdt;
        while (raveTimer_ > FLOOR_STEP) { raveTimer_ -= FLOOR_STEP; discoStep_++; }
    }
    // confetti falling from above the screen
    confettiAcc_ += rdt * CONFETTI_PER_S * partyK_;
    while (confettiAcc_ > 1.f) {
        confettiAcc_ -= 1.f;
        fxAdd_.emit({camX_ + rng_.range(-9, 10), camY_ + rng_.range(6.5f, 8.5f), rng_.range(-3, 3)},
                    {rng_.range(-0.6f, 0.6f), rng_.range(-3.5f, -2), rng_.range(-0.4f, 0.4f)}, rng_.range(2.5f, 3.5f), 0.09f, 0.07f,
                    laserColor(rng_.index(6)) * 1.6f, 0.9f, 0.4f, 0.8f);
    }
    // afterimage trail streaming backward off the hen and her stack
    if (state_ == State::Play) {
        const glm::vec3 hp = hen_.nodes[hen_.root].pos;
        const int n = static_cast<int>(std::round(AFTERIMAGE_PER_FRAME * partyK_));
        for (int i = 0; i < n; ++i) {
            const float y = rng_.range(float(baseY_), hp.y + 1.f);
            fxAdd_.emit({hp.x - 0.2f, y, rng_.range(-0.15f, 0.15f)}, {-speed_ * 0.6f, 0, 0}, 0.35f, 0.26f, 0.05f,
                        hue(partyTime_ * HUE_SPEED + y * 0.15f) * 1.4f, 0.35f * partyK_, 0, 0);
        }
    }
}

void Game::emitParty(RenderList& out, float pz) {
    if (partyK_ <= 0.f) return;
    const float k = partyK_, t = partyTime_;
    const glm::vec3 cam = camera_.position();

    // giant mirror ball on a long chain: top of the screen, behind the lane, drops in as partyK rises
    const float drop = (1.f - easeOutCubic(k)) * GIANT_BALL_DROP;
    const glm::vec3 ball{camX_ + GIANT_BALL_AHEAD, camY_ + 0.1f + GIANT_BALL_HEIGHT + drop, GIANT_BALL_Z};
    const float r = 0.19f * 1.65f * GIANT_BALL_SCALE;
    {
        LitMaterial m; m.color = glm::vec3(1.f); m.metalness = 1; m.roughness = 0.14f; m.pattern = Pattern::DiscoBall;
        m.emissive = glm::vec3((0.9f + pz * 0.8f) * k);
        out.add(Pass::Lit, MeshId::SphereLow) = makeLit(compose(ball, {0.25f, t * GIANT_BALL_SPIN, 0}, glm::vec3(r)), m);
        LitMaterial chain; chain.color = hexColor("#cfd6e2"); chain.metalness = 1; chain.roughness = 0.3f;
        out.add(Pass::Lit, MeshId::Cylinder) = makeLit(compose(ball + glm::vec3(0, r + 6.f, 0), {0.03f, 12.f, 0.03f}), chain);
        out.add(Pass::UnlitAdd, MeshId::Plane) =
            makeUnlit(compose(ball, glm::vec3(r * 4.5f)), glow("#ffb8f4", 0.7f), 0.55f * k, Shape::RadialGlow);
    }
    // 12 thin lasers fanning out from the ball onto the ground and the skyline. They never cross the lane in 3D
    // (both ends stay at z <= -3) and always land ahead of the hen, so on screen they never sweep over her stack.
    const float henX = hen_.nodes[hen_.root].pos.x;
    const float bright = LASER_BRIGHT * (0.55f + 0.9f * pz) * k;
    for (int i = 0; i < LASERS; ++i) {
        const float ph = i * 1.7f, sp = 0.45f + 0.09f * float(i % 5);
        glm::vec3 target;
        target.x = henX + 2.f + (0.5f + 0.5f * std::sin(t * sp + ph)) * LASER_REACH;
        if (i % 2 == 0) { target.y = 0.02f; target.z = -3.5f - 7.f * (0.5f + 0.5f * std::sin(t * sp * 1.3f + ph * 2.f)); }
        else { target.y = 1.f + 9.f * (0.5f + 0.5f * std::sin(t * sp * 0.8f + ph)); target.z = -18.f; }
        out.add(Pass::UnlitAdd, MeshId::Plane) =
            makeUnlit(beamMatrix(ball, target, LASER_WIDTH, cam), laserColor(i) * 2.2f, bright, Shape::Laser);
    }
    // 4 wide, faint spotlight cones from the top of the screen sweeping side to side, hue cycling
    for (int i = 0; i < SPOTLIGHTS; ++i) {
        const float ang = std::sin(t * 0.9f + i * 1.9f) * 0.55f;
        const glm::vec3 apex{camX_ - 6.f + i * 4.f, camY_ + 9.f, -6.f};
        const glm::vec3 dir = glm::normalize(glm::vec3(std::sin(ang), -1.f, 0.f));
        out.add(Pass::UnlitAdd, MeshId::Plane) =
            makeUnlit(beamMatrix(apex, apex + dir * 17.f, 8.f, cam), hue(t * HUE_SPEED + i * 0.25f) * 1.2f, SPOT_ALPHA * k, Shape::Spot);
    }
}

} // namespace cs
