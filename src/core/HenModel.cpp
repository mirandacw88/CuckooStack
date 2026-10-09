#include "HenModel.h"
#include "Log.h"

#include <cstring>

namespace cs::assets {
extern const unsigned char hen_mesh[];
extern const size_t hen_mesh_size;
} // namespace cs::assets

namespace cs {

namespace {
HenModel load() {
    HenModel m;
    const unsigned char* p = assets::hen_mesh;
    const unsigned char* end = p + assets::hen_mesh_size;
    auto take = [&](void* dst, size_t n) {
        if (size_t(end - p) < n) return false;
        std::memcpy(dst, p, n);
        p += n;
        return true;
    };
    char magic[4];
    uint32_t version = 0, regions = 0, shards = 0;
    if (!take(magic, 4) || std::memcmp(magic, "CSHN", 4) != 0 || !take(&version, 4) || version != 2 || !take(&regions, 4) ||
        regions != HenModel::kRegions || !take(&shards, 4) || shards == 0 || shards > 256) {
        CS_LOGW("Hen model: missing or unknown format; using the procedural hen");
        return m;
    }
    static_assert(sizeof(HenModel::V) == 12 * sizeof(float), "HenModel::V must match the file's vertex layout");
    m.shardCount = int(shards);
    for (uint32_t r = 0; r < regions; ++r) {
        uint32_t nv = 0, ni = 0;
        if (!take(&nv, 4) || !take(&ni, 4)) return HenModel{};
        m.verts[r].resize(nv);
        m.indices[r].resize(ni);
        if (!take(m.verts[r].data(), nv * sizeof(HenModel::V)) || !take(m.indices[r].data(), ni * sizeof(uint16_t))) return HenModel{};
        for (uint16_t i : m.indices[r])
            if (i >= nv) { CS_LOGW("Hen model: index out of range"); return HenModel{}; }
        for (const HenModel::V& v : m.verts[r])
            if (!(v.shard >= 0.f && v.shard < float(shards))) { CS_LOGW("Hen model: bad shard id"); return HenModel{}; }
        m.vertexCount += nv;
    }
    m.ok = true;
    return m;
}
} // namespace

const HenModel& henModel() {
    static const HenModel m = load();
    return m;
}

} // namespace cs
