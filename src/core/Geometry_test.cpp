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
