#version 450
// chromaPass from the web build: chromatic aberration + vignette + ACES (RRT/ODT fit) + sRGB, plus bloom add.
// Frosted glass: inside one rounded rect (a HUD glass panel) the scene is replaced by a grainy disc blur of itself.
layout(set = 0, binding = 0) uniform sampler2D tHdr;
layout(set = 0, binding = 1) uniform sampler2D tBloom;
// params: amount, vignette, bloom, encodeSrgb | extra: tint rgb, strength
// frost: centre uv, half size (screen heights) | frost2: corner radius (screen heights), aspect w/h, amount, blur radius
layout(push_constant) uniform Push { vec4 rot; vec4 params; vec4 extra; vec4 frost; vec4 frost2; } P;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

vec3 rrt(vec3 v) { vec3 a = v * (v + 0.0245786) - 0.000090537; vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081; return a / b; }
vec3 aces(vec3 c) {
    const mat3 inM = mat3(vec3(0.59719, 0.07600, 0.02840), vec3(0.35458, 0.90834, 0.13383), vec3(0.04823, 0.01566, 0.83777));
    const mat3 outM = mat3(vec3(1.60475, -0.10208, -0.00327), vec3(-0.53108, 1.10813, -0.07276), vec3(-0.07367, -0.00605, 1.07602));
    c = outM * rrt(inM * (c / 0.6));
    return clamp(c, 0.0, 1.0);
}
vec3 srgb(vec3 c) { return mix(pow(c, vec3(0.41666)) * 1.055 - 0.055, c * 12.92, vec3(lessThanEqual(c, vec3(0.0031308)))); }

// signed distance to the frost rect, in screen heights (negative inside)
float frostDist() {
    vec2 p = (vUv - P.frost.xy) * vec2(P.frost2.y, 1.0);
    vec2 q = abs(p) - P.frost.zw + P.frost2.x;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - P.frost2.x;
}

// 24-tap Vogel disc, rotated per pixel by interleaved-gradient noise: a soft blur with a fine frosted grain
vec3 frosted(float radius) {
    float n = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)))) * 6.2831853;
    vec3 acc = vec3(0.0);
    for (int i = 0; i < 24; ++i) {
        float r = sqrt((float(i) + 0.5) / 24.0) * radius;
        float a = float(i) * 2.3999632 + n;
        acc += texture(tHdr, vUv + vec2(cos(a) / P.frost2.y, sin(a)) * r).rgb;
    }
    return acc / 24.0;
}

void main() {
    vec2 d = vUv - 0.5;
    float r2 = dot(d, d);
    vec2 off = d * P.params.x * 0.022 * (0.35 + r2 * 4.0);
    vec3 c = vec3(texture(tHdr, vUv + off).r, texture(tHdr, vUv).g, texture(tHdr, vUv - off).b);
    if (P.frost2.z > 0.0) {
        float inside = (1.0 - smoothstep(-0.002, 0.0, frostDist())) * P.frost2.z;
        if (inside > 0.0) c = mix(c, frosted(P.frost2.w), inside);
    }
    c += texture(tBloom, vUv).rgb * P.params.z;
    if (any(isnan(c))) c = vec3(0.0);
    c = clamp(c, 0.0, 60.0) * (1.0 - P.params.y * smoothstep(0.08, 0.6, r2 * 2.0));
    c = aces(c);
    // party-mode colour edge (display space, like a CSS overlay): only the borders, never the centre
    float edge = smoothstep(0.42, 0.95, r2 * 2.0); // outer border only
    c += P.extra.rgb * P.extra.a * edge;
    outColor = vec4(P.params.w > 0.5 ? srgb(c) : c, 1.0);
}
