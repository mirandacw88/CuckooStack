#include "VulkanImage.h"
#include "VulkanBuffer.h"
#include "VulkanContext.h"

#include <algorithm>

namespace cs {

bool VulkanImage::create(VkDevice device, VmaAllocator allocator, VkExtent2D extent, VkFormat format, VkImageUsageFlags usage,
                         VkImageAspectFlags aspect, bool transient, uint32_t mipLevels) {
    destroy();
    device_ = device; allocator_ = allocator; extent_ = extent; format_ = format; mips_ = std::max(1u, mipLevels);
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = format;
    ci.extent = {extent.width, extent.height, 1};
    ci.mipLevels = mips_;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage | (transient ? VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT : 0);
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo ai{};
    ai.usage = transient ? VMA_MEMORY_USAGE_GPU_LAZILY_ALLOCATED : VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VkResult r = vmaCreateImage(allocator, &ci, &ai, &image_, &allocation_, nullptr);
    if (r != VK_SUCCESS && transient) { // desktop GPUs have no lazily allocated heap
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        r = vmaCreateImage(allocator, &ci, &ai, &image_, &allocation_, nullptr);
    }
    VK_TRY(r);

    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = image_;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = {aspect, 0, mips_, 0, 1};
    VK_TRY(vkCreateImageView(device, &vi, nullptr, &view_));
    return true;
}

bool VulkanImage::upload(VulkanContext& ctx, const void* pixels, size_t bytes) {
    const Level l{pixels, bytes};
    return uploadMips(ctx, &l, 1);
}

bool VulkanImage::uploadMips(VulkanContext& ctx, const Level* levels, uint32_t count) {
    count = std::min(count, mips_);
    size_t bytes = 0;
    for (uint32_t i = 0; i < count; ++i) bytes += levels[i].bytes;
    VulkanBuffer staging;
    if (!staging.createMapped(ctx.allocator, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) return false;
    {
        size_t off = 0;
        for (uint32_t i = 0; i < count; ++i) { staging.write(levels[i].pixels, levels[i].bytes, off); off += levels[i].bytes; }
    }
    staging.flush(0, bytes);
    ctx.immediateSubmit([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image_;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips_, 0, 1};
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        size_t off = 0;
        for (uint32_t i = 0; i < count; ++i) {
            VkBufferImageCopy copy{};
            copy.bufferOffset = off;
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};
            copy.imageExtent = {std::max(1u, extent_.width >> i), std::max(1u, extent_.height >> i), 1};
            vkCmdCopyBufferToImage(cmd, staging.handle(), image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            off += levels[i].bytes;
        }
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    });
    return true;
}

void VulkanImage::destroy() {
    if (view_) vkDestroyImageView(device_, view_, nullptr);
    if (image_) vmaDestroyImage(allocator_, image_, allocation_);
    view_ = VK_NULL_HANDLE; image_ = VK_NULL_HANDLE; allocation_ = VK_NULL_HANDLE;
}

} // namespace cs
