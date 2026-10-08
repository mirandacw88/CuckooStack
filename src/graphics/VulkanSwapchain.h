// Swapchain creation/recreation, acquire and present.
// Android pre-rotation: the swapchain is created in the display's native ("identity") orientation with
// preTransform = currentTransform, and the final pass rotates clip space. This avoids the compositor rotation
// pass that otherwise costs bandwidth on every frame (Android's recommended Vulkan setup).
#pragma once

#include "VulkanCommon.h"

#include <glm/glm.hpp>
#include <vector>

namespace cs {

class VulkanContext;

class VulkanSwapchain {
public:
    // fallbackExtent: platform drawable size, used only when the surface leaves the extent to the app.
    bool create(VulkanContext& ctx, VkExtent2D fallbackExtent);
    void destroy();

    // VK_SUCCESS / VK_SUBOPTIMAL_KHR / VK_ERROR_OUT_OF_DATE_KHR (caller recreates)
    VkResult acquire(VkSemaphore signal, uint32_t& imageIndex);
    VkResult present(VkSemaphore wait, uint32_t imageIndex);

    bool valid() const { return swapchain_ != VK_NULL_HANDLE; }
    VkFormat format() const { return format_; }
    bool isSrgb() const { return srgb_; }
    VkExtent2D extent() const { return extent_; }          // swapchain image size (native orientation)
    VkExtent2D logicalExtent() const { return logical_; }  // what the user sees (rotated when pre-transformed)
    glm::mat2 preRotation() const { return preRotation_; } // apply to clip-space xy in the present pass
    uint32_t imageCount() const { return static_cast<uint32_t>(images_.size()); }
    VkImageView view(uint32_t i) const { return views_[i]; }
    VkImage image(uint32_t i) const { return images_[i]; }
    bool canReadback() const { return readback_; }
    // one per image: a present can still be reading the previous frame's semaphore when the frame fence signals
    VkSemaphore renderFinished(uint32_t i) const { return renderFinished_[i]; }

private:
    VulkanContext* ctx_ = nullptr;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    bool srgb_ = false, readback_ = false;
    VkExtent2D extent_{}, logical_{};
    glm::mat2 preRotation_{1.f};
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    std::vector<VkSemaphore> renderFinished_;
};

} // namespace cs
