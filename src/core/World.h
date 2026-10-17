// Scenery that follows the run, per zone (Zone.h).
// The city (levels 1-10): wet street, lane markings, guardrail, three parallax tower rows with neon signs and beacons,
// flying traffic, rain, speed lines, street lamps, bollards and steam vents. Layout and wrap maths match the web build.
// Neon Egypt (levels 11-20), lit like Tron: an obsidian street under a glowing grid, pyramids with glowing edges and
// capstone beams, obelisks, temple pylons and palms, cartouches and neon signs, a striped sun disc, solar barques,
// brazier columns, obelisk bollards and drifting sand (no rain, no vents).
// Neon Japan (levels 21-30), lit the same way: a full moon over neon-edged mountains, pagodas, torii gates, cherry
// trees in glowing bloom and stone lanterns, banners and signs, sky lanterns drifting by, a vermilion-and-pink grid,
// and cherry blossoms falling and blowing on gusts of wind through all of it (no rain, no vents).
#pragma once

#include "Materials.h"
#include "Zone.h"

#include <string>

#include <vector>

namespace cs {

class TextLayout;

class World {
public:
    explicit World(FastRandom& rng);

    void update(float dt, float time, float camX, float camY, float speed, bool playing);
    // off in lite mode (low-end devices): the rain (900 see-through streaks of overdraw) and the wind streaks
    void setRain(bool on) { rainOn_ = on; }
    void emit(RenderList& out, float camX, float camY, float time, float pulse, const glm::vec3& gradeLight, const TextLayout* text,
              float windowBoost = 0.f) const; // windowBoost: party mode, extra window emissive

    // world-space vent positions this frame (steam emitters; none in Egypt)
    const std::vector<glm::vec3>& vents() const { return ventWorld_; }

    // the scenery shown: switching is instant (both sets are built up front)
    void setZone(Zone z) { zone_ = z; }
    Zone zone() const { return zone_; }

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

    // Neon Egypt
    struct Monument { enum Kind { Pyramid, Obelisk, Pylon, Palm, Mountain, Pagoda, Torii, Tree, Lantern } kind; glm::vec3 pos, size; float seed; };
    struct Cartouche { glm::vec3 pos; float h; int icons[4]; float seed; };
    struct ERow { float z, P, par; std::vector<Monument> monuments; std::vector<Sign> signs; std::vector<Cartouche> cartouches; std::vector<Beacon> beacons; float x = 0, y = 0; };
    void buildEgypt();
    void buildJapan();
    void emitJapan(RenderList& out, float camX, float camY, float time, float pz, const TextLayout* text) const;
    void emitBlossomTree(RenderList& out, glm::vec3 base, const glm::vec3& size, float seed, float time) const;
    void emitCity(RenderList& out, float camX, float time, float pz, const glm::vec3& gradeLight, const TextLayout* text, float windowBoost) const;
    void emitEgypt(RenderList& out, float camX, float camY, float time, float pz, const TextLayout* text) const;
    void emitIcon(RenderList& out, int kind, glm::vec3 c, float size, const glm::vec3& color, float alpha) const;
    std::vector<ERow> egypt_, japan_;
    float gust_ = 0, gustT_ = 0;      // Neon Japan's wind: gusts that blow the blossoms along
    Zone zone_ = Zone::City;

    FastRandom& rng_;
    std::vector<Row> rows_;
    std::vector<Car> cars_;
    std::vector<Streak> streaks_;
    std::vector<Drop> rain_;
    bool rainOn_ = true;
    std::vector<Line> speed_;
    std::vector<glm::vec3> ventWorld_;
    float speedOpacity_ = 0.f;
    glm::vec3 rainOrigin_{0.f};
    std::vector<glm::vec3> speedSegs_; // pairs of endpoints
};

} // namespace cs
