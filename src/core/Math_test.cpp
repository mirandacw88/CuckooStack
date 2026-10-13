// The maths helpers (Math.h): the small JS helpers ported from cuckoo-stack.html must match the web build exactly
// (the daily course depends on seedFor + mulberry32 bit for bit), plus colours and the THREE.js compose order.
// Reference values were produced by running the web build's own functions in Node.
#include "TestUtil.h"
#include "core/Math.h"

#include <cmath>

using namespace cs;
using namespace test;

namespace {

bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) <= eps; }
bool near3(glm::vec3 a, glm::vec3 b, float eps = 1e-5f) { return glm::length(a - b) <= eps; }

void basics() {
    check(lerp(2, 6, 0.25f) == 3 && lerp(2, 6, 0) == 2 && lerp(2, 6, 1) == 6, "lerp: ends and a quarter of the way");
    check(clampf(-1, 0, 1) == 0 && clampf(2, 0, 1) == 1 && clampf(0.4f, 0, 1) == 0.4f, "clampf: below, above, inside");
    check(smoothstep01(-1) == 0 && smoothstep01(2) == 1 && smoothstep01(0.5f) == 0.5f, "smoothstep01: clamped, symmetric at 0.5");
    check(smoothstep01(0.25f) < 0.25f && smoothstep01(0.75f) > 0.75f, "smoothstep01: eases in and out");
    // JS Math.round: halves go toward +infinity (2.5 -> 3, -2.5 -> -2, -0.5 -> -0)
    check(jsRound(2.5) == 3 && jsRound(-2.5) == -2 && jsRound(-0.5) == 0 && jsRound(1.49999) == 1, "jsRound: Math.round's halves");
}

void webParity() {
    check(seedFor("cluckstack:2026-10-08") == 1564426853u, "seedFor: the web build's daily seed");
    check(seedFor("") == 167010153u && seedFor("a") == 519299066u, "seedFor: empty and one-character strings");
    Mulberry32 r(seedFor("cluckstack:2026-10-08"));
    const double a = r.next(), b = r.next(), c = r.next();
    check(a == 0.11928107147105038 && b == 0.13211465650238097 && c == 0.6312419734895229, "mulberry32: bit-exact with the JS stream");
    Mulberry32 z(0);
    check(z.next() == 0.26642920868471265 && z.next() == 0.0003297457005828619, "mulberry32: seed 0");
    check(near(hash(0), 0.7068252226308687) && near(hash(1.5), 0.2645052954685525) && near(hash(-3.25), 0.990160729350464) &&
              near(hash(1000), 0.9294653253145952),
          "hash: the web build's sin hash");
    check(imul(0xFFFFFFFFu, 2u) == 0xFFFFFFFEu && imul(65536u, 65536u) == 0u, "imul: 32-bit wrapping multiply");
}

void randoms() {
    FastRandom a(42), b(42), c(7);
    bool same = true, inRange = true, differs = false;
    for (int i = 0; i < 1000; ++i) {
        const float x = a.next01(), y = b.next01(), z = c.next01();
        same = same && x == y;
        differs = differs || x != z;
        inRange = inRange && x >= 0.f && x < 1.f;
    }
    check(same && differs, "FastRandom: same seed same stream, other seeds differ");
    check(inRange, "FastRandom: next01 in [0, 1)");
    FastRandom d(3);
    bool idx = true, rng = true;
    for (int i = 0; i < 1000; ++i) {
        const int k = d.index(5);
        idx = idx && k >= 0 && k < 5;
        const float v = d.range(-2.f, 3.f);
        rng = rng && v >= -2.f && v <= 3.f;
    }
    check(idx && rng, "FastRandom: index and range stay inside their bounds");
    FastRandom zero(0);
    check(zero.next01() != zero.next01(), "FastRandom: a zero seed still produces a stream");
}

void colours() {
    check(near3(hexColor("#ffffff"), glm::vec3(1.f)) && near3(hexColor("#000000"), glm::vec3(0.f)), "hexColor: white and black");
    check(near(srgbToLinear(0.5f), 0.214041, 1e-5) && near(srgbToLinear(0.02f), 0.02 / 12.92, 1e-7), "srgbToLinear: the curve and its linear toe");
    check(near3(hexColor("#29e7ff"), {srgbToLinear(0x29 / 255.f), srgbToLinear(0xe7 / 255.f), 1.f}), "hexColor: linearised (THREE.Color)");
    check(near3(hexColor("29E7FF"), hexColor("#29e7ff")), "hexColor: without '#', upper case");
    check(near3(srgbColor("#29e7ff"), {0x29 / 255.f, 0xe7 / 255.f, 1.f}), "srgbColor: as authored (no linearising)");
    check(near3(glow("#ff0000", 3.f), {3.f, 0.f, 0.f}), "glow: a colour times its intensity");
    check(near3(hueColor(0.f), {1, 0, 0}) && near3(hueColor(1.f / 3.f), {0, 1, 0}) && near3(hueColor(2.f / 3.f), {0, 0, 1}),
          "hueColor: red, green, blue at 0, 1/3, 2/3");
    check(near3(hueColor(1.25f), hueColor(0.25f)) && near3(hueColor(-0.75f), hueColor(0.25f)), "hueColor: wraps every turn, negatives too");
}

void composeOrder() {
    const glm::vec3 p{1, 2, 3}, s{2, 2, 2};
    const glm::mat4 m = compose(p, glm::vec3(0, kPi / 2, 0), s);
    const glm::vec3 x = glm::vec3(m * glm::vec4(1, 0, 0, 1));
    check(near3(x, {1, 2, 1}), "compose: scale, then rotate (90 deg about Y: +x -> -z), then translate");
    const glm::mat4 xyz = compose({}, glm::vec3(kPi / 2, kPi / 2, 0), glm::vec3(1));
    // THREE's Euler XYZ is R = Rx * Ry * Rz: a point is turned about Z first, then Y, then X
    const glm::vec3 v = glm::vec3(xyz * glm::vec4(0, 0, 1, 1));
    check(near3(v, {1, 0, 0}), "compose: Euler XYZ order (Rx * Ry * Rz)");
    check(compose(p, glm::vec3(0.f), s) == compose(p, s), "compose: the rotation-free overload matches");
}

} // namespace

int main() {
    basics();
    webParity();
    randoms();
    colours();
    composeOrder();
    return finish("Math");
}
