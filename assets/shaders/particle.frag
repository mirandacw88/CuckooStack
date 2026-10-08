#version 450
layout(push_constant) uniform Push { mat4 unused; vec4 kind; } P; // kind.x: 0 = dotTex, 1 = puffTex
layout(location = 0) in vec2 vUv;
layout(location = 1) flat in vec4 vColor;
layout(location = 0) out vec4 outColor;
float hash12(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
void main() {
    float r = length(vUv - 0.5) * 2.0;
    float a;
    if (P.kind.x < 0.5) {
        a = r < 0.22 ? mix(1.0, 0.85, r / 0.22) : mix(0.85, 0.0, clamp((r - 0.22) / 0.78, 0.0, 1.0));
    } else {
        float lump = 0.75 + 0.25 * hash12(floor(vUv * 5.0));
        a = (1.0 - smoothstep(0.55, 1.0, r)) * 0.8 * lump;
    }
    outColor = vec4(vColor.rgb, a * vColor.a);
}
