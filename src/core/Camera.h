// Perspective camera with the web build's framing rule: keep ~9.2 m of track visible across the screen
// regardless of aspect ratio. Produces Vulkan-ready matrices (z in [0,1], +Y up in world, Y flipped in clip).
#pragma once

#include "Math.h"

namespace cs {

class Camera {
public:
    float fovDeg = 50.f, nearZ = 0.1f, farZ = 900.f;

    void setViewport(float width, float height);
    void setPose(const glm::vec3& position, const glm::vec3& target);
    // Widens the projection only; the follow distance (framing rule) stays put, so it reads as speed.
    void setFovBoost(float degrees);

    float aspect() const { return aspect_; }
    // camDist from resize(): clamp(want / (2 * tan(fov/2) * aspect), 9, 18)
    float followDistance() const { return followDistance_; }
    const glm::vec3& position() const { return position_; }
    const glm::mat4& view() const { return view_; }
    const glm::mat4& proj() const { return proj_; }
    // world -> HUD pixels (top-left origin); returns false when behind the camera
    bool project(const glm::vec3& world, glm::vec2& outPx) const;

private:
    float fovBoost_ = 0.f;
    float aspect_ = 1.f, width_ = 1.f, height_ = 1.f, followDistance_ = 12.f;
    glm::vec3 position_{0.f};
    glm::mat4 view_{1.f}, proj_{1.f};
};

} // namespace cs
