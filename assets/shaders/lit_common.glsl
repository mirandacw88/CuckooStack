// The lit material shader, shared by lit.frag (every solid object) and lit_reflect.frag (the wet street's
// reflection: LIT_REFLECT).
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
layout(location = 8) in vec3 vLocal;
layout(location = 0) out vec4 outColor;
#ifdef LIT_REFLECT
layout(push_constant) uniform Push { mat4 viewProj; } P; // the reflection puts its depth on the street (below)
#endif
layout(set = 0, binding = 1) uniform sampler2D tPuddle; // baked once at startup (BakedPatterns.cpp), tiles every 6 m
layout(set = 0, binding = 2) uniform sampler2D tHero;   // the textured hen model's albedo (HenModel)
layout(set = 0, binding = 3) uniform sampler2D tHeroN;  // its tangent-space normal map (sculpted detail)
layout(set = 0, binding = 4) uniform sampler2D tCyber;     // the cyber cube face, baked (BakedPatterns.cpp): surface
layout(set = 0, binding = 5) uniform sampler2D tCyberGlow; // ... and glow channels (tube, circuit, core ring, dot)
layout(set = 0, binding = 6) uniform sampler2D tDroid;     // the title droid's albedo (DroidModel)
layout(set = 0, binding = 7) uniform sampler2D tDroidN;    // its tangent-space normal map

const float PI = 3.14159265;
const int P_CONTAINER = 1, P_HAZARD = 2, P_ASPHALT = 3, P_WINDOWS = 4, P_DISCO = 5, P_HERO = 6, P_DROID = 7, P_STONE = 8, P_TRON_FLOOR = 9;

vec3 lin(vec3 srgb) { return pow(srgb, vec3(2.2)); }
// Dave Hoskins' "hash without sine": plain ALU, no sin (which is slow on mobile GPUs)
float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

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
#ifndef LIT_REFLECT
        // two-sided pass (glass shards): light the inside of the shell too. (The reflection is mirrored, so its front
        // faces wind the other way; its normals are already right.)
        if (!gl_FrontFacing) N = -N;
#endif
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
        } else if (pattern == P_DROID) {
            // the title droid: the same normal-mapped albedo, from its own textures; its bright, saturated neon trims
            // (pink, teal) glow rather than its warm tones
            vec3 dp1 = dFdx(vWorld), dp2 = dFdy(vWorld);
            vec2 duv1 = dFdx(vUv), duv2 = dFdy(vUv);
            vec3 dp2perp = cross(dp2, N), dp1perp = cross(N, dp1);
            vec3 Tg = dp2perp * duv1.x + dp1perp * duv2.x, Bg = dp2perp * duv1.y + dp1perp * duv2.y;
            float invmax = inversesqrt(max(max(dot(Tg, Tg), dot(Bg, Bg)), 1e-20));
            vec3 nt = texture(tDroidN, vUv).xyz * 2.0 - 1.0;
            N = normalize(mat3(Tg * invmax, Bg * invmax, N) * nt);
            vec3 sx = texture(tDroid, vUv).rgb;
            vec3 tx = lin(sx);
            albedo *= tx;
            float hi = max(sx.r, max(sx.g, sx.b)), lo = min(sx.r, min(sx.g, sx.b));
            float neon = smoothstep(0.45, 0.8, hi) * smoothstep(0.25, 0.55, hi - lo);
            emissive = tx * neon * vEmissive.rgb;
        } else if (pattern == P_CONTAINER) {
            // contMap / contEm: corrugated steel, dark frame, neon trim: cyan or magenta per crate (the cyber cubes'
            // pick, seed vEmissive.w), the outer edge line in the other colour
            vec2 px = vUv * 512.0;
            bool cyanTrim = fract(vEmissive.w * 0.6180339) < 0.5;
            vec3 trimA = cyanTrim ? lin(vec3(0.161, 0.906, 1.0)) : lin(vec3(1.0, 0.169, 0.839));
            vec3 trimB = cyanTrim ? lin(vec3(1.0, 0.169, 0.839)) : lin(vec3(0.161, 0.906, 1.0));
            float e = edgeDist(px, 512.0);
            vec3 base = mod(px.x, 18.0) < 9.0 ? (mod(floor(px.x / 18.0), 2.0) > 0.5 ? lin(vec3(0.337, 0.380, 0.478)) : lin(vec3(0.235, 0.271, 0.345)))
                                               : lin(vec3(0.290, 0.329, 0.408));
            base *= 0.9 + 0.2 * hash12(floor(px / vec2(3.0, 7.0)));
            if (e < 34.0) base = lin(vec3(0.165, 0.188, 0.251));
            bool inner = abs(e - 40.0) < 5.0, outer = abs(e - 6.0) < 3.0;
            if (inner) base = mix(trimA, vec3(1.0), 0.35);
            albedo *= base;
            emissive *= inner ? trimA : outer ? trimB * 0.8 : vec3(0.0);
        } else if (pattern == P_HAZARD) {
            // the cyber cube: gunmetal armour frame with bolts, a neon tube around the face with a spark running
            // round it, a dark glass panel with circuit traces that shimmer, a core ring pulsing on the beat and a
            // strip of amber hazard tape. The static design is baked (BakedPatterns.cpp, 4 circuit layouts); only the
            // animation is computed here. Each block (seed: vEmissive.w) picks its layout, colours and timing.
            vec2 uv = vec2(vUv.x, 1.0 - vUv.y);
            float seed = fract(vEmissive.w * 0.6180339);
            float variant = min(floor(seed * 4.0), 3.0);
            vec2 auv = (uv * (254.0 / 256.0) + 1.0 / 256.0 + vec2(mod(variant, 2.0), floor(variant * 0.5))) * 0.5;
            vec4 surf = texture(tCyber, auv), glw = texture(tCyberGlow, auv);
            bool lite = F.misc.y > 0.5; // low-end device: the design without its animation
            float t = lite ? 0.0 : F.cameraPos.w, beat = lite ? 0.0 : F.misc.x;
            bool cyanFirst = seed < 0.5;
            vec3 neonA = cyanFirst ? lin(vec3(0.161, 0.906, 1.0)) : lin(vec3(1.0, 0.169, 0.839));
            vec3 neonB = cyanFirst ? lin(vec3(1.0, 0.169, 0.839)) : lin(vec3(0.161, 0.906, 1.0));
            vec3 base = surf.rgb * surf.rgb; // stored as sqrt(linear)
            float kind = surf.a * 3.0;       // 0 frame, 1 bolt, 2 glass panel, 3 hazard tape
            metal = kind < 1.5 ? 0.85 : kind < 2.5 ? 0.25 : 0.1;
            rough = kind < 0.5 ? 0.32 : kind < 1.5 ? 0.2 : kind < 2.5 ? 0.12 : 0.45;
            float em = 0.0;
            vec3 emc = neonA;
            if (glw.g > 0.004 && lite) em = glw.g * 0.6; // (lite: a steady glow)
            else if (glw.g > 0.004) { // circuit traces: a wave of light sweeps across
                float g = (uv.x + uv.y) * (512.0 / 48.0) - (128.0 / 48.0);
                em = glw.g * 0.9 * (0.35 + 0.65 * smoothstep(0.6, 1.0, sin(g * 0.9 - t * 3.0 + seed * 40.0) * 0.5 + 0.5));
            }
            float core = max(glw.b * (0.7 + 0.6 * beat), glw.a * (0.35 + 1.2 * beat)); // the core flashes on the beat
            if (core > em) { em = core; emc = neonB; }
            if (kind > 2.5) { em = 0.35 * smoothstep(0.25, 0.5, surf.r); emc = lin(vec3(1.0, 0.62, 0.1)); } // amber tape
            if (glw.r > 0.004 && lite) { em = max(em, glw.r * 0.9); emc = glw.r * 0.9 >= em ? neonA : emc; base = mix(base, lin(vec3(0.9)), glw.r * 0.3); }
            else if (glw.r > 0.004) { // the neon tube, with a spark of light running round it (pseudo-angle: no atan)
                vec2 c = uv * 512.0 - 256.0;
                float d = abs(c.x) + abs(c.y) + 1e-3;
                float ang = (c.y >= 0.0 ? 1.0 - c.x / d : 3.0 + c.x / d) * (PI * 0.5);
                float spark = smoothstep(0.86, 1.0, 0.5 + 0.5 * cos(ang - t * 2.2 - seed * 6.283));
                float tubeEm = glw.r * (0.9 + 2.5 * spark + 0.4 * beat);
                if (tubeEm > em) { em = tubeEm; emc = neonA; }
                base = mix(base, lin(vec3(0.9)), glw.r * 0.3);
            }
            albedo *= base;
            emissive *= emc * em;
        } else if (pattern == P_STONE) {
            // Neon Egypt stone (pyramids, obelisks, pylons, the course's blocks): dark glossy stone cut by glowing
            // Tron edge lines (between the side faces, and round the base and top), vColor.a horizontal stone courses
            // (1: none, plain), and on the blocks (0) a band of glyph marks that light up in a sweep; all pulse on the beat.
            // Lines are measured in the mesh's own unit space and kept ~1.5 px wide on screen (fwidth), at any size or distance.
            bool lite = F.misc.y > 0.5;
            float t = lite ? 0.0 : F.cameraPos.w, beat = lite ? 0.0 : F.misc.x;
            vec3 o = vLocal; // (unit space: -0.5 .. 0.5 on every mesh it's used on)
            float ax = abs(o.x), az = abs(o.z);
            bool cap = abs(N.y) > 0.95; // (they only turn about y)
            // (caps carry no lines: the blocks' tops are under the floor tiles, the monuments' are tiny or unseen)
            float d = cap ? 1.0 : min(max(ax, az) - min(ax, az), 0.5 - abs(o.y));
            float w = max(fwidth(d), 1e-5);
            float edge = 1.0 - smoothstep(w * 0.8, w * 2.2, d);
            float halo = exp(-d / (w * 7.0)) * 0.3;
            float courses = vColor.a, groove = 0.0, glyph = 0.0;
            if (courses > 1.5) { // stone courses: thin glowing seams
                float fc = fract((o.y + 0.5) * courses), dc = min(fc, 1.0 - fc) / courses;
                float wc = max(fwidth(dc), 1e-5);
                groove = 1.0 - smoothstep(wc * 0.6, wc * 1.8, dc);
            } else if (!cap && courses < 0.5) { // the blocks (0 courses; 1 = plain stone): a band of glyph marks across the face's middle
                float u = (ax < az ? o.x : o.z) + 0.5, v = o.y + 0.5;
                if (v > 0.36 && v < 0.64) {
                    vec2 g = vec2(u * 5.0, (v - 0.36) / 0.28 * 2.0), cell = floor(g), f = fract(g);
                    float h = hash12(cell + vEmissive.w * 7.13);
                    bool mark = h > 0.35 && f.x > 0.22 && f.x < 0.78 && f.y > 0.18 && f.y < 0.82 &&
                                (h < 0.6 ? (f.x < 0.42 || f.y > 0.62) : h < 0.8 ? abs(f.x - 0.5) < 0.12 || abs(f.y - 0.5) < 0.12 : length(f - 0.5) > 0.16);
                    float sweep = 0.5 + 0.5 * sin(t * 2.4 - u * 6.0 - vEmissive.w * 3.0);
                    glyph = mark ? 0.55 + 0.45 * sweep : 0.0;
                }
            }
            float pulse = 0.85 + 0.4 * beat;
            albedo *= (1.0 - 0.45 * groove) * (1.0 - 0.3 * edge);
            emissive = vEmissive.rgb * (edge * 1.6 + halo + groove * 0.35 + glyph * 0.9) * pulse;
        } else if (pattern == P_TRON_FLOOR) {
            // the zones' street: glossy obsidian under a glowing grid, a wave of light running along it, glossier
            // patches where the puddle mask lies (the reflections stay). Lines every 1.5 m in vRim.rgb (its rim
            // strength is 0), every 6 m in vEmissive.rgb (Egypt: cyan / gold; Japan: pink / vermilion)
            bool lite = F.misc.y > 0.5;
            float t = lite ? 0.0 : F.cameraPos.w, beat = lite ? 0.0 : F.misc.x;
            vec2 p = vWorld.xz;
            vec2 fm = fract(p / 1.5), fM = fract(p / 6.0);
            float dm = min(min(fm.x, 1.0 - fm.x), min(fm.y, 1.0 - fm.y)) * 1.5;
            float dM = min(min(fM.x, 1.0 - fM.x), min(fM.y, 1.0 - fM.y)) * 6.0;
            float wm = max(fwidth(dm), 1e-5), wM = max(fwidth(dM), 1e-5);
            float minor = 1.0 - smoothstep(wm * 0.7, wm * 2.0, dm), major = 1.0 - smoothstep(wM * 0.8, wM * 2.6, dM);
            float wave = smoothstep(0.82, 1.0, 0.5 + 0.5 * sin((p.x - t * 9.0) * 0.21));
            float puddle = texture(tPuddle, fract(p / 6.0)).r;
            albedo *= lin(vec3(0.03, 0.035, 0.06)) * (0.9 + 0.2 * hash12(floor(p * 40.0)));
            rough = mix(0.32, 0.1, puddle);
            metal = 0.6;
            emissive = vRim.rgb * minor * (0.45 + 0.9 * wave) + vEmissive.rgb * major * (1.0 + 0.5 * wave + 0.4 * beat);
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
    float alpha = 1.0;
#ifdef LIT_REFLECT
    { // the wet street's reflection (lit_reflect.frag; only this variant writes depth, so normal draws keep early-z)
        if (vWorld.y > 0.01) discard;
        // depth of the street surface this pixel shows (where the eye ray crosses y = 0), a hair nearer, so the
        // street passes the test and anything standing in front of it doesn't
        vec3 cam = F.cameraPos.xyz;
        vec3 onStreet = cam + (vWorld - cam) * (cam.y / max(cam.y - vWorld.y, 1e-4));
        vec4 clip = P.viewProj * vec4(onStreet, 1.0);
        gl_FragDepth = clamp(clip.z / clip.w - 2e-5, 0.0, 1.0);
        // a faint sheen, 5% at most: strongest where it meets the street (in the puddles), fading with depth below it
        float puddle = texture(tPuddle, fract(vWorld.xz / 6.0)).r;
        alpha = 0.05 * exp(vWorld.y * 0.6) * mix(0.6, 1.0, puddle);
    }
#endif
    outColor = vec4(mix(color, F.fogColor.rgb, fog), alpha);
}
