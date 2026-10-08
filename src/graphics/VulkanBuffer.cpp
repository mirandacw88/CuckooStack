#include "VulkanBuffer.h"
#include "VulkanContext.h"

#include <cstring>

namespace cs {

VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& o) noexcept {
    if (this != &o) {
        destroy();
        allocator_ = o.allocator_; buffer_ = o.buffer_; allocation_ = o.allocation_; size_ = o.size_; mapped_ = o.mapped_;
        o.buffer_ = VK_NULL_HANDLE; o.allocation_ = VK_NULL_HANDLE; o.mapped_ = nullptr; o.size_ = 0;
    }
    return *this;
}

bool VulkanBuffer::createMapped(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage) {
    destroy();
    allocator_ = allocator; size_ = size;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo info{};
    VK_TRY(vmaCreateBuffer(allocator, &bi, &ai, &buffer_, &allocation_, &info));
    mapped_ = info.pMappedData;
    return mapped_ != nullptr;
}

bool VulkanBuffer::createStatic(VulkanContext& ctx, const void* data, VkDeviceSize size, VkBufferUsageFlags usage) {
    destroy();
    allocator_ = ctx.allocator; size_ = size;
    VulkanBuffer staging;
    if (!staging.createMapped(ctx.allocator, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) return false;
    staging.write(data, static_cast<size_t>(size));
    staging.flush(0, size);

    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VK_TRY(vmaCreateBuffer(ctx.allocator, &bi, &ai, &buffer_, &allocation_, nullptr));
    ctx.immediateSubmit([&](VkCommandBuffer cmd) {
        VkBufferCopy copy{0, 0, size};
        vkCmdCopyBuffer(cmd, staging.handle(), buffer_, 1, &copy);
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    });
    return true;
}

void VulkanBuffer::write(const void* data, size_t bytes, size_t offset) {
    if (mapped_ && bytes) std::memcpy(static_cast<char*>(mapped_) + offset, data, bytes);
}

void VulkanBuffer::flush(VkDeviceSize offset, VkDeviceSize size) {
    // no-op on HOST_COHERENT memory; required on the non-coherent heaps some Mali/Adreno drivers expose
    if (allocation_) vmaFlushAllocation(allocator_, allocation_, offset, size);
}

void VulkanBuffer::destroy() {
    if (buffer_) vmaDestroyBuffer(allocator_, buffer_, allocation_);
    buffer_ = VK_NULL_HANDLE; allocation_ = VK_NULL_HANDLE; mapped_ = nullptr; size_ = 0;
}

} // namespace cs
