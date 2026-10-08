#include "VulkanContext.h"
#include "../platform/PlatformSurface.h"

#include <cstring>
#include <vector>

// Provisional in Vulkan-Headers; spelled out so the build never depends on VK_ENABLE_BETA_EXTENSIONS.
#ifndef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
#define VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME "VK_KHR_portability_subset"
#endif

namespace cs {

const char* vkResultName(VkResult r) {
    switch (r) {
#define CASE(x) case x: return #x;
        CASE(VK_SUCCESS) CASE(VK_NOT_READY) CASE(VK_TIMEOUT) CASE(VK_INCOMPLETE) CASE(VK_SUBOPTIMAL_KHR)
        CASE(VK_ERROR_OUT_OF_HOST_MEMORY) CASE(VK_ERROR_OUT_OF_DEVICE_MEMORY) CASE(VK_ERROR_INITIALIZATION_FAILED)
        CASE(VK_ERROR_DEVICE_LOST) CASE(VK_ERROR_MEMORY_MAP_FAILED) CASE(VK_ERROR_LAYER_NOT_PRESENT)
        CASE(VK_ERROR_EXTENSION_NOT_PRESENT) CASE(VK_ERROR_FEATURE_NOT_PRESENT) CASE(VK_ERROR_INCOMPATIBLE_DRIVER)
        CASE(VK_ERROR_TOO_MANY_OBJECTS) CASE(VK_ERROR_FORMAT_NOT_SUPPORTED) CASE(VK_ERROR_SURFACE_LOST_KHR)
        CASE(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR) CASE(VK_ERROR_OUT_OF_DATE_KHR)
#undef CASE
    default: return "VK_ERROR_<unknown>";
    }
}

namespace {

bool hasExtension(const std::vector<VkExtensionProperties>& list, const char* name) {
    for (const auto& e : list) if (std::strcmp(e.extensionName, name) == 0) return true;
    return false;
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    const LogLevel level = severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? LogLevel::Error : LogLevel::Warn;
    logf(level, "[validation] %s", data->pMessage);
    return VK_FALSE;
}

} // namespace

bool VulkanContext::init(PlatformSurface& platform, const Desc& desc, std::string& error) {
    // Never touch a Vulkan entry point before the loader is confirmed: devices without libvulkan.so
    // (or with a broken driver) must end in a diagnostic exit, not a SIGSEGV on a null function pointer.
    const VkResult loaded = platform.loadVulkan();
    if (loaded != VK_SUCCESS) {
        error = std::string("Vulkan loader unavailable on this device (") + vkResultName(loaded) + ")";
        return false;
    }
    if (!createInstance(platform, desc, error)) return false;
    if (!createSurface(platform)) { error = "Could not create a presentation surface"; return false; }
    if (!pickDevice(error)) return false;
    if (!createDevice(error)) return false;

    VmaVulkanFunctions fns{};
    fns.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    fns.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo ai{};
    ai.vulkanApiVersion = VK_API_VERSION_1_0;
    ai.physicalDevice = gpu;
    ai.device = device;
    ai.instance = instance;
    ai.pVulkanFunctions = &fns;
    if (vmaCreateAllocator(&ai, &allocator) != VK_SUCCESS) { error = "vmaCreateAllocator failed"; return false; }

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = queueFamily;
    VK_TRY(vkCreateCommandPool(device, &pci, nullptr, &uploadPool_));
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_TRY(vkCreateFence(device, &fci, nullptr, &uploadFence_));

    CS_LOGI("Vulkan %u.%u.%u on %s (%s), portability=%d, depth format %d", VK_VERSION_MAJOR(properties.apiVersion),
            VK_VERSION_MINOR(properties.apiVersion), VK_VERSION_PATCH(properties.apiVersion), properties.deviceName,
            platform.name(), portability ? 1 : 0, int(depthFormat));
    return true;
}

bool VulkanContext::createInstance(PlatformSurface& platform, const Desc& desc, std::string& error) {
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data());

    std::vector<const char*> exts = platform.requiredInstanceExtensions();
    for (const char* e : exts)
        if (!hasExtension(available, e)) { error = std::string("Missing instance extension ") + e; return false; }

    // MoltenVK (and any other non-conformant implementation) is only enumerated when the app opts in.
    VkInstanceCreateFlags flags = 0;
    if (hasExtension(available, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        exts.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        portability = true;
    }
    // Required by VK_KHR_portability_subset on a Vulkan 1.0 instance.
    if (hasExtension(available, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
        exts.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

    std::vector<const char*> layers;
    bool debugUtils = false;
    if (desc.enableValidation) {
        uint32_t lc = 0;
        vkEnumerateInstanceLayerProperties(&lc, nullptr);
        std::vector<VkLayerProperties> props(lc);
        vkEnumerateInstanceLayerProperties(&lc, props.data());
        for (const auto& l : props)
            if (std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0) layers.push_back("VK_LAYER_KHRONOS_validation");
        if (hasExtension(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) { exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME); debugUtils = true; }
        if (layers.empty()) CS_LOGW("Validation requested but VK_LAYER_KHRONOS_validation is not installed");
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = desc.appName;
    app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app.pEngineName = "CuckooEngine";
    app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app.apiVersion = VK_API_VERSION_1_0; // baseline: every Vulkan-capable Android device and MoltenVK

    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.flags = flags;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
    ci.ppEnabledExtensionNames = exts.data();
    ci.enabledLayerCount = static_cast<uint32_t>(layers.size());
    ci.ppEnabledLayerNames = layers.data();
    const VkResult r = vkCreateInstance(&ci, nullptr, &instance);
    if (r != VK_SUCCESS) { error = std::string("vkCreateInstance failed: ") + vkResultName(r); return false; }
    volkLoadInstanceOnly(instance);

    if (debugUtils && vkCreateDebugUtilsMessengerEXT) {
        VkDebugUtilsMessengerCreateInfoEXT mi{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        mi.pfnUserCallback = debugCallback;
        vkCreateDebugUtilsMessengerEXT(instance, &mi, nullptr, &messenger_);
    }
    return true;
}

bool VulkanContext::createSurface(PlatformSurface& platform) {
    if (surface) return true;
    const VkResult r = platform.createSurface(instance, &surface);
    if (r != VK_SUCCESS) { CS_LOGE("createSurface failed: %s", vkResultName(r)); surface = VK_NULL_HANDLE; return false; }
    if (gpu) { // re-validate presentation support on a new window
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(gpu, queueFamily, surface, &present);
        if (!present) { CS_LOGE("Queue family %u cannot present to the new surface", queueFamily); return false; }
    }
    return true;
}

void VulkanContext::destroySurface() {
    if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
    surface = VK_NULL_HANDLE;
}

bool VulkanContext::pickDevice(std::string& error) {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (count == 0) { error = "No Vulkan physical device found"; return false; }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());

    int bestScore = -1;
    std::string rejected;
    for (VkPhysicalDevice d : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(d, &props);
        uint32_t ec = 0;
        vkEnumerateDeviceExtensionProperties(d, nullptr, &ec, nullptr);
        std::vector<VkExtensionProperties> exts(ec);
        vkEnumerateDeviceExtensionProperties(d, nullptr, &ec, exts.data());
        if (!hasExtension(exts, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) { rejected += std::string(props.deviceName) + ": no swapchain; "; continue; }

        uint32_t qc = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qc, nullptr);
        std::vector<VkQueueFamilyProperties> qprops(qc);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qc, qprops.data());
        int family = -1;
        for (uint32_t i = 0; i < qc; ++i) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface, &present);
            if ((qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) { family = static_cast<int>(i); break; }
        }
        if (family < 0) { rejected += std::string(props.deviceName) + ": no graphics+present queue; "; continue; }

        const int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3
                        : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
        if (score > bestScore) {
            bestScore = score; gpu = d; queueFamily = static_cast<uint32_t>(family); properties = props;
            hasPortabilitySubset_ = hasExtension(exts, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
        }
    }
    if (!gpu) { error = "No compatible Vulkan GPU (" + rejected + ")"; return false; }

    // D16 is the only depth format the spec guarantees; prefer higher precision for the 0.1..900 m range.
    for (VkFormat f : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM}) {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(gpu, f, &fp);
        if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) { depthFormat = f; break; }
    }
    if (depthFormat == VK_FORMAT_UNDEFINED) { error = "No depth attachment format"; return false; }
    return true;
}

bool VulkanContext::createDevice(std::string& error) {
    const float priority = 1.f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;

    std::vector<const char*> exts = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    // The spec requires enabling portability_subset whenever the device exposes it (MoltenVK).
    if (hasPortabilitySubset_) exts.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);

    VkPhysicalDeviceFeatures features{}; // nothing optional: the pipeline is pure Vulkan 1.0 core
    VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qci;
    ci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
    ci.ppEnabledExtensionNames = exts.data();
    ci.pEnabledFeatures = &features;
    const VkResult r = vkCreateDevice(gpu, &ci, nullptr, &device);
    if (r != VK_SUCCESS) { error = std::string("vkCreateDevice failed: ") + vkResultName(r); return false; }
    volkLoadDevice(device); // direct device dispatch, skipping the loader trampoline
    vkGetDeviceQueue(device, queueFamily, 0, &queue);
    return true;
}

void VulkanContext::immediateSubmit(const std::function<void(VkCommandBuffer)>& record) {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = uploadPool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &ai, &cmd);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    record(cmd);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &si, uploadFence_);
    vkWaitForFences(device, 1, &uploadFence_, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &uploadFence_);
    vkFreeCommandBuffers(device, uploadPool_, 1, &cmd);
}

void VulkanContext::shutdown() {
    if (device) {
        vkDeviceWaitIdle(device);
        if (uploadFence_) vkDestroyFence(device, uploadFence_, nullptr);
        if (uploadPool_) vkDestroyCommandPool(device, uploadPool_, nullptr);
        if (allocator) vmaDestroyAllocator(allocator);
        vkDestroyDevice(device, nullptr);
    }
    destroySurface();
    if (messenger_) vkDestroyDebugUtilsMessengerEXT(instance, messenger_, nullptr);
    if (instance) vkDestroyInstance(instance, nullptr);
    *this = VulkanContext{};
}

} // namespace cs
