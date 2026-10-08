// Abstract window/surface bridge. The renderer talks only to this interface, so src/graphics and src/core
// never include ANativeWindow, UIKit, QuartzCore or GLFW headers.
#pragma once

#include <volk.h>

#include <vector>

namespace cs {

class PlatformSurface {
public:
    virtual ~PlatformSurface() = default;

    // Load the Vulkan loader and bootstrap volk. Android/desktop dlopen the system loader (volkInitialize);
    // iOS hands volk the statically linked MoltenVK vkGetInstanceProcAddr instead.
    virtual VkResult loadVulkan() { return volkInitialize(); }

    // Instance extensions this platform's surface needs (VK_KHR_surface + the platform surface extension).
    virtual std::vector<const char*> requiredInstanceExtensions() const = 0;

    // Create the presentation surface: vkCreateAndroidSurfaceKHR / vkCreateMetalSurfaceEXT / GLFW.
    virtual VkResult createSurface(VkInstance instance, VkSurfaceKHR* outSurface) = 0;

    // Drawable size in pixels, used when the surface reports currentExtent = 0xFFFFFFFF.
    virtual VkExtent2D drawableExtent() const = 0;

    // Pixels per logical point (HUD units).
    virtual float contentScale() const { return 1.f; }

    virtual const char* name() const = 0;
};

} // namespace cs
