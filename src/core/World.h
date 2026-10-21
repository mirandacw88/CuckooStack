// Scenery that follows the run, per zone (Zone.h).
// The city (levels 1-10): wet street, lane markings, guardrail, three parallax tower rows with neon signs and beacons,
// flying traffic, rain, speed lines, street lamps, bollards and steam vents. Layout and wrap maths match the web build.
// Neon Egypt (levels 11-20), the artist's neon line art (assets/backdrop, EgyptBackdrop.h) laid out in parallax layers
// under a night sky of constellations (WorldEgypt.cpp): Giza's pyramids far off, capstones lit and sweeping beams;
// temple gates, columned temples, obelisks, the sphinx and a watchtower; Anubis and Horus standing guard among palms;
// a canal; scarabs flying over; a glowing moon, clouds drifting across it, its light on the canal; the furthest
// pyramids silhouettes against the night. On an obsidian street under a glowing grid, with a paved plaza, brazier
// columns, obelisk bollards, drifting sand and gold dust sparkling as it blows by on the wind (no rain, no vents).
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

    // Neon Japan's skyline
    struct Monument { enum Kind { Mountain, Pagoda, Torii, Tree, Lantern } kind; glm::vec3 pos, size; float seed; };
    struct ERow { float z, P, par; std::vector<Monument> monuments; std::vector<Sign> signs; std::vector<Beacon> beacons; float x = 0, y = 0; };
    // Neon Egypt's backdrop (WorldEgypt.cpp): layers of sprites, far to near, each wrapping like the city's rows. A prop
    // stands on the ground at x along its layer (dz: nearer or further than the layer, which also orders the drawing),
    // h tall; flip mirrors it
    struct Prop { uint8_t sprite; float x, h, dz; bool flip; float seed; };
    struct Layer { float z, P, par, light, haze, neon; std::vector<Prop> props; float x = 0, y = 0, sil = 0; }; // (sil 1: silhouettes)
    struct Constellation { std::vector<glm::vec2> stars; std::vector<std::pair<int, int>> links; glm::vec2 at; float size, phase; };
    void buildEgypt();
    void emitEgyptBackdrop(RenderList& out, float camX, float time) const;
    void buildJapan();
    void emitJapan(RenderList& out, float camX, float camY, float time, float pz, const TextLayout* text) const;
    void emitBlossomTree(RenderList& out, glm::vec3 base, const glm::vec3& size, float seed, float time) const;
    void emitCity(RenderList& out, float camX, float time, float pz, const glm::vec3& gradeLight, const TextLayout* text, float windowBoost) const;
    void emitEgypt(RenderList& out, float camX, float time, float pz) const;
    std::vector<ERow> japan_;
    std::vector<Layer> egypt_;          // far (pyramids), temples, guardians and palms, the canal, the plaza
    std::vector<Constellation> stars_;  // Neon Egypt's sky
    Layer starLayer_{};
    // ...and its gold dust: specks blowing by on the wind, all through the scene's depth (world space, each one wrapping
    // round to come again ahead of the camera once it's blown out of sight behind)
    struct Dust { glm::vec3 p; float size, phase, rate; };
    std::vector<Dust> dust_;
    glm::vec3 moon_{0.f}; // ...and its moon, where it is this frame (it keeps its place in the sky as she runs)
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
