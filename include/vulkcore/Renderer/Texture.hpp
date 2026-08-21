#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include <vulkan/vulkan.h>

namespace VulkCore {

class VulkanContext;

class Texture {
public:
    Texture(VulkanContext& context, const std::filesystem::path& path);
    Texture(VulkanContext& context, const std::vector<std::uint8_t>& encodedData);
    Texture(
        VulkanContext& context,
        const std::vector<std::uint8_t>& rgbaPixels,
        std::uint32_t width,
        std::uint32_t height
    );
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    VkImageView imageView() const;
    VkSampler sampler() const;

private:
    void uploadRgbaPixels(const std::uint8_t* pixels, std::uint32_t width, std::uint32_t height);
    void createImage(uint32_t width, uint32_t height);
    void createImageView();
    void createSampler();
    void transitionImageLayout(VkImageLayout oldLayout, VkImageLayout newLayout);
    void copyBufferToImage(VkBuffer buffer, uint32_t width, uint32_t height);
    void generateMipmaps(uint32_t width, uint32_t height);
    void cleanup();

    VulkanContext& context_;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory_ = VK_NULL_HANDLE;
    VkImageView imageView_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    std::uint32_t mipLevels_ = 1;
};

} // namespace VulkCore
