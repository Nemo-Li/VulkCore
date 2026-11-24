#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include <vulkan/vulkan.h>

namespace VulkCore {

class VulkanContext;

enum class TextureColorSpace { Srgb, Linear };

struct DecodedTexturePixels {
    std::vector<std::uint8_t> rgba;
    std::uint32_t width = 0, height = 0, layers = 1;
};

DecodedTexturePixels decodeTexturePixels(const std::filesystem::path& path);
DecodedTexturePixels decodeTexturePixels(const std::vector<std::uint8_t>& encodedData);
DecodedTexturePixels decodeTextureLayers(const std::vector<std::filesystem::path>& paths);

class Texture {
public:
    Texture(
        VulkanContext& context,
        const std::filesystem::path& path,
        VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        TextureColorSpace colorSpace = TextureColorSpace::Srgb
    );
    // All layers must have the same extent. Layer indices follow path order.
    Texture(
        VulkanContext& context,
        const std::vector<std::filesystem::path>& layerPaths,
        TextureColorSpace colorSpace
    );
    Texture(VulkanContext& context, const std::vector<std::uint8_t>& encodedData,
        TextureColorSpace colorSpace = TextureColorSpace::Srgb,
        VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT);
    Texture(
        VulkanContext& context,
        const std::vector<std::uint8_t>& rgbaPixels,
        std::uint32_t width,
        std::uint32_t height,
        bool arrayView = false,
        TextureColorSpace colorSpace = TextureColorSpace::Srgb,
        std::uint32_t layers = 1,
        VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT
    );
    Texture(VulkanContext& context, const DecodedTexturePixels& image, bool arrayView,
        TextureColorSpace colorSpace = TextureColorSpace::Srgb,
        VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT);
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    VkImageView imageView() const;
    VkSampler sampler() const;

private:
    Texture(VulkanContext& context, const std::vector<std::uint8_t>& rgbaPixels,
        std::uint32_t width, std::uint32_t height, bool arrayView, TextureColorSpace colorSpace,
        std::uint32_t layers, VkSamplerAddressMode addressModeU, VkSamplerAddressMode addressModeV);
    void uploadRgbaPixels(const std::uint8_t* pixels, std::uint32_t width, std::uint32_t height);
    void createImage(uint32_t width, uint32_t height);
    void createImageView();
    void createSampler();
    void transitionImageLayout(VkCommandBuffer commandBuffer, VkImageLayout oldLayout, VkImageLayout newLayout);
    void copyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer buffer, uint32_t width, uint32_t height);
    void generateMipmaps(VkCommandBuffer commandBuffer, uint32_t width, uint32_t height);
    void cleanup();

    VulkanContext& context_;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory_ = VK_NULL_HANDLE;
    VkImageView imageView_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    std::uint32_t mipLevels_ = 1;
    std::uint32_t arrayLayers_ = 1;
    bool arrayView_ = false;
    VkFormat format_ = VK_FORMAT_R8G8B8A8_SRGB;
    VkSamplerAddressMode addressModeU_ = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressModeV_ = VK_SAMPLER_ADDRESS_MODE_REPEAT;
};

} // namespace VulkCore
