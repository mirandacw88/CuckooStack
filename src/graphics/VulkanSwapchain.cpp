#include "VulkanSwapchain.h"
#include "VulkanContext.h"

#include <algorithm>
#include <cstdlib>

namespace cs {

bool VulkanSwapchain::create(VulkanContext& ctx, VkExtent2D fallbackExtent) {
    ctx_ = &ctx;
    VkSwapchainKHR old = swapchain_;
    for (VkImageView v : views_) vkDestroyImageView(ctx.device, v, nullptr);
    for (VkSemaphore s : renderFinished_) vkDestroySemaphore(ctx.device, s, nullptr);
    views_.clear(); renderFinished_.clear(); images_.clear();

    VkSurfaceCapabilitiesKHR caps;
    VK_TRY(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.gpu, ctx.surface, &caps));

    uint32_t fc = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.gpu, ctx.surface, &fc, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fc);
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.gpu, ctx.surface, &fc, formats.data());
    if (formats.empty()) { CS_LOGE("Surface reports no formats"); return false; }
    // UNORM + manual sRGB encode in the composite shader matches the web build's colour pipeline exactly;
    // fall back to whatever the surface offers (and skip the manual encode for *_SRGB formats).
    VkSurfaceFormatKHR chosen = formats[0];
    for (const auto& f : formats)
        if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { chosen = f; break; }
    if (chosen.format == VK_FORMAT_UNDEFINED) chosen.format = VK_FORMAT_B8G8R8A8_UNORM;
    format_ = chosen.format;
    srgb_ = format_ == VK_FORMAT_B8G8R8A8_SRGB || format_ == VK_FORMAT_R8G8B8A8_SRGB || format_ == VK_FORMAT_A8B8G8R8_SRGB_PACK32;

    // extent and pre-rotation
    VkExtent2D current = caps.currentExtent;
    if (current.width == UINT32_MAX) {
        current.width = std::clamp(fallbackExtent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        current.height = std::clamp(fallbackExtent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    const VkSurfaceTransformFlagBitsKHR transform = caps.currentTransform;
    const bool rotated90 = transform & (VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR | VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR);
    logical_ = current;
    extent_ = rotated90 ? VkExtent2D{current.height, current.width} : current;
    float angle = 0.f;
    if (transform & VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR) angle = 90.f;
    else if (transform & VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR) angle = 180.f;
    else if (transform & VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR) angle = 270.f;
    const float rad = glm::radians(angle), c = std::cos(rad), s = std::sin(rad);
    preRotation_ = glm::mat2(c, s, -s, c);
    if (extent_.width == 0 || extent_.height == 0) { // minimised: drop the old chain, retry next frame
        if (old) vkDestroySwapchainKHR(ctx.device, old, nullptr);
        swapchain_ = VK_NULL_HANDLE;
        return false;
    }

    uint32_t imageCount = std::max(caps.minImageCount + 1, 3u); // triple buffering keeps TBDR GPUs fed
    if (caps.maxImageCount > 0) imageCount = std::min(imageCount, caps.maxImageCount);

    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (VkCompositeAlphaFlagBitsKHR a : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
                                          VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR})
        if (caps.supportedCompositeAlpha & a) { alpha = a; break; }

    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = ctx.surface;
    ci.minImageCount = imageCount;
    ci.imageFormat = chosen.format;
    ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = extent_;
    ci.imageArrayLayers = 1;
    // TRANSFER_SRC (when offered) enables Renderer::captureNextFrame for screenshots / golden-image tests
    readback_ = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (readback_ ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = transform;
    ci.compositeAlpha = alpha;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR; // always available; vsync-paced for battery
#ifndef NDEBUG
    if (std::getenv("CS_UNCAPPED")) { // profiling only: let the GPU run flat out to measure its real cost
        uint32_t pc = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(ctx.gpu, ctx.surface, &pc, nullptr);
        std::vector<VkPresentModeKHR> modes(pc);
        vkGetPhysicalDeviceSurfacePresentModesKHR(ctx.gpu, ctx.surface, &pc, modes.data());
        for (VkPresentModeKHR m : {VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR})
            if (std::find(modes.begin(), modes.end(), m) != modes.end()) { ci.presentMode = m; break; }
    }
#endif
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = old;
    VK_TRY(vkCreateSwapchainKHR(ctx.device, &ci, nullptr, &swapchain_));
    if (old) vkDestroySwapchainKHR(ctx.device, old, nullptr);

    uint32_t n = 0;
    vkGetSwapchainImagesKHR(ctx.device, swapchain_, &n, nullptr);
    images_.resize(n);
    vkGetSwapchainImagesKHR(ctx.device, swapchain_, &n, images_.data());
    for (VkImage img : images_) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = img;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format_;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView view;
        VK_TRY(vkCreateImageView(ctx.device, &vi, nullptr, &view));
        views_.push_back(view);
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore sem;
        VK_TRY(vkCreateSemaphore(ctx.device, &si, nullptr, &sem));
        renderFinished_.push_back(sem);
    }
    CS_LOGI("Swapchain %ux%u (logical %ux%u), %u images, format %d, rotation %.0f", extent_.width, extent_.height,
            logical_.width, logical_.height, n, int(format_), angle);
    return true;
}

void VulkanSwapchain::destroy() {
    if (!ctx_) return;
    for (VkImageView v : views_) vkDestroyImageView(ctx_->device, v, nullptr);
    for (VkSemaphore s : renderFinished_) vkDestroySemaphore(ctx_->device, s, nullptr);
    if (swapchain_) vkDestroySwapchainKHR(ctx_->device, swapchain_, nullptr);
    views_.clear(); renderFinished_.clear(); images_.clear();
    swapchain_ = VK_NULL_HANDLE;
}

VkResult VulkanSwapchain::acquire(VkSemaphore signal, uint32_t& imageIndex) {
    return vkAcquireNextImageKHR(ctx_->device, swapchain_, UINT64_MAX, signal, VK_NULL_HANDLE, &imageIndex);
}

VkResult VulkanSwapchain::present(VkSemaphore wait, uint32_t imageIndex) {
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &wait;
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &imageIndex;
    return vkQueuePresentKHR(ctx_->queue, &pi);
}

} // namespace cs
