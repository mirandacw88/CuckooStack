// Procedural mesh generation. Ports of the three.js geometry generators used by the web build
// (BoxGeometry, SphereGeometry, CylinderGeometry, TorusGeometry, LatheGeometry, PlaneGeometry,
// RingGeometry, CircleGeometry) with identical vertex order and counter-clockwise front faces.
#pragma once

#include "RenderList.h"

#include <cstdint>
#include <vector>

namespace cs {

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
};

namespace geo {
MeshData box(float w, float h, float d);
MeshData sphere(float r, int widthSegments, int heightSegments);
MeshData cylinder(float radiusTop, float radiusBottom, float height, int radialSegments);
MeshData torus(float radius, float tube, int radialSegments, int tubularSegments, float arc);
// (phiLength < 2 pi: part of the turn, from +z through +x, so a half turn is the x >= 0 half)
MeshData lathe(const std::vector<glm::vec2>& points, int segments, float phiLength = 6.2831853f);
// the flat face closing a lathe cut in half (phiLength = pi): the profile's outline in the x = 0 plane, facing -x
MeshData latheCap(const std::vector<glm::vec2>& points);
// a saw blade's toothed rim in XY, `thickness` along z: `teeth` hooked teeth (tips at r = 1, gullets at `gullet`)
// round a hole of radius `inner`; flat-shaded; each tooth's steep face leads clockwise (seen from +z)
MeshData sawTeeth(int teeth, float gullet, float inner, float thickness);
MeshData plane(float w, float h);
MeshData ring(float inner, float outer, int thetaSegments);
MeshData circle(float r, int segments);
// a square frustum, centred: base `bottom` wide at y = -h/2, top `top` wide at y = +h/2 (top 0: a pyramid);
// flat-shaded (each face its own vertices and normal), uv = (across the face, up it)
MeshData frustum(float bottom, float top, float h);
} // namespace geo

// Every mesh in MeshId order.
std::vector<MeshData> buildMeshLibrary();

} // namespace cs
