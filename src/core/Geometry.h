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
MeshData lathe(const std::vector<glm::vec2>& points, int segments);
MeshData plane(float w, float h);
MeshData ring(float inner, float outer, int thetaSegments);
MeshData circle(float r, int segments);
} // namespace geo

// Every mesh in MeshId order.
std::vector<MeshData> buildMeshLibrary();

} // namespace cs
