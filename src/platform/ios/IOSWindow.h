// CAMetalLayer -> VkSurfaceKHR bridge (VK_EXT_metal_surface via MoltenVK, loaded at runtime). Plain C++ header so the shared
// code can hold it; the Objective-C++ implementation lives in IOSWindow.mm.
#pragma once

#include "../PlatformSurface.h"

namespace cs {

class IOSWindow final : public PlatformSurface {
public:
    // layer: CAMetalLayer* passed as an opaque pointer (bridged in IOSWindow.mm)
    explicit IOSWindow(void* metalLayer) : layer_(metalLayer) {}

    VkResult loadVulkan() override;
    std::vector<const char*> requiredInstanceExtensions() const override;
    VkResult createSurface(VkInstance instance, VkSurfaceKHR* outSurface) override;
    VkExtent2D drawableExtent() const override;
    float contentScale() const override;
    const char* name() const override { return "iOS/MoltenVK"; }

private:
    void* layer_;
};

} // namespace cs
