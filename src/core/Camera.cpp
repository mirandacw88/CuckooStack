#include "Camera.h"

namespace cs {

void Camera::setViewport(float width, float height) {
    width_ = std::max(1.f, width);
    height_ = std::max(1.f, height);
    aspect_ = width_ / height_;
    constexpr float kTrackVisible = 9.2f; // metres of track visible across the screen
    const float t = std::tan(glm::radians(fovDeg) * 0.5f);
    followDistance_ = clampf(kTrackVisible / (2.f * t * aspect_), 9.f, 18.f);
    setFovBoost(fovBoost_);
}

void Camera::setFovBoost(float degrees) {
    fovBoost_ = degrees;
    proj_ = glm::perspective(glm::radians(fovDeg + fovBoost_), aspect_, nearZ, farZ);
    proj_[1][1] *= -1.f; // Vulkan framebuffer Y points down
}

void Camera::setPose(const glm::vec3& position, const glm::vec3& target) {
    position_ = position;
    view_ = glm::lookAt(position, target, glm::vec3(0, 1, 0));
}

bool Camera::project(const glm::vec3& world, glm::vec2& outPx) const {
    const glm::vec4 clip = proj_ * view_ * glm::vec4(world, 1.f);
    if (clip.w <= 0.f) return false;
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z > 1.f) return false;
    outPx = {(ndc.x * 0.5f + 0.5f) * width_, (ndc.y * 0.5f + 0.5f) * height_}; // Y already flipped
    return true;
}

} // namespace cs
