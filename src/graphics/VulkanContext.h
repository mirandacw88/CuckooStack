// Instance, physical/logical device, graphics+present queue and the VMA allocator. Vulkan 1.0 core only.
// Portability (MoltenVK) is detected at runtime from the extensions the loader reports, not from #ifdefs,
// which keeps this file free of platform knowledge.
#pragma once

#include "VulkanCommon.h"

#include <functional>
#include <string>

namespace cs {

class PlatformSurface;

class VulkanContext {
public:
    struct Desc {
        const char* appName = "Cuckoo Stack";
        bool enableValidation = false;
    };

    // Returns false with a human-readable reason in `error` (no Vulkan loader, no compatible GPU, ...).
    bool init(PlatformSurface& platform, const Desc& desc, std::string& error);
    void shutdown();

    // Surface lifecycle: Android destroys the window on every pause.
    bool createSurface(PlatformSurface& platform);
    void destroySurface();

    // One-shot command buffer for uploads; blocks until complete.
    void immediateSubmit(const std::function<void(VkCommandBuffer)>& record);

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties{};
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    bool portability = false;

private:
    bool createInstance(PlatformSurface& platform, const Desc& desc, std::string& error);
    bool pickDevice(std::string& error);
    bool createDevice(std::string& error);

    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkCommandPool uploadPool_ = VK_NULL_HANDLE;
    VkFence uploadFence_ = VK_NULL_HANDLE;
    bool hasPortabilitySubset_ = false;
};

} // namespace cs
