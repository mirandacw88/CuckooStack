// The hen models (HenModel.cpp): the Classic hen and every outfit with its own textured model (OutfitModels.inc), parsed
// from the embedded assets. Checks every model loads and is sound: skin weights, shard ids for the glass shatter,
// indices, normals, textures, and the outfit -> model lookup.
#include "TestUtil.h"
#include "core/HenModel.h"
#include "core/OutfitModels.h"

#include <cstring>

using namespace cs;
using namespace test;

namespace {

const char* kIds[] = {
#define CS_OUTFIT_MODEL(id) #id,
#include "core/OutfitModels.inc"
#undef CS_OUTFIT_MODEL
};

void sound(const HenModel& m, const std::string& name) {
    check(m.ok && m.vertexCount > 100 && m.shardCount > 1, (name + ": loads").c_str());
    bool weights = true, shards = true, indices = true, normals = true, used = true;
    std::vector<bool> shardSeen(size_t(std::max(1, m.shardCount)), false);
    size_t total = 0;
    for (int r = 0; r < m.regions; ++r) {
        total += m.verts[size_t(r)].size();
        for (const HenModel::V& v : m.verts[size_t(r)]) {
            float sum = 0;
            for (float w : v.w) { sum += w; weights = weights && w >= -1e-4f; }
            weights = weights && std::abs(sum - 1.f) < 1e-3f;
            shards = shards && v.shard >= 0 && v.shard < float(m.shardCount);
            if (v.shard >= 0 && v.shard < float(m.shardCount)) shardSeen[size_t(v.shard)] = true;
            normals = normals && std::abs(glm::length(v.normal) - 1.f) < 1e-2f;
        }
        for (uint16_t i : m.indices[size_t(r)]) indices = indices && i < m.verts[size_t(r)].size();
        used = used && m.indices[size_t(r)].size() % 3 == 0 && !m.indices[size_t(r)].empty();
    }
    int seen = 0;
    for (bool s : shardSeen) seen += s;
    check(weights, (name + ": every vertex's bone weights add up to 1").c_str());
    check(shards && seen == m.shardCount, (name + ": shard ids in range, every shard has glass").c_str());
    check(indices && used, (name + ": whole triangles, indices in range").c_str());
    check(normals, (name + ": unit normals (no zero normals: they shade as NaN / black)").c_str());
    check(total == m.vertexCount, (name + ": vertexCount matches the regions").c_str());
}

void classic() {
    const HenModel& c = henModel(kClassicHen);
    sound(c, "Classic");
    check(c.regions == HenModel::kRegions && !c.textured() && c.firstMesh == MeshId::HenBody, "Classic: five colour regions, no texture");
    check(&henModel(-1) == &c && &henModel(9999) == &c, "an unknown model id falls back to the Classic hen");
}

void outfits() {
    check(int(std::size(kIds)) == kOutfitModelCount, "every OutfitModels.inc line is counted");
    for (int i = 0; i < kOutfitModelCount; ++i) {
        const HenModel& m = henModel(i + 1);
        const std::string name = kIds[i];
        sound(m, name);
        check(m.textured() && m.regions == 1 && m.firstMesh == MeshId(int(MeshId::HenOutfit0) + i), (name + ": one textured region, its own mesh slot").c_str());
        check(m.textureSize > 4 && m.texture[0] == 0xFF && m.texture[1] == 0xD8, (name + ": the texture is a JPEG").c_str());
        check(m.normalMap && m.normalMapSize > 4 && m.normalMap[0] == 0xFF && m.normalMap[1] == 0xD8, (name + ": with a JPEG normal map").c_str());
        check(m.glow >= 0 && m.metal >= 0 && m.metal <= 1 && m.rough > 0 && m.rough <= 1, (name + ": a sane look (glow, metal, rough)").c_str());
        check(henModelFor(kIds[i]) == i + 1, (name + ": the outfit wears its model").c_str());
    }
    check(henModelFor("classic") == kClassicHen && henModelFor("") == kClassicHen && henModelFor("hen_gold_typo") == kClassicHen,
          "outfits without a model wear the Classic hen");
}

} // namespace

int main() {
    classic();
    outfits();
    return finish("HenModel");
}
