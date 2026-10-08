// VK_USE_PLATFORM_METAL_EXT is defined only here, keeping QuartzCore types out of the shared code.
#define VK_USE_PLATFORM_METAL_EXT 1
#include "IOSWindow.h"

#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>

#include <dlfcn.h>

namespace cs {

// MoltenVK ships as the *dynamic* MoltenVK.xcframework, embedded in the app's Frameworks folder (allowed
// on the App Store). A static MoltenVK would export vkGetInstanceProcAddr and collide with volk's global of
// the same name. volk's own fallback dlopen("MoltenVK.framework/MoltenVK") is relative to the working
// directory, so the framework is opened from the bundle path and volk is bootstrapped from it.
VkResult IOSWindow::loadVulkan() {
    NSString* path = [NSBundle.mainBundle.privateFrameworksPath stringByAppendingPathComponent:@"MoltenVK.framework/MoltenVK"];
    void* lib = dlopen(path.fileSystemRepresentation, RTLD_NOW | RTLD_LOCAL);
    if (!lib) return volkInitialize(); // e.g. a loader-based setup
    auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(lib, "vkGetInstanceProcAddr"));
    if (!gipa) return VK_ERROR_INITIALIZATION_FAILED;
    volkInitializeCustom(gipa);
    return VK_SUCCESS;
}

std::vector<const char*> IOSWindow::requiredInstanceExtensions() const {
    // VK_KHR_portability_enumeration + VK_KHR_get_physical_device_properties2 are added by VulkanContext
    // whenever the implementation advertises them, which MoltenVK always does.
    return {VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_METAL_SURFACE_EXTENSION_NAME};
}

VkResult IOSWindow::createSurface(VkInstance instance, VkSurfaceKHR* outSurface) {
    auto create = reinterpret_cast<PFN_vkCreateMetalSurfaceEXT>(vkGetInstanceProcAddr(instance, "vkCreateMetalSurfaceEXT"));
    if (!create || !layer_) return VK_ERROR_EXTENSION_NOT_PRESENT;
    VkMetalSurfaceCreateInfoEXT ci{VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT};
    ci.pLayer = (__bridge CAMetalLayer*)layer_;
    return create(instance, &ci, nullptr, outSurface);
}

VkExtent2D IOSWindow::drawableExtent() const {
    CAMetalLayer* layer = (__bridge CAMetalLayer*)layer_;
    return {static_cast<uint32_t>(layer.drawableSize.width), static_cast<uint32_t>(layer.drawableSize.height)};
}

float IOSWindow::contentScale() const {
    return static_cast<float>(((__bridge CAMetalLayer*)layer_).contentsScale);
}

} // namespace cs
