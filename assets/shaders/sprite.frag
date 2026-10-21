#version 450
// World-space sprites (Neon Egypt's backdrop): a premultiplied atlas of sRGB art drawn into the linear HDR scene, so
// the art is made linear here. vColor.rgb tints it (over 1: brighter) and vColor.a is its opacity; vSpread lifts its
// bright neon lines (into the bloom) more than its dark bodies; vGlow.rgb is the night haze it fades toward, by vGlow.a
// (the far layers furthest). vAdd 1 makes it light: added to what's behind, covering none of it (the backdrop's glows,
// drawn in their place among its sprites). vSil 1 makes it a silhouette: its shape filled flat with vGlow.rgb, only its
// outline lit, in vColor.rgb (backlit by the moon).
layout(set = 0, binding = 0) uniform sampler2D tImage;
layout(location = 0) in vec2 vUv;
layout(location = 1) flat in vec4 vColor;
layout(location = 2) flat in vec4 vGlow;
layout(location = 3) flat in float vSpread;
layout(location = 4) flat in float vAdd;
layout(location = 5) flat in float vSil;
layout(location = 0) out vec4 outColor;
vec3 toLinear(vec3 c) { return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c)); }
void main() {
    vec4 t = texture(tImage, vUv); // premultiplied
    float edge = clamp(fwidth(t.a) * 1.4, 0.0, 1.0); // (its outline: where its alpha changes fast on screen; before any discard)
    if (t.a < 0.003) discard;
    vec3 c = toLinear(clamp(t.rgb / t.a, 0.0, 1.0));
    float neon = smoothstep(0.25, 0.85, max(c.r, max(c.g, c.b)));
    c *= vColor.rgb * (1.0 + vSpread * neon);
    c = mix(c, vGlow.rgb, vGlow.a);
    c = mix(c, vGlow.rgb + vColor.rgb * edge, vSil);
    outColor = vec4(c * t.a, t.a * (1.0 - vAdd)) * vColor.a;
}
