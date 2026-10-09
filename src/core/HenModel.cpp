#include "HenModel.h"
#include "Log.h"

#include <cstring>

namespace cs::assets {
extern const unsigned char hen_mesh[];
extern const size_t hen_mesh_size;
#define CS_OUTFIT_MODEL(id)                                                                                         \
    extern const unsigned char outfit_##id##_mesh[];                                                                \
    extern const size_t outfit_##id##_mesh_size;                                                                    \
    extern const unsigned char outfit_##id##_jpg[];                                                                 \
    extern const size_t outfit_##id##_jpg_size;                                                                     \
    extern const unsigned char outfit_##id##_n_jpg[];                                                               \
    extern const size_t outfit_##id##_n_jpg_size;
#include "OutfitModels.inc"
#undef CS_OUTFIT_MODEL
} // namespace cs::assets

namespace cs {

namespace {
// version 2: 12 floats per vertex (no uv); version 3: 14 (uv); version 4: 14 + the look (glow, metal, rough)
HenModel load(const unsigned char* data, size_t size, MeshId firstMesh) {
    HenModel m;
    const unsigned char* p = data;
    const unsigned char* end = p + size;
    auto take = [&](void* dst, size_t n) {
        if (size_t(end - p) < n) return false;
        std::memcpy(dst, p, n);
        p += n;
        return true;
    };
    char magic[4];
    uint32_t version = 0, regions = 0, shards = 0;
    if (!take(magic, 4) || std::memcmp(magic, "CSHN", 4) != 0 || !take(&version, 4) || version < 2 || version > 4 ||
        !take(&regions, 4) || regions == 0 || regions > HenModel::kRegions || !take(&shards, 4) || shards == 0 || shards > 256) {
        CS_LOGW("Hen model: missing or unknown format; using the procedural hen");
        return m;
    }
    static_assert(sizeof(HenModel::V) == 14 * sizeof(float), "HenModel::V layout");
    const size_t floats = version >= 3 ? 14 : 12;
    if (version >= 4 && (!take(&m.glow, 4) || !take(&m.metal, 4) || !take(&m.rough, 4))) return HenModel{};
    m.regions = int(regions);
    m.shardCount = int(shards);
    m.firstMesh = firstMesh;
    std::vector<float> raw;
    for (uint32_t r = 0; r < regions; ++r) {
        uint32_t nv = 0, ni = 0;
        if (!take(&nv, 4) || !take(&ni, 4)) return HenModel{};
        raw.resize(size_t(nv) * floats);
        m.verts[r].resize(nv);
        m.indices[r].resize(ni);
        if (!take(raw.data(), raw.size() * sizeof(float)) || !take(m.indices[r].data(), ni * sizeof(uint16_t))) return HenModel{};
        for (uint32_t i = 0; i < nv; ++i) {
            HenModel::V& v = m.verts[r][i];
            std::memcpy(&v, &raw[size_t(i) * floats], floats * sizeof(float));
            if (floats == 12) v.uv = glm::vec2(0.f);
            if (!(v.shard >= 0.f && v.shard < float(shards))) { CS_LOGW("Hen model: bad shard id"); return HenModel{}; }
        }
        for (uint16_t i : m.indices[r])
            if (i >= nv) { CS_LOGW("Hen model: index out of range"); return HenModel{}; }
        m.vertexCount += nv;
    }
    m.ok = true;
    return m;
}
} // namespace

namespace {
struct Entry {
    const char* outfit;
    const unsigned char* mesh; size_t meshSize;
    const unsigned char* jpg; size_t jpgSize;
    const unsigned char* normal; size_t normalSize;
};
const Entry kOutfits[] = {
#define CS_OUTFIT_MODEL(id)                                                                                         \
    {#id, assets::outfit_##id##_mesh, assets::outfit_##id##_mesh_size, assets::outfit_##id##_jpg, assets::outfit_##id##_jpg_size, \
     assets::outfit_##id##_n_jpg, assets::outfit_##id##_n_jpg_size},
#include "OutfitModels.inc"
#undef CS_OUTFIT_MODEL
    {nullptr, nullptr, 0, nullptr, 0, nullptr, 0}};

const std::vector<HenModel>& models() {
    static const std::vector<HenModel> all = [] {
        std::vector<HenModel> v;
        v.push_back(load(assets::hen_mesh, assets::hen_mesh_size, MeshId::HenBody));
        for (int i = 0; i < kOutfitModelCount; ++i) {
            HenModel m = load(kOutfits[i].mesh, kOutfits[i].meshSize, MeshId(int(MeshId::HenOutfit0) + i));
            m.texture = kOutfits[i].jpg;
            m.textureSize = kOutfits[i].jpgSize;
            m.normalMap = kOutfits[i].normal;
            m.normalMapSize = kOutfits[i].normalSize;
            v.push_back(std::move(m));
        }
        return v;
    }();
    return all;
}
} // namespace

const HenModel& henModel(HenModelId id) {
    const auto& all = models();
    return id > 0 && id < int(all.size()) ? all[size_t(id)] : all[0];
}

HenModelId henModelFor(const char* outfitId) {
    for (int i = 0; i < kOutfitModelCount; ++i)
        if (std::strcmp(kOutfits[i].outfit, outfitId) == 0 && henModel(i + 1).ok) return i + 1;
    return kClassicHen;
}

} // namespace cs
