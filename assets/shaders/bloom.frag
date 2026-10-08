#version 450
// Quarter-resolution bloom (stand-in for UnrealBloomPass): mode 0 = threshold prefilter, mode 1 = 9-tap gaussian.
layout(set = 0, binding = 0) uniform sampler2D tSrc;
layout(push_constant) uniform Push { vec4 rot; vec4 params; vec4 extra; } P; // params: texel.xy, dir.xy | extra: mode, threshold, knee
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;
void main() {
    vec2 texel = P.params.xy;
    if (P.extra.x < 0.5) {
        vec3 c = 0.25 * (texture(tSrc, vUv + texel * vec2(-1, -1)).rgb + texture(tSrc, vUv + texel * vec2(1, -1)).rgb +
                         texture(tSrc, vUv + texel * vec2(-1, 1)).rgb + texture(tSrc, vUv + texel * vec2(1, 1)).rgb);
        if (any(isnan(c))) c = vec3(0.0);
        c = clamp(c, 0.0, 40.0); // stop overflowing pixels before bloom spreads them
        float lum = dot(c, vec3(0.299, 0.587, 0.114));
        float soft = clamp(lum - P.extra.y + P.extra.z, 0.0, 2.0 * P.extra.z);
        soft = soft * soft / (4.0 * P.extra.z + 1e-4);
        float w = max(soft, lum - P.extra.y) / max(lum, 1e-4);
        outColor = vec4(c * w, 1.0);
    } else {
        const float wgt[5] = float[](0.2270270, 0.1945946, 0.1216216, 0.0540541, 0.0162162);
        vec2 step = texel * P.params.zw * 1.5;
        vec3 c = texture(tSrc, vUv).rgb * wgt[0];
        for (int i = 1; i < 5; ++i) c += (texture(tSrc, vUv + step * float(i)).rgb + texture(tSrc, vUv - step * float(i)).rgb) * wgt[i];
        outColor = vec4(c, 1.0);
    }
}
