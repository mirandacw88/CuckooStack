// Per-frame constants shared by every scene shader. Mirrors GpuFrame in Renderer.cpp (std140).
layout(set = 0, binding = 0, std140) uniform Frame {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    mat4 invViewProj;
    vec4 cameraPos;   // w = time
    vec4 fogColor;    // w = FogExp2 density
    vec4 skyTop;
    vec4 skyMid;
    vec4 skyBot;
    vec4 skyHaze;
    vec4 moonDir;
    vec4 hemiSky;     // w = intensity
    vec4 hemiGround;
    vec4 sunDir;      // towards the light
    vec4 sunColor;    // rgb * intensity
    vec4 pointPos[3]; // w = cutoff distance
    vec4 pointColor[3];
    vec4 misc;        // x = beat pulse
} F;

vec3 skyGradient(vec3 d) {
    float h = d.y;
    vec3 c = mix(F.skyMid.rgb, F.skyTop.rgb, smoothstep(0.05, 0.7, h));
    c = mix(F.skyBot.rgb, c, smoothstep(-0.05, 0.28, h));
    c += F.skyHaze.rgb * exp(-max(h, 0.0) * 9.0) * 0.36;
    return c;
}
