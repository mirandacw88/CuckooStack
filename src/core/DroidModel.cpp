#include "DroidModel.h"
#include "Log.h"

#include <cstring>

namespace cs::assets {
extern const unsigned char droid_mesh[];
extern const size_t droid_mesh_size;
extern const unsigned char droid_jpg[];
extern const size_t droid_jpg_size;
extern const unsigned char droid_n_jpg[];
extern const size_t droid_n_jpg_size;
} // namespace cs::assets

namespace cs {

// droid.mesh (scripts/models/export_droid.py): "CSDR", u32 version 1, u32 parts, per part: u8 name length, name, u32
// flags (1 textured, 2 glowing), f32 colour rgb, metal, rough, glow, pivot xyz, axis xyz, spin, u32 vertex count, u32
// index count, vertices (8 f32), u32 indices; u32 anchors, per anchor: u8 name length, name, f32 pos xyz, dir xyz,
// size xyzw
DroidModel parseDroidModel(const unsigned char* data, size_t size) {
    DroidModel m;
    if (!data) return m;
    const unsigned char* p = data;
    const unsigned char* end = p + size;
    auto take = [&](void* dst, size_t n) {
        if (size_t(end - p) < n) return false;
        std::memcpy(dst, p, n);
        p += n;
        return true;
    };
    auto name = [&](std::string& s) {
        uint8_t len = 0;
        if (!take(&len, 1) || size_t(end - p) < len) return false;
        s.assign(reinterpret_cast<const char*>(p), len);
        p += len;
        return true;
    };
    char magic[4];
    uint32_t version = 0, parts = 0;
    if (!take(magic, 4) || std::memcmp(magic, "CSDR", 4) != 0 || !take(&version, 4) || version != 1 || !take(&parts, 4)) return m;
    static const char* kNames[DroidModel::kParts] = {"body", "eye", "fan_l", "fan_r", "shroud", "backplate", "flame"};
    bool found[DroidModel::kParts] = {};
    for (uint32_t i = 0; i < parts; ++i) {
        std::string n;
        uint32_t flags = 0, vcount = 0, icount = 0;
        float f[13];
        if (!name(n) || !take(&flags, 4) || !take(f, sizeof f) || !take(&vcount, 4) || !take(&icount, 4)) return m;
        if (vcount > 1u << 20 || icount > 3u << 20 || icount % 3) return m;
        DroidModel::Mesh mesh;
        mesh.textured = flags & 1; mesh.glowing = flags & 2;
        mesh.color = {f[0], f[1], f[2]}; mesh.metal = f[3]; mesh.rough = f[4]; mesh.glow = f[5];
        mesh.pivot = {f[6], f[7], f[8]}; mesh.axis = {f[9], f[10], f[11]}; mesh.spin = f[12];
        mesh.verts.resize(vcount);
        mesh.indices.resize(icount);
        for (DroidModel::V& v : mesh.verts) {
            float x[8];
            if (!take(x, sizeof x)) return m;
            v.pos = {x[0], x[1], x[2]}; v.normal = {x[3], x[4], x[5]}; v.uv = {x[6], x[7]};
        }
        if (icount && !take(mesh.indices.data(), icount * 4)) return m;
        for (uint32_t k : mesh.indices) if (k >= vcount) return m;
        for (int k = 0; k < DroidModel::kParts; ++k)
            if (n == kNames[k]) { m.parts[size_t(k)] = std::move(mesh); found[k] = true; }
    }
    uint32_t anchors = 0;
    if (!take(&anchors, 4)) return m;
    bool eye = false, ex[2] = {};
    for (uint32_t i = 0; i < anchors; ++i) {
        std::string n;
        float f[10];
        if (!name(n) || !take(f, sizeof f)) return m;
        const DroidModel::Anchor a{{f[0], f[1], f[2]}, {f[3], f[4], f[5]}, {f[6], f[7], f[8], f[9]}};
        if (n == "eye") { m.eye = a; eye = true; }
        else if (n == "exhaust_l") { m.exhaust[0] = a; ex[0] = true; }
        else if (n == "exhaust_r") { m.exhaust[1] = a; ex[1] = true; }
    }
    for (bool b : found) if (!b) return m;
    m.ok = eye && ex[0] && ex[1];
    return m;
}

const DroidModel& droidModel() {
    static const DroidModel m = [] {
        DroidModel d = parseDroidModel(assets::droid_mesh, assets::droid_mesh_size);
        if (!d.ok) { CS_LOGE("droid model missing or malformed: the title screen goes without it"); return d; }
        d.texture = assets::droid_jpg; d.textureSize = assets::droid_jpg_size;
        d.normalMap = assets::droid_n_jpg; d.normalMapSize = assets::droid_n_jpg_size;
        return d;
    }();
    return m;
}

} // namespace cs
