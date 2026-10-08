#version 450
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
// per instance (see cs::Instance)
layout(location = 3) in vec4 iModel0;
layout(location = 4) in vec4 iModel1;
layout(location = 5) in vec4 iModel2;
layout(location = 6) in vec4 iModel3;
layout(location = 7) in vec4 iColor;
layout(location = 8) in vec4 iEmissive;
layout(location = 9) in vec4 iRim;
layout(location = 10) in vec4 iParams;

layout(location = 0) out vec3 vWorld;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUv;
layout(location = 3) out vec3 vObj;
layout(location = 4) flat out vec4 vColor;
layout(location = 5) flat out vec4 vEmissive;
layout(location = 6) flat out vec4 vRim;
layout(location = 7) flat out vec4 vParams;

void main() {
    mat4 model = mat4(iModel0, iModel1, iModel2, iModel3);
    vec4 world = model * vec4(inPos, 1.0);
    // normal matrix without a per-vertex inverse: valid for rotation * scale (our transforms)
    mat3 m3 = mat3(model);
    vec3 invSq = 1.0 / max(vec3(dot(m3[0], m3[0]), dot(m3[1], m3[1]), dot(m3[2], m3[2])), vec3(1e-12));
    vNormal = m3 * (inNormal * invSq);
    vWorld = world.xyz;
    vObj = world.xyz - model[3].xyz;
    vUv = inUv;
    vColor = iColor; vEmissive = iEmissive; vRim = iRim; vParams = iParams;
    gl_Position = F.viewProj * world;
}
