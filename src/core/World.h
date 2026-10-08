// Scenery that follows the run: wet street, lane markings, guardrail, three parallax tower rows with
// neon signs and beacons, flying traffic, rain, speed lines, street lamps, bollards and steam vents.
// Layout and wrap maths match the web build; skyline silhouettes and searchlight cones are not ported yet.
#pragma once

#include "Materials.h"

#include <string>

#include <vector>

namespace cs {

class TextLayout;

class World {
public:
    explicit World(FastRandom& rng);

    void update(float dt, float time, float camX, float camY, float speed, bool playing);
    void emit(RenderList& out, float camX, float camY, float time, float pulse, const glm::vec3& gradeLight, const TextLayout* text,
              float windowBoost = 0.f) const; // windowBoost: party mode, extra window emissive

    // world-space vent positions this frame (steam emitters)
    const std::vector<glm::vec3>& vents() const { return ventWorld_; }

private:
    struct Tower { glm::vec3 pos, size; float seed; };
    struct Sign { glm::vec3 pos; glm::vec2 size; glm::vec3 color; bool flicker; float t, opacity; std::string word; bool vertical; };
    void emitSignText(RenderList& out, const TextLayout& text, const Sign& s, const glm::vec3& origin) const;
    struct Beacon { glm::vec3 pos; float phase; };
    struct Row { float z, P, par; std::vector<Tower> towers; std::vector<Sign> signs; std::vector<Beacon> beacons; float x = 0, y = 0; };
    struct Car { float z, y, v, off; bool flip; glm::vec3 under; glm::vec3 pos{0.f}; };
    struct Streak { glm::vec3 pos; float w, l; int mat; };
    struct Drop { glm::vec3 p; };
    struct Line { float x, y, z, l; };

    FastRandom& rng_;
    std::vector<Row> rows_;
    std::vector<Car> cars_;
    std::vector<Streak> streaks_;
    std::vector<Drop> rain_;
    std::vector<Line> speed_;
    std::vector<glm::vec3> ventWorld_;
    float speedOpacity_ = 0.f;
    glm::vec3 rainOrigin_{0.f};
    std::vector<glm::vec3> speedSegs_; // pairs of endpoints
};

} // namespace cs
