// VMA-backed buffers: device-local static geometry (staged upload) and persistently mapped
// host-visible buffers for per-frame data (UBO, instance streams).
#pragma once

#include "VulkanCommon.h"

#include <cstddef>

namespace cs {

class VulkanContext;

class VulkanBuffer {
public:
    VulkanBuffer() = default;
    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;
    VulkanBuffer(VulkanBuffer&& o) noexcept { *this = static_cast<VulkanBuffer&&>(o); }
    VulkanBuffer& operator=(VulkanBuffer&& o) noexcept;
    ~VulkanBuffer() { destroy(); }

    // Host-visible, persistently mapped, sequential-write (VBO/IBO streams, UBO). On UMA mobile GPUs VMA
    // places this in device-local + host-visible memory automatically.
    bool createMapped(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage);
    // Device-local, filled once through a staging buffer.
    bool createStatic(VulkanContext& ctx, const void* data, VkDeviceSize size, VkBufferUsageFlags usage);
    void destroy();

    void write(const void* data, size_t bytes, size_t offset = 0);
    void flush(VkDeviceSize offset, VkDeviceSize size);
    void invalidate() { if (allocation_) vmaInvalidateAllocation(allocator_, allocation_, 0, VK_WHOLE_SIZE); }

    VkBuffer handle() const { return buffer_; }
    VkDeviceSize size() const { return size_; }
    void* mapped() const { return mapped_; }

private:
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
    void* mapped_ = nullptr;
};

} // namespace cs
