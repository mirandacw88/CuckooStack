#version 450
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
// Port of skyMat: gradient, magenta horizon haze, moon disc + halo, hashed star field.
layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColor;
void main() {
    vec4 w = F.invViewProj * vec4(vNdc, 1.0, 1.0);
    vec3 d = normalize(w.xyz / w.w - F.cameraPos.xyz);
    float h = d.y;
    vec3 c = skyGradient(d);
    float m = max(dot(d, F.moonDir.xyz), 0.0);
    c += vec3(0.78, 0.86, 1.0) * (smoothstep(0.99925, 0.9996, m) * 1.7 + pow(m, 70.0) * 0.14);
    vec3 sp = floor(d * 360.0);
    float n = fract(sin(dot(sp, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
    c += vec3(0.9, 0.95, 1.0) * step(0.9975, n) * smoothstep(0.15, 0.5, h);
    outColor = vec4(c, 1.0);
}
