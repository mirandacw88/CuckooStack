// VMA-backed 2D render targets. Transient attachments (depth) request lazily allocated memory so tile-based
// GPUs (Mali, Adreno, PowerVR, Apple) can keep them entirely in on-chip tile memory.
#pragma once

#include "VulkanCommon.h"

namespace cs {

class VulkanContext;

class VulkanImage {
public:
    VulkanImage() = default;
    VulkanImage(const VulkanImage&) = delete;
    VulkanImage& operator=(const VulkanImage&) = delete;
    ~VulkanImage() { destroy(); }

    bool create(VkDevice device, VmaAllocator allocator, VkExtent2D extent, VkFormat format, VkImageUsageFlags usage,
                VkImageAspectFlags aspect, bool transient);
    void destroy();
    // Copies tightly packed pixels into the image and leaves it in SHADER_READ_ONLY_OPTIMAL.
    bool upload(VulkanContext& ctx, const void* pixels, size_t bytes);

    VkImage image() const { return image_; }
    VkImageView view() const { return view_; }
    VkExtent2D extent() const { return extent_; }
    VkFormat format() const { return format_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkImage image_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
    VkFormat format_ = VK_FORMAT_UNDEFINED;
};

} // namespace cs
