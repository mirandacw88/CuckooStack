// VK_USE_PLATFORM_ANDROID_KHR is defined only in this translation unit: platform types never leak into
// src/graphics or src/core.
#define VK_USE_PLATFORM_ANDROID_KHR 1
#include "AndroidWindow.h"

#include <android/native_window.h>

namespace cs {

std::vector<const char*> AndroidWindow::requiredInstanceExtensions() const {
    return {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
}

VkResult AndroidWindow::createSurface(VkInstance instance, VkSurfaceKHR* outSurface) {
    if (!window_) return VK_ERROR_INITIALIZATION_FAILED;
    auto create = reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(vkGetInstanceProcAddr(instance, "vkCreateAndroidSurfaceKHR"));
    if (!create) return VK_ERROR_EXTENSION_NOT_PRESENT;
    VkAndroidSurfaceCreateInfoKHR ci{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
    ci.window = window_;
    return create(instance, &ci, nullptr, outSurface);
}

VkExtent2D AndroidWindow::drawableExtent() const {
    if (!window_) return {0, 0};
    return {static_cast<uint32_t>(ANativeWindow_getWidth(window_)), static_cast<uint32_t>(ANativeWindow_getHeight(window_))};
}

} // namespace cs
