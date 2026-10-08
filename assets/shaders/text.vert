#version 450
// SDF glyph quads (HUD and world). Per instance: model, colour, atlas UV rect (emissive slot), glow (rim slot).
layout(push_constant) uniform Push { mat4 viewProj; vec4 extra; } P;
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 iModel0;
layout(location = 4) in vec4 iModel1;
layout(location = 5) in vec4 iModel2;
layout(location = 6) in vec4 iModel3;
layout(location = 7) in vec4 iColor;
layout(location = 8) in vec4 iUvRect;
layout(location = 9) in vec4 iGlow;
layout(location = 10) in vec4 iParams;
layout(location = 0) out vec2 vUv;
layout(location = 1) flat out vec4 vColor;
layout(location = 2) flat out vec4 vGlow;
layout(location = 3) flat out float vSpread;
void main() {
    mat4 model = mat4(iModel0, iModel1, iModel2, iModel3);
    // mesh uv.y = 1 at the top of the quad; atlas rows grow downward
    vUv = mix(iUvRect.xy, iUvRect.zw, vec2(inUv.x, 1.0 - inUv.y));
    vColor = iColor;
    vGlow = iGlow;
    vSpread = iParams.x;
    gl_Position = P.viewProj * (model * vec4(inPos, 1.0));
}
