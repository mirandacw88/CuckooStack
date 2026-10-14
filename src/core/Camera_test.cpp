// The camera (Camera.cpp): the web build's framing rule (about 9.2 m of track across the screen whatever the aspect
// ratio), Vulkan-ready projection, the FOV boost, and world -> HUD projection used for popups and the banner.
#include "TestUtil.h"
#include "core/Camera.h"

using namespace cs;
using namespace test;

namespace {

void framing() {
    Camera c;
    float last = 0;
    bool wider = true;
    for (float w : {360.f, 393.f, 430.f, 600.f, 1024.f, 1920.f}) {
        c.setViewport(w, 860.f);
        wider = wider && (last == 0 || c.followDistance() <= last);
        last = c.followDistance();
    }
    check(wider, "a wider screen: the camera comes closer (same track width visible)");
    c.setViewport(600, 800); // a tablet: inside the clamp
    const float t = std::tan(glm::radians(c.fovDeg) * 0.5f);
    check(c.followDistance() > 9.f && c.followDistance() < 18.f && std::abs(2.f * t * c.aspect() * c.followDistance() - 9.2f) < 0.01f,
          "inside the clamp: exactly 9.2 m of track across");
    c.setViewport(430, 900);
    check(c.followDistance() == 18.f, "a portrait phone: as far back as allowed (18 m)");
    c.setViewport(100, 2000);
    check(c.followDistance() == 18.f, "a very tall screen: clamped to 18 m away");
    c.setViewport(4000, 1000);
    check(c.followDistance() == 9.f, "a very wide screen: clamped to 9 m");
    c.setViewport(0, 0);
    check(c.aspect() == 1.f && std::isfinite(c.followDistance()), "a zero-size viewport (minimised): no division by zero");
}

void projection() {
    Camera c;
    c.setViewport(400, 800);
    c.setPose({0, 2, 10}, {0, 2, 0});
    glm::vec2 px;
    check(c.project({0, 2, 0}, px) && std::abs(px.x - 200) < 0.01f && std::abs(px.y - 400) < 0.01f, "the target: centre of the screen");
    check(c.project({0, 3, 0}, px) && px.y < 400, "higher in the world: higher on screen (HUD y points down)");
    check(c.project({1, 2, 0}, px) && px.x > 200, "right in the world: right on screen");
    check(!c.project({0, 2, 20}, px), "behind the camera: not projected");
    check(!c.project({0, 2, -2000}, px), "beyond the far plane: not projected");
    const glm::vec4 clip = c.proj() * c.view() * glm::vec4(0, 2, 0, 1);
    check(clip.z / clip.w >= 0.f && clip.z / clip.w <= 1.f, "Vulkan depth range: z in [0, 1]");
    check(c.position() == glm::vec3(0, 2, 10), "position as posed");
}

void fovBoost() {
    Camera c;
    c.setViewport(400, 800);
    c.setPose({0, 2, 10}, {0, 2, 0});
    const float dist = c.followDistance();
    glm::vec2 a, b;
    c.project({1, 2, 0}, a);
    c.setFovBoost(10.f);
    c.project({1, 2, 0}, b);
    check(std::abs(b.x - 200) < std::abs(a.x - 200), "a FOV boost widens the view (things move toward the centre)");
    check(c.followDistance() == dist, "...without moving the camera (it reads as speed)");
    c.setViewport(800, 800);
    c.project({1, 2, 0}, a);
    c.setFovBoost(10.f);
    c.project({1, 2, 0}, b);
    check(a == b, "the boost is kept across a resize");
}

} // namespace

int main() {
    framing();
    projection();
    fovBoost();
    return finish("Camera");
}
