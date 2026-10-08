#include "Effects.h"
#include "Materials.h"

namespace cs {

void ParticleSystem::update(float dt) {
    size_t i = 0;
    while (i < p_.size()) {
        P& p = p_[i];
        p.life -= dt;
        if (p.life <= 0.f) { p = p_.back(); p_.pop_back(); continue; } // swap-remove, like FX.kill
        const float t = 1.f - p.life / p.maxLife, d = std::exp(-p.drag * dt);
        p.vel.x *= d; p.vel.y = p.vel.y * d - p.grav * dt; p.vel.z *= d;
        p.pos += p.vel * dt;
        p.size = p.s0 + (p.s1 - p.s0) * t;
        p.alpha = p.a0 * (t < 0.06f ? t / 0.06f : (1.f - t) / 0.94f);
        ++i;
    }
}

void ParticleSystem::emitTo(std::vector<Particle>& out) const {
    out.reserve(out.size() + p_.size());
    for (const P& p : p_) out.push_back({glm::vec4(p.pos, p.size), glm::vec4(p.color, p.alpha)});
}

void Shockwaves::update(float dt) {
    for (size_t i = live_.size(); i-- > 0;) {
        live_[i].t += dt;
        if (live_[i].t >= live_[i].dur) live_.erase(live_.begin() + static_cast<long>(i));
    }
}

void Shockwaves::emitTo(RenderList& out) const {
    for (const Ring& r : live_) {
        const float k = std::min(1.f, r.t / r.dur);
        const float scale = r.size * (0.12f + 0.88f * (1.f - std::pow(1.f - k, 3.f)));
        const float opacity = std::pow(1.f - k, 1.6f);
        const glm::mat4 m = compose(r.pos, {r.vertical ? 0.f : -kPi / 2, 0, 0}, glm::vec3(scale));
        out.add(Pass::UnlitAdd, MeshId::RingFlat) = makeUnlit(m, r.color, opacity);
    }
}

} // namespace cs
