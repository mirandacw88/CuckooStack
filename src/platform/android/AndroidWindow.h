// ANativeWindow -> VkSurfaceKHR bridge (VK_KHR_android_surface).
#pragma once

#include "../PlatformSurface.h"

struct ANativeWindow;

namespace cs {

class AndroidWindow final : public PlatformSurface {
public:
    void setWindow(ANativeWindow* window) { window_ = window; }
    ANativeWindow* window() const { return window_; }
    void setDensity(float pixelsPerPoint) { density_ = pixelsPerPoint; }

    std::vector<const char*> requiredInstanceExtensions() const override;
    VkResult createSurface(VkInstance instance, VkSurfaceKHR* outSurface) override;
    VkExtent2D drawableExtent() const override;
    float contentScale() const override { return density_; }
    const char* name() const override { return "Android"; }

private:
    ANativeWindow* window_ = nullptr;
    float density_ = 1.f;
};

} // namespace cs
