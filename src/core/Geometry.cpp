#include "Geometry.h"
#include "DroidModel.h"
#include "HenModel.h"

namespace cs::geo {

namespace {
uint16_t idx(size_t i) { return static_cast<uint16_t>(i); }

void pushQuadGrid(MeshData& m, size_t a, size_t b, size_t c, size_t d) {
    m.indices.insert(m.indices.end(), {idx(a), idx(b), idx(d), idx(b), idx(c), idx(d)});
}
} // namespace

MeshData box(float width, float height, float depth) {
    MeshData m;
    // buildPlane(u, v, w, udir, vdir, width, height, depth) from three.js BoxGeometry; axes 0=x 1=y 2=z
    auto buildPlane = [&](int u, int v, int w, float udir, float vdir, float pw, float ph, float pd) {
        const size_t base = m.vertices.size();
        for (int iy = 0; iy <= 1; ++iy) {
            const float y = iy * ph - ph * 0.5f;
            for (int ix = 0; ix <= 1; ++ix) {
                const float x = ix * pw - pw * 0.5f;
                Vertex vert{};
                vert.pos[u] = x * udir; vert.pos[v] = y * vdir; vert.pos[w] = pd * 0.5f;
                vert.normal[w] = pd > 0 ? 1.f : -1.f;
                vert.uv = {float(ix), 1.f - float(iy)};
                m.vertices.push_back(vert);
            }
        }
        pushQuadGrid(m, base + 0, base + 2, base + 3, base + 1);
    };
    buildPlane(2, 1, 0, -1, -1, depth, height, width);   // px
    buildPlane(2, 1, 0, 1, -1, depth, height, -width);   // nx
    buildPlane(0, 2, 1, 1, 1, width, depth, height);     // py
    buildPlane(0, 2, 1, 1, -1, width, depth, -height);   // ny
    buildPlane(0, 1, 2, 1, -1, width, height, depth);    // pz
    buildPlane(0, 1, 2, -1, -1, width, height, -depth);  // nz
    return m;
}

MeshData frustum(float bottom, float top, float h) {
    MeshData m;
    const float b = bottom / 2, t = top / 2, y0 = -h / 2, y1 = h / 2;
    // the four sides, counter-clockwise seen from outside: corners (x, z) of the base, walked around +y
    const glm::vec2 c[4] = {{-1, 1}, {1, 1}, {1, -1}, {-1, -1}};
    for (int i = 0; i < 4; ++i) {
        const glm::vec2 a2 = c[i], b2 = c[(i + 1) % 4];
        const glm::vec3 p0{a2.x * b, y0, a2.y * b}, p1{b2.x * b, y0, b2.y * b}, p2{b2.x * t, y1, b2.y * t}, p3{a2.x * t, y1, a2.y * t};
        const glm::vec3 n = glm::normalize(glm::cross(p1 - p0, (top > 0 ? p3 : p2) - p0));
        const uint16_t base = uint16_t(m.vertices.size());
        m.vertices.push_back({p0, n, {0, 0}});
        m.vertices.push_back({p1, n, {1, 0}});
        if (top > 0) {
            m.vertices.push_back({p2, n, {1, 1}});
            m.vertices.push_back({p3, n, {0, 1}});
            for (uint16_t k : {0, 1, 2, 0, 2, 3}) m.indices.push_back(uint16_t(base + k));
        } else {
            m.vertices.push_back({glm::vec3(0, y1, 0), n, {0.5f, 1}});
            for (uint16_t k : {0, 1, 2}) m.indices.push_back(uint16_t(base + k));
        }
    }
    auto cap = [&](float r, float y, float ny) { // a square face, facing ny
        const uint16_t base = uint16_t(m.vertices.size());
        for (const glm::vec2& q : c) m.vertices.push_back({{q.x * r, y, q.y * r}, {0, ny, 0}, {q.x * 0.5f + 0.5f, q.y * 0.5f + 0.5f}});
        if (ny > 0) for (uint16_t k : {0, 1, 2, 0, 2, 3}) m.indices.push_back(uint16_t(base + k));
        else for (uint16_t k : {0, 2, 1, 0, 3, 2}) m.indices.push_back(uint16_t(base + k));
    };
    cap(b, y0, -1.f);
    if (top > 0) cap(t, y1, 1.f);
    return m;
}

MeshData sphere(float r, int ws, int hs) {
    MeshData m;
    std::vector<std::vector<size_t>> grid(hs + 1);
    for (int iy = 0; iy <= hs; ++iy) {
        const float v = float(iy) / hs;
        const float uOffset = iy == 0 ? 0.5f / ws : iy == hs ? -0.5f / ws : 0.f;
        for (int ix = 0; ix <= ws; ++ix) {
            const float u = float(ix) / ws;
            glm::vec3 p{-r * std::cos(u * 2 * kPi) * std::sin(v * kPi), r * std::cos(v * kPi), r * std::sin(u * 2 * kPi) * std::sin(v * kPi)};
            grid[iy].push_back(m.vertices.size());
            m.vertices.push_back({p, glm::length(p) > 0 ? glm::normalize(p) : glm::vec3(0, 1, 0), {u + uOffset, 1.f - v}});
        }
    }
    for (int iy = 0; iy < hs; ++iy)
        for (int ix = 0; ix < ws; ++ix) {
            const size_t a = grid[iy][ix + 1], b = grid[iy][ix], c = grid[iy + 1][ix], d = grid[iy + 1][ix + 1];
            if (iy != 0) m.indices.insert(m.indices.end(), {idx(a), idx(b), idx(d)});
            if (iy != hs - 1) m.indices.insert(m.indices.end(), {idx(b), idx(c), idx(d)});
        }
    return m;
}

MeshData cylinder(float rt, float rb, float height, int rs) {
    MeshData m;
    const float half = height * 0.5f, slope = (rb - rt) / height;
    std::vector<std::vector<size_t>> grid(2);
    for (int y = 0; y <= 1; ++y) {
        const float v = float(y), radius = v * (rb - rt) + rt;
        for (int x = 0; x <= rs; ++x) {
            const float u = float(x) / rs, th = u * 2 * kPi, s = std::sin(th), c = std::cos(th);
            grid[y].push_back(m.vertices.size());
            m.vertices.push_back({{radius * s, -v * height + half, radius * c}, glm::normalize(glm::vec3(s, slope, c)), {u, 1.f - v}});
        }
    }
    for (int x = 0; x < rs; ++x) pushQuadGrid(m, grid[0][x], grid[1][x], grid[1][x + 1], grid[0][x + 1]);
    auto cap = [&](bool top) {
        const float sign = top ? 1.f : -1.f, radius = top ? rt : rb;
        const size_t centerStart = m.vertices.size();
        for (int x = 1; x <= rs; ++x) m.vertices.push_back({{0, half * sign, 0}, {0, sign, 0}, {0.5f, 0.5f}});
        const size_t centerEnd = m.vertices.size();
        for (int x = 0; x <= rs; ++x) {
            const float th = float(x) / rs * 2 * kPi, s = std::sin(th), c = std::cos(th);
            m.vertices.push_back({{radius * s, half * sign, radius * c}, {0, sign, 0}, {c * 0.5f + 0.5f, s * 0.5f * sign + 0.5f}});
        }
        for (int x = 0; x < rs; ++x) {
            const size_t c = centerStart + x, i = centerEnd + x;
            if (top) m.indices.insert(m.indices.end(), {idx(i), idx(i + 1), idx(c)});
            else m.indices.insert(m.indices.end(), {idx(i + 1), idx(i), idx(c)});
        }
    };
    if (rt > 0) cap(true);
    if (rb > 0) cap(false);
    return m;
}

MeshData torus(float R, float r, int radialSegments, int tubularSegments, float arc) {
    MeshData m;
    for (int j = 0; j <= radialSegments; ++j)
        for (int i = 0; i <= tubularSegments; ++i) {
            const float u = float(i) / tubularSegments * arc, v = float(j) / radialSegments * 2 * kPi;
            glm::vec3 p{(R + r * std::cos(v)) * std::cos(u), (R + r * std::cos(v)) * std::sin(u), r * std::sin(v)};
            glm::vec3 center{R * std::cos(u), R * std::sin(u), 0};
            m.vertices.push_back({p, glm::normalize(p - center), {float(i) / tubularSegments, float(j) / radialSegments}});
        }
    const size_t row = size_t(tubularSegments) + 1;
    for (int j = 1; j <= radialSegments; ++j)
        for (int i = 1; i <= tubularSegments; ++i)
            pushQuadGrid(m, row * j + i - 1, row * (j - 1) + i - 1, row * (j - 1) + i, row * j + i);
    return m;
}

MeshData lathe(const std::vector<glm::vec2>& pts, int segments, float phiLength) {
    MeshData m;
    const size_t n = pts.size();
    for (int i = 0; i <= segments; ++i) {
        const float phi = float(i) / segments * phiLength, s = std::sin(phi), c = std::cos(phi);
        for (size_t j = 0; j < n; ++j) {
            // analytic profile normal: perpendicular to the local tangent of the profile
            const glm::vec2 t = pts[std::min(j + 1, n - 1)] - pts[j == 0 ? 0 : j - 1];
            glm::vec2 n2 = glm::length(t) > 0 ? glm::normalize(glm::vec2(t.y, -t.x)) : glm::vec2(1, 0);
            m.vertices.push_back({{pts[j].x * s, pts[j].y, pts[j].x * c}, glm::normalize(glm::vec3(n2.x * s, n2.y, n2.x * c)),
                                  {float(i) / segments, float(j) / float(n - 1)}});
        }
    }
    for (int i = 0; i < segments; ++i)
        for (size_t j = 0; j + 1 < n; ++j) {
            const size_t base = j + i * n, a = base, b = base + n, c = base + n + 1, d = base + 1;
            m.indices.insert(m.indices.end(), {idx(a), idx(b), idx(d), idx(c), idx(d), idx(b)});
        }
    return m;
}

MeshData latheCap(const std::vector<glm::vec2>& pts) {
    MeshData m;
    float lo = 1e9f, hi = -1e9f, r = 1e-6f;
    for (const glm::vec2& p : pts) { lo = std::min(lo, p.y); hi = std::max(hi, p.y); r = std::max(r, p.x); }
    for (const glm::vec2& p : pts) // (the turn's two ends: phi = 0 at +z, phi = pi at -z)
        for (float side : {1.f, -1.f})
            m.vertices.push_back({{0, p.y, side * p.x}, {-1, 0, 0}, {0.5f + side * p.x / (2 * r), (p.y - lo) / std::max(1e-6f, hi - lo)}});
    for (size_t j = 0; j + 1 < pts.size(); ++j) {
        const size_t a = 2 * j, b = 2 * j + 1, c = 2 * j + 2, d = 2 * j + 3; // a, c: the +z edge; b, d: the -z edge
        m.indices.insert(m.indices.end(), {idx(a), idx(c), idx(d), idx(a), idx(d), idx(b)});
    }
    return m;
}

MeshData sawTeeth(int teeth, float gullet, float inner, float thickness) {
    MeshData m;
    // the outline, counter-clockwise: per tooth its gullet, its tip just past it (the steep cutting face, leading as the
    // blade turns clockwise), and a point on its back sloping down to the next gullet (slightly bowed out)
    std::vector<glm::vec2> rim;
    std::vector<float> ang;
    const float step = 2 * kPi / float(teeth);
    for (int k = 0; k < teeth; ++k) {
        const float a0 = float(k) * step;
        const float r[3] = {gullet, 1.f, gullet + (1.f - gullet) * 0.58f}, f[3] = {0.f, 0.1f, 0.55f};
        for (int i = 0; i < 3; ++i) {
            const float a = a0 + f[i] * step;
            rim.push_back({r[i] * std::cos(a), r[i] * std::sin(a)});
            ang.push_back(a);
        }
    }
    const size_t n = rim.size();
    const float h = thickness * 0.5f;
    for (float side : {1.f, -1.f}) { // the two faces: rings of (hole edge, outline) pairs
        const size_t base = m.vertices.size();
        for (size_t i = 0; i <= n; ++i) {
            const size_t k = i % n;
            const glm::vec2 in{inner * std::cos(ang[k]), inner * std::sin(ang[k])};
            const float u = float(i) / float(n);
            m.vertices.push_back({{in.x, in.y, side * h}, {0, 0, side}, {u, 0}});
            m.vertices.push_back({{rim[k].x, rim[k].y, side * h}, {0, 0, side}, {u, 1}});
        }
        for (size_t i = 0; i < n; ++i) {
            const size_t a = base + 2 * i, b = a + 1, c = a + 3, d = a + 2; // in, out, next out, next in
            if (side > 0) m.indices.insert(m.indices.end(), {idx(a), idx(b), idx(c), idx(a), idx(c), idx(d)});
            else m.indices.insert(m.indices.end(), {idx(a), idx(c), idx(b), idx(a), idx(d), idx(c)});
        }
    }
    for (size_t i = 0; i < n; ++i) { // the outline's edge, a flat quad per stretch, facing out
        const glm::vec2 p = rim[i], q = rim[(i + 1) % n], e = q - p;
        const glm::vec3 nrm = glm::normalize(glm::vec3(e.y, -e.x, 0));
        const size_t base = m.vertices.size();
        m.vertices.push_back({{p.x, p.y, h}, nrm, {0, 0}});
        m.vertices.push_back({{p.x, p.y, -h}, nrm, {0, 1}});
        m.vertices.push_back({{q.x, q.y, -h}, nrm, {1, 1}});
        m.vertices.push_back({{q.x, q.y, h}, nrm, {1, 0}});
        m.indices.insert(m.indices.end(), {idx(base), idx(base + 1), idx(base + 2), idx(base), idx(base + 2), idx(base + 3)});
    }
    return m;
}

MeshData plane(float w, float h) {
    MeshData m;
    for (int iy = 0; iy <= 1; ++iy)
        for (int ix = 0; ix <= 1; ++ix)
            m.vertices.push_back({{ix * w - w * 0.5f, -(iy * h - h * 0.5f), 0}, {0, 0, 1}, {float(ix), 1.f - float(iy)}});
    pushQuadGrid(m, 0, 2, 3, 1);
    return m;
}

MeshData ring(float inner, float outer, int ts) {
    MeshData m;
    for (int j = 0; j <= 1; ++j) {
        const float radius = j == 0 ? inner : outer;
        for (int i = 0; i <= ts; ++i) {
            const float a = float(i) / ts * 2 * kPi;
            glm::vec3 p{radius * std::cos(a), radius * std::sin(a), 0};
            m.vertices.push_back({p, {0, 0, 1}, {(p.x / outer + 1) * 0.5f, (p.y / outer + 1) * 0.5f}});
        }
    }
    for (int i = 0; i < ts; ++i) pushQuadGrid(m, i, i + ts + 1, i + ts + 2, i + 1);
    return m;
}

MeshData circle(float r, int segments) {
    MeshData m;
    m.vertices.push_back({{0, 0, 0}, {0, 0, 1}, {0.5f, 0.5f}});
    for (int s = 0; s <= segments; ++s) {
        const float a = float(s) / segments * 2 * kPi;
        m.vertices.push_back({{r * std::cos(a), r * std::sin(a), 0}, {0, 0, 1}, {(std::cos(a) + 1) * 0.5f, (std::sin(a) + 1) * 0.5f}});
    }
    for (int i = 1; i <= segments; ++i) m.indices.insert(m.indices.end(), {idx(i), idx(i + 1), idx(0)});
    return m;
}

} // namespace cs::geo

namespace cs {

std::vector<MeshData> buildMeshLibrary() {
    std::vector<MeshData> lib(kMeshCount);
    auto at = [&](MeshId id) -> MeshData& { return lib[static_cast<size_t>(id)]; };
    at(MeshId::Box) = geo::box(1, 1, 1);
    at(MeshId::Sphere) = geo::sphere(1, 32, 24);
    at(MeshId::SphereLow) = geo::sphere(1, 24, 12);
    at(MeshId::Cylinder) = geo::cylinder(1, 1, 1, 12);
    at(MeshId::Cone) = geo::cylinder(0, 1, 1, 12);
    {
        // eggGeo: lathe of a slightly pointed ellipse, U * 0.49 tall half-height
        constexpr float U = 0.6f, h = U * 0.49f, R = 0.25f;
        std::vector<glm::vec2> pts;
        for (int i = 0; i <= 32; ++i) {
            const float t = float(i) / 32 * kPi;
            pts.push_back({std::max(0.0001f, std::sin(t) * R * (1 + 0.13f * std::cos(t))), -std::cos(t) * h});
        }
        at(MeshId::Egg) = geo::lathe(pts, 40);
        at(MeshId::EggHalf) = geo::lathe(pts, 20, kPi);   // sawn lengthwise (GameSaw.cpp)
        at(MeshId::EggHalfCap) = geo::latheCap(pts);
    }
    at(MeshId::SawTeeth) = geo::sawTeeth(16, 0.84f, 0.74f, 0.1f);
    at(MeshId::EggRing) = geo::torus(0.254f, 0.022f, 10, 48, 2 * kPi);
    at(MeshId::Visor) = geo::torus(0.188f, 0.04f, 12, 40, 2.5f);
    at(MeshId::SmallRing) = geo::torus(0.075f, 0.018f, 8, 20, 2 * kPi);
    at(MeshId::Plane) = geo::plane(1, 1);
    at(MeshId::RingFlat) = geo::ring(0.86f, 1.f, 64);
    at(MeshId::Circle) = geo::circle(1, 24);
    at(MeshId::Pyramid) = geo::frustum(1, 0, 1);
    at(MeshId::Frustum) = geo::frustum(1, 0.7f, 1);
    // the title screen's jet droid, one mesh per part (DroidModel.h); empty if its asset is missing
    if (const DroidModel& droid = droidModel(); droid.ok)
        for (int p = 0; p < DroidModel::kParts; ++p) {
            MeshData& m = at(MeshId(int(MeshId::DroidBody) + p));
            for (const DroidModel::V& v : droid.parts[size_t(p)].verts) m.vertices.push_back({v.pos, v.normal, v.uv});
            m.indices.assign(droid.parts[size_t(p)].indices.begin(), droid.parts[size_t(p)].indices.end());
        }
    // the hen model in its rest pose (the renderer streams the posed vertices over these every frame)
    for (int id = 0; id <= kOutfitModelCount; ++id) {
        const HenModel& hen = henModel(HenModelId(id));
        if (!hen.ok) continue;
        for (int r = 0; r < hen.regions; ++r) {
            MeshData& m = at(MeshId(int(hen.firstMesh) + r));
            for (const HenModel::V& v : hen.verts[size_t(r)]) m.vertices.push_back({v.pos, v.normal, v.uv});
            m.indices = hen.indices[size_t(r)];
        }
    }
    return lib;
}

} // namespace cs
