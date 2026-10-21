// Procedural meshes (Geometry.cpp): the three.js generators the web build used. For each: three.js's vertex and
// index counts, indices in range, unit normals, triangles wound counter-clockwise (their face normal agrees with the
// vertex normals, so back-face culling keeps the right side), and the requested size. Then the mesh library.
#include "TestUtil.h"
#include "core/DroidModel.h"
#include "core/Geometry.h"
#include "core/HenModel.h"

using namespace cs;
using namespace test;

namespace {

struct Stats { bool inRange = true, unitNormals = true, ccw = true; int degenerate = 0; glm::vec3 lo{1e9f}, hi{-1e9f}; };

Stats inspect(const MeshData& m) {
    Stats s;
    for (const Vertex& v : m.vertices) {
        s.unitNormals = s.unitNormals && std::abs(glm::length(v.normal) - 1.f) < 1e-3f;
        s.lo = glm::min(s.lo, v.pos);
        s.hi = glm::max(s.hi, v.pos);
    }
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const uint16_t a = m.indices[i], b = m.indices[i + 1], c = m.indices[i + 2];
        if (a >= m.vertices.size() || b >= m.vertices.size() || c >= m.vertices.size()) { s.inRange = false; continue; }
        const glm::vec3 face = glm::cross(m.vertices[b].pos - m.vertices[a].pos, m.vertices[c].pos - m.vertices[a].pos);
        if (glm::length(face) < 1e-9f) { s.degenerate++; continue; }
        const glm::vec3 n = m.vertices[a].normal + m.vertices[b].normal + m.vertices[c].normal;
        if (glm::dot(face, n) <= 0.f) s.ccw = false;
    }
    return s;
}
bool size3(const Stats& s, glm::vec3 want, float eps = 1e-4f) { return glm::length((s.hi - s.lo) - want) < eps; }
void all(const char* name, const MeshData& m, size_t verts, size_t indices, glm::vec3 extent, int allowedDegenerate = 0) {
    const Stats s = inspect(m);
    const std::string n(name);
    check(m.vertices.size() == verts && m.indices.size() == indices, (n + ": three.js vertex / index counts").c_str());
    check(s.inRange && m.indices.size() % 3 == 0, (n + ": whole triangles, indices in range").c_str());
    check(s.unitNormals, (n + ": unit normals").c_str());
    check(s.ccw && s.degenerate <= allowedDegenerate, (n + ": counter-clockwise faces (normals point out)").c_str());
    check(size3(s, extent), (n + ": the requested size").c_str());
}

void generators() {
    all("box", geo::box(2, 3, 4), 24, 36, {2, 3, 4});
    all("sphere", geo::sphere(1.5f, 16, 8), size_t(17 * 9), size_t(16 * 6 * 2 * 3 + 16 * 2 * 3), {3, 3, 3});
    // cylinder: the side (2 rows of rs + 1) plus each cap (rs centres + a ring of rs + 1)
    all("cylinder", geo::cylinder(0.5f, 0.5f, 2, 12), size_t(2 * 13 + 2 * (12 + 13)), size_t(12 * 6 + 2 * 12 * 3), {1, 2, 1});
    all("cone (no top cap)", geo::cylinder(0, 1, 1, 12), size_t(2 * 13 + 12 + 13), size_t(12 * 6 + 12 * 3), {2, 1, 2}, 12);
    all("torus", geo::torus(1, 0.25f, 8, 24, 2 * kPi), size_t(9 * 25), size_t(8 * 24 * 6), {2.5f, 2.5f, 0.5f});
    all("plane", geo::plane(2, 3), 4, 6, {2, 3, 0});
    all("ring", geo::ring(0.5f, 1, 32), size_t(2 * 33), size_t(32 * 6), {2, 2, 0});
    all("circle", geo::circle(2, 24), size_t(1 + 25), size_t(24 * 3), {4, 4, 0});
    const std::vector<glm::vec2> profile = {{0.0001f, -1}, {0.5f, -0.5f}, {0.6f, 0}, {0.5f, 0.5f}, {0.0001f, 1}};
    all("lathe", geo::lathe(profile, 16), size_t(17 * 5), size_t(16 * 4 * 6), {1.2f, 2, 1.2f});
    const MeshData arc = geo::torus(1, 0.2f, 6, 10, kPi);
    check(inspect(arc).lo.y > -0.21f, "torus arc: half a turn stays on one side");
    // an egg sawn lengthwise (GameSaw.cpp): half a turn of the lathe, on the x >= 0 side, and the flat face closing it
    all("half lathe", geo::lathe(profile, 8, kPi), size_t(9 * 5), size_t(8 * 4 * 6), {0.6f, 2, 1.2f});
    const MeshData cap = geo::latheCap(profile);
    all("lathe cap", cap, size_t(2 * 5), size_t(4 * 6), {0, 2, 1.2f});
    bool flat = true;
    for (const Vertex& v : cap.vertices) flat = flat && v.pos.x == 0 && v.normal == glm::vec3(-1, 0, 0);
    check(flat, "lathe cap: in the cut plane (x = 0), facing out of the open half (-x)");
    // a saw blade's toothed rim: closed faces on both sides, 16 teeth with tips at r = 1 and gullets at 0.84 round the hole
    const MeshData teeth = geo::sawTeeth(16, 0.84f, 0.74f, 0.1f);
    const Stats ts = inspect(teeth);
    check(teeth.vertices.size() == size_t(2 * 2 * 49 + 4 * 48) && teeth.indices.size() == size_t(2 * 48 * 6 + 48 * 6) && ts.inRange,
          "saw teeth: two faces of 48 quads round the hole, and the edge of the outline");
    check(ts.unitNormals && ts.ccw && ts.degenerate == 0, "saw teeth: counter-clockwise faces (normals point out)");
    int tips = 0;
    float rMin = 9, rMax = 0, hole = 9;
    for (const Vertex& v : teeth.vertices) {
        const float r = glm::length(glm::vec2(v.pos));
        if (v.normal.z > 0.5f && v.uv.y > 0.5f) { tips += std::abs(r - 1) < 1e-5f; rMin = std::min(rMin, r); rMax = std::max(rMax, r); } // the front's outline
        if (v.uv.y < 0.5f && std::abs(v.normal.z) > 0.5f) hole = std::min(hole, r);
    }
    check(tips == 16 && std::abs(rMax - 1) < 1e-5f && std::abs(rMin - 0.84f) < 1e-5f && std::abs(hole - 0.74f) < 1e-5f && std::abs(ts.hi.z - 0.05f) < 1e-6f,
          "saw teeth: 16 tips at r = 1, gullets at 0.84, the hole 0.74, 0.1 thick");
    // the teeth hook: each tip comes just after its gullet going counter-clockwise, so the steep faces lead clockwise
    bool hooked = true;
    for (size_t i = 0; i + 2 < 2 * 48; i += 2 * 3) {
        const glm::vec2 g(teeth.vertices[i + 1].pos), t(teeth.vertices[i + 3].pos), b(teeth.vertices[i + 5].pos);
        const float ag = std::atan2(g.y, g.x), at = std::atan2(t.y, t.x), ab = std::atan2(b.y, b.x);
        auto ahead = [](float a, float b) { float d = b - a; while (d < 0) d += 2 * kPi; while (d >= 2 * kPi) d -= 2 * kPi; return d; };
        hooked = hooked && ahead(ag, at) < ahead(at, ab) * 0.4f && glm::length(t) > glm::length(b) && glm::length(b) > glm::length(g);
    }
    check(hooked, "saw teeth: steep cutting faces, sloping backs");
}

void library() {
    const std::vector<MeshData> lib = buildMeshLibrary();
    check(lib.size() == kMeshCount, "library: one mesh per MeshId");
    bool filled = true, fits = true, valid = true;
    for (size_t i = 0; i < lib.size(); ++i) {
        const bool hen = i >= size_t(MeshId::HenBody);
        const HenModel* model = nullptr;
        if (hen) {
            const int id = i < size_t(MeshId::HenOutfit0) ? 0 : 1 + int(i - size_t(MeshId::HenOutfit0));
            model = &henModel(HenModelId(id));
        }
        // a textured outfit model has one region: its other hen slots stay empty by design
        const bool expected = !hen || (model->ok && int(i) - int(model->firstMesh) < model->regions && int(i) >= int(model->firstMesh));
        if (expected) filled = filled && !lib[i].vertices.empty() && !lib[i].indices.empty();
        fits = fits && lib[i].vertices.size() <= 65536;
        valid = valid && inspect(lib[i]).inRange;
    }
    check(filled, "library: every procedural mesh and every hen model region is built");
    check(fits && valid, "library: 16-bit indices, all in range");
    const Stats egg = inspect(lib[size_t(MeshId::Egg)]);
    check(std::abs((egg.hi.y - egg.lo.y) - 0.6f * 0.49f * 2) < 1e-3f, "the egg: one egg unit (0.6 m) tall, near enough");
    const Stats half = inspect(lib[size_t(MeshId::EggHalf)]), cap = inspect(lib[size_t(MeshId::EggHalfCap)]);
    check(half.ccw && cap.ccw && std::abs(half.hi.y - egg.hi.y) < 1e-4f && std::abs(half.lo.y - egg.lo.y) < 1e-4f && half.lo.x > -1e-4f &&
              std::abs(half.hi.x - egg.hi.x) < 1e-3f && std::abs(cap.hi.z - egg.hi.z) < 1e-3f && cap.hi.x == 0,
          "the sawn egg: half the egg's shell (x >= 0), as long as the egg, closed by its cut face");
    check(inspect(lib[size_t(MeshId::Box)]).ccw && size3(inspect(lib[size_t(MeshId::Box)]), {1, 1, 1}), "the unit box");
    // Neon Egypt: the pyramid and the tapered block, unit sized, closed, faces pointing out
    const Stats pyr = inspect(lib[size_t(MeshId::Pyramid)]), fru = inspect(lib[size_t(MeshId::Frustum)]);
    check(pyr.ccw && pyr.unitNormals && pyr.degenerate == 0 && size3(pyr, {1, 1, 1}) && lib[size_t(MeshId::Pyramid)].indices.size() == 3 * 6,
          "the pyramid: 1 x 1 base, 1 tall, four sides and a base, faces out");
    check(fru.ccw && fru.unitNormals && fru.degenerate == 0 && size3(fru, {1, 1, 1}) && lib[size_t(MeshId::Frustum)].indices.size() == 3 * 12,
          "the tapered block: four sides, base and top, faces out");
    float topW = 0;
    for (const Vertex& v : lib[size_t(MeshId::Frustum)].vertices) if (v.pos.y > 0.49f) topW = std::max(topW, 2 * std::abs(v.pos.x));
    check(std::abs(topW - 0.7f) < 1e-5f, "...its top 0.7 wide (it tapers)");
    // the jet droid: each part of its model (DroidModel.h) becomes its own mesh, every vertex and triangle carried over
    const DroidModel& droid = droidModel();
    bool same = droid.ok;
    for (int part = 0; part < DroidModel::kParts && same; ++part) {
        const MeshData& m = lib[size_t(int(MeshId::DroidBody) + part)];
        const DroidModel::Mesh& src = droid.parts[size_t(part)];
        same = m.vertices.size() == src.verts.size() && m.indices.size() == src.indices.size();
        for (size_t v = 0; v < src.verts.size() && same; ++v)
            same = m.vertices[v].pos == src.verts[v].pos && m.vertices[v].normal == src.verts[v].normal && m.vertices[v].uv == src.verts[v].uv;
        for (size_t k = 0; k < src.indices.size() && same; ++k) same = m.indices[k] == src.indices[k]; // (no 16-bit truncation)
    }
    check(same, "the droid: one mesh per model part (body, eye, fans, shrouds, backplates, flame), vertices and triangles intact");
    check(int(MeshId::DroidFlame) - int(MeshId::DroidBody) == DroidModel::kParts - 1, "...its MeshIds in the model's part order");
}

} // namespace

int main() {
    generators();
    library();
    return finish("Geometry");
}
