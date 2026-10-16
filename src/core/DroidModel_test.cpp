// The title droid's model (DroidModel.h): the embedded export loads whole, with its parts, fan spins and anchors, and a
// damaged file is refused (the title screen then goes without the droid) rather than read out of bounds.
#include "TestUtil.h"
#include "core/DroidModel.h"

#include <cstring>
#include <vector>

using namespace cs;
using namespace test;

namespace {

void embedded() {
    const DroidModel& d = droidModel();
    check(d.ok, "the embedded droid model loads");
    bool parts = true;
    for (const DroidModel::Mesh& m : d.parts) parts = parts && !m.verts.empty() && !m.indices.empty() && m.indices.size() % 3 == 0;
    check(parts, "every part has geometry (body, eye, both fans, shrouds, backplates, flame)");
    check(d.parts[DroidModel::Body].verts.size() < 65536 && d.parts[DroidModel::Body].indices.size() / 3 < 40000,
          "the body is mobile-sized (16-bit indices, under 40k triangles)");
    check(d.parts[DroidModel::Body].textured && d.parts[DroidModel::Eye].textured && d.parts[DroidModel::Eye].glowing &&
              d.parts[DroidModel::FanL].textured && !d.parts[DroidModel::Shroud].textured,
          "the body, eye and fans are textured (the eye glows); the shrouds are plain metal");
    const DroidModel::Mesh &l = d.parts[DroidModel::FanL], &r = d.parts[DroidModel::FanR];
    check(std::abs(std::abs(l.spin) - 15.08f) < 0.05f && std::abs(l.spin + r.spin) < 1e-3f,
          "the fans spin 2.4 turns a second, in opposite directions (the Blender animation)");
    check(std::abs(glm::length(l.axis) - 1.f) < 1e-3f && std::abs(l.axis.x) > 0.9f, "a fan spins about the jet's axis (nose to tail)");
    check(d.eye.pos.x < -0.15f && d.exhaust[0].pos.x > 0.1f && d.exhaust[1].pos.x > 0.1f && d.exhaust[0].pos.z * d.exhaust[1].pos.z < 0,
          "the eye is at the nose (-x), the exhausts behind it on either side");
    check(d.exhaust[0].size.x > d.exhaust[0].size.z && d.exhaust[0].size.y > d.exhaust[0].size.w && d.exhaust[0].size.w > 0,
          "each exhaust's flame is longer and wider than its core");
    float minX = 1e9f, maxX = -1e9f;
    for (const DroidModel::V& v : d.parts[DroidModel::Flame].verts) { minX = std::min(minX, v.pos.x); maxX = std::max(maxX, v.pos.x); }
    check(std::abs(minX) < 1e-4f && std::abs(maxX - 1.f) < 1e-4f, "the flame is unit length from the nozzle (the game scales it)");
    check(d.texture && d.textureSize > 1000 && d.normalMap && d.normalMapSize > 1000, "its albedo and normal map are embedded");
}

void damaged() {
    std::vector<unsigned char> good;
    {
        // rebuild a small valid file: one of each part (a triangle), and the three anchors
        auto put = [&](const void* p, size_t n) { const auto* b = static_cast<const unsigned char*>(p); good.insert(good.end(), b, b + n); };
        auto u32 = [&](uint32_t v) { put(&v, 4); };
        auto f32 = [&](float v) { put(&v, 4); };
        put("CSDR", 4); u32(1); u32(7);
        for (const char* n : {"body", "eye", "fan_l", "fan_r", "shroud", "backplate", "flame"}) {
            const uint8_t len = uint8_t(std::strlen(n));
            put(&len, 1); put(n, len); u32(1);
            for (int k = 0; k < 13; ++k) f32(k == 9 ? 1.f : 0.f);
            u32(3); u32(3);
            for (int v = 0; v < 3; ++v) for (int k = 0; k < 8; ++k) f32(float(v == k));
            u32(0); u32(1); u32(2);
        }
        u32(3);
        for (const char* n : {"eye", "exhaust_l", "exhaust_r"}) {
            const uint8_t len = uint8_t(std::strlen(n));
            put(&len, 1); put(n, len);
            for (int k = 0; k < 10; ++k) f32(0.1f * float(k));
        }
    }
    check(parseDroidModel(good.data(), good.size()).ok, "a well-formed file loads");
    bool truncated = true;
    for (size_t n = 0; n < good.size(); n += 7) truncated = truncated && !parseDroidModel(good.data(), n).ok;
    check(truncated, "a truncated file is refused, wherever it's cut");
    std::vector<unsigned char> bad = good;
    bad[0] = 'X';
    check(!parseDroidModel(bad.data(), bad.size()).ok, "a wrong magic is refused");
    bad = good;
    bad[4] = 2; // version
    check(!parseDroidModel(bad.data(), bad.size()).ok, "an unknown version is refused");
    bad = good;
    // the body's first index (after "CSDR", version, count, name, flags, 13 floats, counts, 3 vertices)
    const size_t firstIndex = 12 + 1 + 4 + 4 + 52 + 8 + 3 * 32;
    const uint32_t far = 99;
    std::memcpy(bad.data() + firstIndex, &far, 4);
    check(!parseDroidModel(bad.data(), bad.size()).ok, "an index past the vertices is refused");
    check(!parseDroidModel(nullptr, 0).ok, "no data: not ok");
}

} // namespace

int main() {
    embedded();
    damaged();
    return finish("DroidModel");
}
