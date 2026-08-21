#include "vulkcore/Renderer/Buffer.hpp"

#include <cstring>
#include <stdexcept>

#include "vulkcore/Renderer/VulkanContext.hpp"

namespace VulkCore {

Buffer::Buffer(
    VulkanContext& context,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties
)
    : context_(context), size_(size) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(context_.device(), &bufferInfo, nullptr, &buffer_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer!");
    }

    try {
        VkMemoryRequirements memoryRequirements{};
        vkGetBufferMemoryRequirements(context_.device(), buffer_, &memoryRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memoryRequirements.size;
        allocInfo.memoryTypeIndex = context_.findMemoryType(memoryRequirements.memoryTypeBits, properties);

        if (vkAllocateMemory(context_.device(), &allocInfo, nullptr, &memory_) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate buffer memory!");
        }

        if (vkBindBufferMemory(context_.device(), buffer_, memory_, 0) != VK_SUCCESS) {
            throw std::runtime_error("failed to bind buffer memory!");
        }
    } catch (...) {
        if (memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(context_.device(), memory_, nullptr);
            memory_ = VK_NULL_HANDLE;
        }
        vkDestroyBuffer(context_.device(), buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
        throw;
    }
}

Buffer::~Buffer() {
    if (buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(context_.device(), buffer_, nullptr);
    }
    if (memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(context_.device(), memory_, nullptr);
    }
}

VkBuffer Buffer::handle() const {
    return buffer_;
}

VkDeviceMemory Buffer::memory() const {
    return memory_;
}

VkDeviceSize Buffer::size() const {
    return size_;
}

void Buffer::upload(const void* data, VkDeviceSize dataSize) {
    if (dataSize > size_) {
        throw std::out_of_range("buffer upload exceeds buffer size!");
    }
    if (dataSize == 0) {
        return;
    }

    void* mappedData = nullptr;
    if (vkMapMemory(context_.device(), memory_, 0, dataSize, 0, &mappedData) != VK_SUCCESS) {
        throw std::runtime_error("failed to map buffer memory!");
    }

    std::memcpy(mappedData, data, static_cast<size_t>(dataSize));
    vkUnmapMemory(context_.device(), memory_);
}

void Buffer::download(void* data, VkDeviceSize dataSize) const {
    if (data == nullptr && dataSize != 0) {
        throw std::invalid_argument("buffer download requires a destination");
    }
    if (dataSize > size_) {
        throw std::out_of_range("buffer download exceeds buffer size!");
    }
    if (dataSize == 0) {
        return;
    }

    void* mappedData = nullptr;
    if (vkMapMemory(context_.device(), memory_, 0, dataSize, 0, &mappedData) != VK_SUCCESS) {
        throw std::runtime_error("failed to map buffer memory for reading!");
    }

    std::memcpy(data, mappedData, static_cast<size_t>(dataSize));
    vkUnmapMemory(context_.device(), memory_);
}

} // namespace VulkCore
