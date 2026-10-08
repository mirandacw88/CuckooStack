// CPU-simulated effects: the FX particle class (points with drag/gravity/size/alpha curves)
// and the pooled shockwave rings, ported from the web build.
#pragma once

#include "RenderList.h"

#include <vector>

namespace cs {

class ParticleSystem {
public:
    explicit ParticleSystem(size_t max) : max_(max) { p_.reserve(max); }

    void emit(glm::vec3 pos, glm::vec3 vel, float life, float s0, float s1, glm::vec3 color,
              float alpha = 1.f, float drag = 0.f, float grav = 0.f) {
        if (p_.size() >= max_) return;
        p_.push_back({pos, vel, color, life, life, s0, s1, alpha, drag, grav, s0, 0.f});
    }
    void update(float dt);
    void clear() { p_.clear(); }
    void emitTo(std::vector<Particle>& out) const;
    size_t size() const { return p_.size(); }

private:
    struct P {
        glm::vec3 pos, vel, color;
        float life, maxLife, s0, s1, a0, drag, grav, size, alpha;
    };
    size_t max_;
    std::vector<P> p_;
};

class Shockwaves {
public:
    void spawn(glm::vec3 pos, glm::vec3 color, float size, float duration, bool vertical) {
        live_.push_back({pos, color, size, duration, 0.f, vertical});
    }
    void update(float dt);
    void clear() { live_.clear(); }
    void emitTo(RenderList& out) const;

private:
    struct Ring { glm::vec3 pos, color; float size, dur, t; bool vertical; };
    std::vector<Ring> live_;
};

} // namespace cs
