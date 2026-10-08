#version 450
// Full-screen triangle. P.rot pre-rotates clip space for Android surface transforms (identity elsewhere).
layout(push_constant) uniform Push { vec4 rot; vec4 params; vec4 extra; } P;
layout(location = 0) out vec2 vUv;
void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vUv = uv;
    vec2 ndc = uv * 2.0 - 1.0;
    gl_Position = vec4(mat2(P.rot.xy, P.rot.zw) * ndc, 0.0, 1.0);
}
