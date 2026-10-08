#version 450
// Signed-distance-field text: 0.5 is the glyph edge, the stored spread is 12 px at a 48 px em.
// The glow (CSS text-shadow blur) is a soft falloff outside the edge, composited under the glyph.
layout(set = 0, binding = 0) uniform sampler2D tAtlas;
layout(location = 0) in vec2 vUv;
layout(location = 1) flat in vec4 vColor;
layout(location = 2) flat in vec4 vGlow;
layout(location = 3) flat in float vSpread;
layout(location = 0) out vec4 outColor;
void main() {
    float d = texture(tAtlas, vUv).r;
    float w = max(fwidth(d) * 0.75, 1e-3);
    float fill = smoothstep(0.5 - w, 0.5 + w, d);
    float glow = 0.0;
    if (vSpread > 0.0) {
        float g = clamp((d - (0.5 - vSpread)) / vSpread, 0.0, 1.0);
        glow = g * g * vGlow.a;
    }
    float a = fill * vColor.a;
    // "over" composite of text on its glow (straight alpha)
    float outA = a + glow * (1.0 - a);
    vec3 rgb = outA > 0.0 ? (vColor.rgb * a + vGlow.rgb * glow * (1.0 - a)) / outA : vColor.rgb;
    if (outA < 0.003) discard;
    outColor = vec4(rgb, outA);
}
