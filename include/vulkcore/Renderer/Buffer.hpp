#pragma once

#include <vulkan/vulkan.h>

namespace VulkCore {

class VulkanContext;

class Buffer {
public:
    Buffer(
        VulkanContext& context,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties
    );
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    VkBuffer handle() const;
    VkDeviceMemory memory() const;
    VkDeviceSize size() const;

    void upload(const void* data, VkDeviceSize dataSize);
    void download(void* data, VkDeviceSize dataSize) const;

private:
    VulkanContext& context_;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
};

} // namespace VulkCore
