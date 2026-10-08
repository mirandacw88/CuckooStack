#version 450
// Full-colour HUD sprites (premultiplied RGBA atlas). vColor tints the sprite; vColor.a is the overall opacity.
layout(set = 0, binding = 0) uniform sampler2D tImage;
layout(location = 0) in vec2 vUv;
layout(location = 1) flat in vec4 vColor;
layout(location = 2) flat in vec4 vGlow;
layout(location = 3) flat in float vSpread;
layout(location = 0) out vec4 outColor;
void main() {
    vec4 t = texture(tImage, vUv);              // premultiplied
    outColor = vec4(t.rgb * vColor.rgb, t.a) * vColor.a;
}
