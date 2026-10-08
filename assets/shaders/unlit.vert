#version 450
// MeshBasicMaterial-style quads and meshes (glows, curtains, rings, rain, HUD).
// The matrix comes from push constants so the same pipeline layout serves world and HUD space.
layout(push_constant) uniform Push { mat4 viewProj; } P;
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 iModel0;
layout(location = 4) in vec4 iModel1;
layout(location = 5) in vec4 iModel2;
layout(location = 6) in vec4 iModel3;
layout(location = 7) in vec4 iColor;
layout(location = 8) in vec4 iEmissive;
layout(location = 9) in vec4 iRim;
layout(location = 10) in vec4 iParams;
layout(location = 0) out vec2 vUv;
layout(location = 1) flat out vec4 vColor;
layout(location = 2) flat out vec4 vParams;
void main() {
    mat4 model = mat4(iModel0, iModel1, iModel2, iModel3);
    vUv = inUv;
    vColor = iColor;
    vParams = iParams;
    gl_Position = P.viewProj * (model * vec4(inPos, 1.0));
}
