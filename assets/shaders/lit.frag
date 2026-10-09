#version 450
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
// Approximation of three.js MeshStandardMaterial as the web build configures it:
// GGX + Lambert, hemisphere light, moonlight, two point lights, an analytic stand-in for the PMREM
// neon environment, fresnel rim glow (addRim), FogExp2. Canvas textures are evaluated procedurally.

layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUv;
layout(location = 3) in vec3 vObj;
layout(location = 4) flat in vec4 vColor;
layout(location = 5) flat in vec4 vEmissive;
layout(location = 6) flat in vec4 vRim;
layout(location = 7) flat in vec4 vParams;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 1) uniform sampler2D tPuddle; // baked once at startup (BakedPatterns.cpp), tiles every 6 m
layout(set = 0, binding = 2) uniform sampler2D tHero;   // the textured hen model's albedo (HenModel)
layout(set = 0, binding = 3) uniform sampler2D tHeroN;  // its tangent-space normal map (sculpted detail)

const float PI = 3.14159265;
const int P_CONTAINER = 1, P_HAZARD = 2, P_ASPHALT = 3, P_WINDOWS = 4, P_DISCO = 5, P_HERO = 6;

vec3 lin(vec3 srgb) { return pow(srgb, vec3(2.2)); }
float hash12(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

// stand-in for scene.environment: night sky, dark floor, and the five neon panels around the origin
vec3 envColor(vec3 d, float rough) {
    vec3 c = d.y > -0.12 ? skyGradient(d) : lin(vec3(0.039, 0.031, 0.071));
    c = mix(c, (F.skyMid.rgb + F.skyBot.rgb) * 0.5, rough * 0.7);
    // each 3x6 m panel spans ~10 degrees (1 - cos ~ 0.015); blurrier lobes spread the same energy wider
    float w = 0.015 + 0.5 * rough * rough, k = 0.015 / w;
    c += lin(vec3(1.0, 0.169, 0.839)) * 5.0 * k * exp((dot(d, normalize(vec3(-6, 2, -6))) - 1.0) / w);
    c += lin(vec3(0.161, 0.906, 1.0)) * 4.0 * k * exp((dot(d, normalize(vec3(6, 3, -5))) - 1.0) / w);
    c += lin(vec3(1.0, 0.169, 0.839)) * 3.0 * k * exp((dot(d, normalize(vec3(7, 1, 6))) - 1.0) / w);
    c += lin(vec3(0.161, 0.906, 1.0)) * 3.0 * k * exp((dot(d, normalize(vec3(-7, 4, 5))) - 1.0) / w);
    c += lin(vec3(0.541, 0.357, 1.0)) * 1.5 * k * exp((dot(d, normalize(vec3(0, 6, -8))) - 1.0) / w);
    return c;
}

// Karis' analytic DFG approximation (same one three.js uses for EnvironmentBRDF)
vec3 envBRDF(vec3 f0, float rough, float nv) {
    const vec4 c0 = vec4(-1, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1, 0.0425, 1.04, -0.04);
    vec4 r = rough * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * nv)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return f0 * ab.x + ab.y;
}

vec3 direct(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 diff, vec3 f0, float a) {
    float nl = max(dot(N, L), 0.0);
    if (nl <= 0.0) return vec3(0.0);
    vec3 H = normalize(L + V);
    float nv = max(dot(N, V), 1e-4), nh = max(dot(N, H), 0.0), vh = max(dot(V, H), 0.0);
    float a2 = a * a, dd = nh * nh * (a2 - 1.0) + 1.0;
    float D = a2 / (PI * dd * dd);
    float gl = nl * sqrt(nv * nv * (1.0 - a2) + a2), gv = nv * sqrt(nl * nl * (1.0 - a2) + a2);
    float Vis = 0.5 / max(gl + gv, 1e-5);
    vec3 Fr = f0 + (1.0 - f0) * pow(1.0 - vh, 5.0);
    return (diff / PI + Fr * (Vis * D)) * radiance * nl;
}

float edgeDist(vec2 px, float size) { return min(min(px.x, px.y), min(size - px.x, size - px.y)); }

void main() {
    vec3 emissive = vEmissive.rgb;
    vec3 color;
    if (vParams.x < 0.0) {
        color = emissive; // MeshBasicMaterial
    } else {
        vec3 N = normalize(vNormal);
        if (!gl_FrontFacing) N = -N; // two-sided pass (glass shards): light the inside of the shell too
        vec3 V = normalize(F.cameraPos.xyz - vWorld);
        vec3 albedo = vColor.rgb;
        float metal = vParams.x, rough = vParams.y;
        int pattern = int(vParams.z + 0.5);

        if (pattern == P_HERO) {
            // the textured hen: the normal map's detail on a per-pixel tangent frame rebuilt from the derivatives of
            // position and uv (no vertex tangents needed), then albedo from its texture; hot texels glow
            vec3 dp1 = dFdx(vWorld), dp2 = dFdy(vWorld);
            vec2 duv1 = dFdx(vUv), duv2 = dFdy(vUv);
            vec3 dp2perp = cross(dp2, N), dp1perp = cross(N, dp1);
            vec3 Tg = dp2perp * duv1.x + dp1perp * duv2.x, Bg = dp2perp * duv1.y + dp1perp * duv2.y;
            float invmax = inversesqrt(max(max(dot(Tg, Tg), dot(Bg, Bg)), 1e-20));
            vec3 nt = texture(tHeroN, vUv).xyz * 2.0 - 1.0;
            N = normalize(mat3(Tg * invmax, Bg * invmax, N) * nt);
            vec3 tx = lin(texture(tHero, vUv).rgb);
            albedo *= tx;
            float heat = smoothstep(0.18, 0.55, dot(tx, vec3(0.6, 0.35, 0.05)));
            emissive = tx * heat * vEmissive.rgb;
        } else if (pattern == P_CONTAINER) {
            // contMap / contEm: corrugated steel, dark frame, amber hazard trim
            vec2 px = vUv * 512.0;
            float e = edgeDist(px, 512.0);
            vec3 base = mod(px.x, 18.0) < 9.0 ? (mod(floor(px.x / 18.0), 2.0) > 0.5 ? lin(vec3(0.337, 0.380, 0.478)) : lin(vec3(0.235, 0.271, 0.345)))
                                               : lin(vec3(0.290, 0.329, 0.408));
            base *= 0.9 + 0.2 * hash12(floor(px / vec2(3.0, 7.0)));
            if (e < 34.0) base = lin(vec3(0.165, 0.188, 0.251));
            if (abs(e - 40.0) < 5.0) base = lin(vec3(0.910, 0.635, 0.102));
            float em = (abs(e - 40.0) < 5.0 || abs(e - 6.0) < 3.0) ? 1.0 : 0.0;
            albedo *= base;
            emissive *= lin(vec3(1.0, 0.690, 0.125)) * em;
        } else if (pattern == P_HAZARD) {
            // hazMap / hazEm: yellow-black chevrons inside a dark frame
            vec2 px = vec2(vUv.x, 1.0 - vUv.y) * 512.0;
            float e = edgeDist(px, 512.0);
            bool stripe = e > 30.0 && mod(px.x + px.y + 512.0, 96.0) < 48.0;
            vec3 base = stripe ? lin(vec3(0.949, 0.718, 0.020)) : lin(vec3(0.090, 0.090, 0.110));
            base *= 1.0 - 0.2 * step(0.85, hash12(floor(px / vec2(6.0, 2.0))));
            if (e < 30.0) base = lin(vec3(0.165, 0.165, 0.188));
            float em = stripe ? 0.45 : (abs(e - 11.5) < 3.5 ? 1.0 : 0.0);
            albedo *= base;
            emissive *= lin(vec3(1.0, 0.690, 0.125)) * em;
        } else if (pattern == P_ASPHALT) {
            // asphMap / asphRough: speckled wet asphalt, glossy puddles tiled every 6 m
            vec2 t = vWorld.xz / 6.0, cell = floor(t), f = fract(t);
            float v = mix(0.07, 0.19, hash12(floor(vWorld.xz * 85.0)));
            vec3 base = mix(lin(vec3(0.071, 0.071, 0.098)), lin(vec3(v, v, v + 0.03)), 0.55);
            float puddle = texture(tPuddle, f).r; // was 14 ellipse tests per pixel
            albedo = mix(base, lin(vec3(0.016, 0.024, 0.078)), puddle * 0.6);
            rough = mix(0.604, 0.11, puddle);
        } else if (pattern == P_WINDOWS) {
            // winMap / winEm: 1 m window grid on the tower walls, ~36% lit in five warm/cool tones
            vec3 n = abs(normalize(vNormal));
            vec2 q = vec2(n.x > 0.5 ? vObj.z : vObj.x, vObj.y);
            vec2 cell = floor(q), f = fract(q);
            bool win = n.y < 0.5 && f.x > 0.156 && f.x < 0.844 && f.y > 0.19 && f.y < 0.81;
            float h = hash12(cell + vEmissive.w);
            vec3 base = lin(vec3(0.047, 0.055, 0.102));
            float em = 0.0;
            vec3 litCol = vec3(1.0);
            if (win) {
                base = lin(vec3(0.078, 0.102, 0.180));
                if (h < 0.36) {
                    int k = int(hash12(cell + 3.1 + vEmissive.w) * 5.0);
                    litCol = k == 1 ? lin(vec3(0.561, 0.953, 1.0)) : k == 2 ? lin(vec3(1.0, 0.541, 0.902)) : k == 3 ? lin(vec3(0.914, 0.941, 1.0)) : lin(vec3(1.0, 0.812, 0.541));
                    float frac = hash12(cell + 7.3) < 0.3 ? mix(0.3, 0.7, hash12(cell + 1.1)) : 1.0;
                    float fill = (f.y - 0.19) / 0.62;
                    em = fill < frac ? mix(0.8, 1.0, hash12(cell + 5.5)) : 0.0;
                    base = mix(base, litCol, 0.8 * step(0.5, em));
                }
            }
            albedo *= base;
            emissive *= litCol * em;
        } else if (pattern == P_DISCO) {
            // ballMap / ballEm: mirror facets, ~22% glowing in disco colours, flat shaded
            vec2 g = vUv * vec2(24.0, 12.0), cell = floor(g), f = fract(g);
            float edge = step(0.094, f.x) * step(f.x, 0.906) * step(0.083, f.y) * step(f.y, 0.917);
            float v = mix(150.0, 245.0, hash12(cell)) / 255.0;
            albedo *= lin(vec3(v, v, min(1.0, v + 0.047))) * mix(0.2, 1.0, edge);
            int k = int(hash12(cell + 2.3) * 6.0);
            vec3 dc = k == 0 ? vec3(1.0, 0.169, 0.839) : k == 1 ? vec3(0.161, 0.906, 1.0) : k == 2 ? vec3(0.957, 1.0, 0.353)
                    : k == 3 ? vec3(0.541, 0.357, 1.0) : k == 4 ? vec3(1.0, 0.541, 0.102) : vec3(0.169, 1.0, 0.604);
            emissive *= lin(dc) * edge * step(hash12(cell + 8.8), 0.22);
            vec3 fn = normalize(cross(dFdx(vWorld), dFdy(vWorld)));
            N = dot(fn, V) < 0.0 ? -fn : fn;
        }

        vec3 diff = albedo * (1.0 - metal);
        vec3 f0 = mix(vec3(0.04), albedo, metal);
        float a = max(rough * rough, 0.0025);
        float nv = max(dot(N, V), 1e-4);

        vec3 c = direct(N, V, normalize(F.sunDir.xyz), F.sunColor.rgb, diff, f0, a);
        for (int i = 0; i < 3; ++i) {
            vec3 Lv = F.pointPos[i].xyz - vWorld;
            float d = length(Lv), cutoff = F.pointPos[i].w;
            if (cutoff <= 0.0) continue;
            float att = 1.0 / max(d * d, 0.01) * pow(clamp(1.0 - pow(d / cutoff, 4.0), 0.0, 1.0), 2.0);
            c += direct(N, V, Lv / d, F.pointColor[i].rgb * att, diff, f0, a);
        }
        vec3 hemi = mix(F.hemiGround.rgb, F.hemiSky.rgb, 0.5 * N.y + 0.5) * F.hemiSky.w;
        c += hemi * diff / PI;
        c += envColor(N, 1.0) * diff; // scene.environment irradiance (PMREM), full strength like three.js
        c += envColor(reflect(-V, N), rough) * envBRDF(f0, rough, nv);
        // addRim(): fresnel edge glow so silhouettes read (and bloom)
        c += vRim.rgb * pow(1.0 - clamp(dot(N, V), 0.0, 1.0), vParams.w) * vRim.w;
        color = c + emissive;
    }
    float dist = length(F.cameraPos.xyz - vWorld);
    float fog = 1.0 - exp(-F.fogColor.w * F.fogColor.w * dist * dist);
    outColor = vec4(mix(color, F.fogColor.rgb, fog), 1.0);
}
