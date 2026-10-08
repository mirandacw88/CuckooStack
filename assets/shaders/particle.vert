#version 450
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
// Camera-facing quads for the FX point systems (gl_PointSize > 1 needs the optional largePoints feature).
layout(location = 0) in vec4 iPosSize;
layout(location = 1) in vec4 iColorAlpha;
layout(location = 0) out vec2 vUv;
layout(location = 1) flat out vec4 vColor;
void main() {
    vec2 corner = vec2(gl_VertexIndex & 1, (gl_VertexIndex >> 1) & 1);
    vec3 right = vec3(F.view[0][0], F.view[1][0], F.view[2][0]);
    vec3 up = vec3(F.view[0][1], F.view[1][1], F.view[2][1]);
    vec3 p = iPosSize.xyz + (right * (corner.x - 0.5) + up * (corner.y - 0.5)) * iPosSize.w;
    vUv = corner;
    vColor = iColorAlpha;
    gl_Position = F.viewProj * vec4(p, 1.0);
}
