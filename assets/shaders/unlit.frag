#version 450
// Shape masks replace the web build's small canvas gradient textures (padTex, curtainTex, streakTex, ...).
layout(location = 0) in vec2 vUv;
layout(location = 1) flat in vec4 vColor;
layout(location = 2) flat in vec4 vParams; // xy = quad size, z = shape, w = shape param
layout(location = 0) out vec4 outColor;

const int SOLID = 0, RADIAL = 1, CURTAIN = 2, STREAK = 3, FEATHER = 4, EGGCELL = 5, PILL = 6, BOTTOMFADE = 7, DISC = 8, HBEAM = 9, SCAN = 10, LASER = 11, SPOT = 12;

float aastep(float edge, float x) { float w = max(fwidth(x), 1e-4); return smoothstep(edge - w, edge + w, x); }

void main() {
    int shape = int(vParams.z + 0.5);
    vec2 uv = vUv;
    vec3 rgb = vColor.rgb;
    float a = 1.0;
    if (shape == RADIAL) {
        float r = length(uv - 0.5) * 2.0;
        a = r < 0.35 ? mix(0.9, 0.35, r / 0.35) : mix(0.35, 0.0, clamp((r - 0.35) / 0.65, 0.0, 1.0));
    } else if (shape == CURTAIN) {
        float t = uv.y;
        a = t < 0.25 ? mix(0.55, 0.18, t / 0.25) : mix(0.18, 0.0, (t - 0.25) / 0.75);
        a *= step(1.0, mod((1.0 - uv.y) * 128.0, 6.0)); // laser scan gaps
    } else if (shape == STREAK) {
        float t = 1.0 - uv.y;
        float v = t < 0.18 ? mix(0.0, 0.95, t / 0.18) : mix(0.95, 0.0, (t - 0.18) / 0.82);
        a = v * (1.0 - abs(uv.x * 2.0 - 1.0));
    } else if (shape == FEATHER) {
        vec2 p = (uv - 0.5) / vec2(0.36, 0.46);
        if (dot(p, p) > 1.0) discard;
        if (abs(uv.x - 0.5) < 0.03) rgb *= vec3(0.63, 0.78, 0.9);
    } else if (shape == EGGCELL) {
        // egg outline from the meter canvas; w = fill fraction from the bottom
        vec2 p = vec2(uv.x - 0.5, uv.y - 0.43);
        float rx = 0.46 * (1.0 - 0.28 * max(p.y, 0.0) / 0.55);
        float d = length(p / vec2(rx, p.y > 0.0 ? 0.55 : 0.43)) - 1.0;
        float inside = 1.0 - aastep(0.0, d);
        float outline = inside * aastep(-0.12, d);
        float filled = step(uv.y, 0.05 + vParams.w * 0.9) * step(0.001, vParams.w);
        a = max(inside * (filled > 0.5 ? 1.0 : 0.13), outline * 0.9);
        rgb = outline > 0.5 && vParams.w >= 1.0 ? vec3(0.83, 0.96, 1.0) : rgb;
    } else if (shape == PILL) {
        vec2 size = max(vParams.xy, vec2(1e-3));
        float m = min(size.x, size.y), r = m * 0.18, th = vParams.w * m;
        vec2 p = (uv - 0.5) * size;
        vec2 q = abs(p) - (size * 0.5 - r);
        float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
        a = (1.0 - aastep(0.0, d)) * aastep(-th, d);
    } else if (shape == BOTTOMFADE) {
        a = clamp(((1.0 - uv.y) - 0.4) / 0.6, 0.0, 1.0);
    } else if (shape == DISC) {
        a = 1.0 - aastep(0.5, length(uv - 0.5));
    } else if (shape == HBEAM) {
        a = 0.9 * (1.0 - abs(uv.x * 2.0 - 1.0));
    } else if (shape == LASER) {
        // thin beam: hot core across the width, fading toward the far end (uv.y = 0)
        float x = abs(uv.x * 2.0 - 1.0);
        a = (exp(-x * x * 9.0) * 0.8 + exp(-x * x * 120.0)) * mix(0.15, 1.0, uv.y);
    } else if (shape == SPOT) {
        // light cone seen from the side: apex at the top (uv.y = 1), widening and fading downward
        float halfW = mix(0.04, 0.5, 1.0 - uv.y);
        float x = abs(uv.x - 0.5) / halfW;
        a = (1.0 - smoothstep(0.55, 1.0, x)) * pow(uv.y, 0.6) * (0.35 + 0.65 * (1.0 - uv.y));
    } else if (shape == SCAN) {
        // .scan: repeating-linear-gradient(to bottom, rgba(234,250,255,.035) 0 1px, transparent 1px 3px)
        a = step(mod((1.0 - uv.y) * vParams.y, 3.0), 1.0);
    }
    outColor = vec4(rgb, a * vColor.a);
}
