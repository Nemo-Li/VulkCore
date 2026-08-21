#include "vulkcore/Renderer/Texture.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

#include <stb_image.h>

#include "vulkcore/Renderer/Buffer.hpp"
#include "vulkcore/Renderer/VulkanContext.hpp"

namespace VulkCore {

Texture::Texture(VulkanContext& context, const std::filesystem::path& path)
    : context_(context) {
    int width = 0;
    int height = 0;
    const std::string pathString = path.string();
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load(pathString.c_str(), &width, &height, nullptr, STBI_rgb_alpha),
        &stbi_image_free
    );
    if (!pixels || width <= 0 || height <= 0) {
        const char* reason = stbi_failure_reason();
        throw std::runtime_error(
            "failed to load texture image '" + pathString + "': " +
            (reason == nullptr ? "unknown stb_image error" : reason)
        );
    }

    uploadRgbaPixels(pixels.get(), static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

Texture::Texture(VulkanContext& context, const std::vector<std::uint8_t>& encodedData)
    : context_(context) {
    if (encodedData.empty() || encodedData.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("embedded texture data is empty or too large");
    }

    int width = 0;
    int height = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(
            encodedData.data(),
            static_cast<int>(encodedData.size()),
            &width,
            &height,
            nullptr,
            STBI_rgb_alpha
        ),
        &stbi_image_free
    );
    if (!pixels || width <= 0 || height <= 0) {
        const char* reason = stbi_failure_reason();
        throw std::runtime_error(
            "failed to decode embedded texture: " +
            std::string(reason == nullptr ? "unknown stb_image error" : reason)
        );
    }

    uploadRgbaPixels(pixels.get(), static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

Texture::Texture(
    VulkanContext& context,
    const std::vector<std::uint8_t>& rgbaPixels,
    std::uint32_t width,
    std::uint32_t height
)
    : context_(context) {
    if (width == 0 || height == 0) {
        throw std::runtime_error("raw texture dimensions must be non-zero");
    }
    const std::uint64_t pixelCount = static_cast<std::uint64_t>(width) * height;
    if (pixelCount > std::numeric_limits<std::size_t>::max() / 4 ||
        rgbaPixels.size() < static_cast<std::size_t>(pixelCount) * 4) {
        throw std::runtime_error("raw texture data has invalid dimensions");
    }

    uploadRgbaPixels(rgbaPixels.data(), width, height);
}

void Texture::uploadRgbaPixels(
    const std::uint8_t* pixels,
    std::uint32_t width,
    std::uint32_t height
) {
    if (pixels == nullptr || width == 0 || height == 0) {
        throw std::runtime_error("texture pixels and dimensions must be valid");
    }

    const std::uint64_t pixelCount = static_cast<std::uint64_t>(width) * height;
    if (pixelCount > std::numeric_limits<VkDeviceSize>::max() / 4) {
        throw std::runtime_error("texture is too large");
    }
    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(pixelCount) * 4;

    mipLevels_ = 1;
    for (std::uint32_t dimension = std::max(width, height);
         dimension > 1;
         dimension >>= 1) {
        ++mipLevels_;
    }

    VkFormatProperties formatProperties{};
    vkGetPhysicalDeviceFormatProperties(
        context_.physicalDevice(),
        VK_FORMAT_R8G8B8A8_SRGB,
        &formatProperties
    );
    if ((formatProperties.optimalTilingFeatures &
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) == 0) {
        // Mip generation below uses a linear blit. Keeping the base level is
        // safer than rejecting otherwise valid textures on such devices.
        mipLevels_ = 1;
    }

    try {
        Buffer stagingBuffer(
            context_,
            imageSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        stagingBuffer.upload(pixels, imageSize);

        createImage(width, height);
        transitionImageLayout(
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        );
        copyBufferToImage(stagingBuffer.handle(), width, height);
        generateMipmaps(width, height);
        createImageView();
        createSampler();
    } catch (...) {
        cleanup();
        throw;
    }
}

Texture::~Texture() {
    cleanup();
}

VkImageView Texture::imageView() const {
    return imageView_;
}

VkSampler Texture::sampler() const {
    return sampler_;
}

void Texture::createImage(uint32_t width, uint32_t height) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = mipLevels_;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(context_.device(), &imageInfo, nullptr, &image_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create texture image!");
    }

    try {
        VkMemoryRequirements memoryRequirements{};
        vkGetImageMemoryRequirements(context_.device(), image_, &memoryRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memoryRequirements.size;
        allocInfo.memoryTypeIndex = context_.findMemoryType(
            memoryRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );

        if (vkAllocateMemory(context_.device(), &allocInfo, nullptr, &imageMemory_) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate texture image memory!");
        }
        if (vkBindImageMemory(context_.device(), image_, imageMemory_, 0) != VK_SUCCESS) {
            throw std::runtime_error("failed to bind texture image memory!");
        }
    } catch (...) {
        if (imageMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(context_.device(), imageMemory_, nullptr);
            imageMemory_ = VK_NULL_HANDLE;
        }
        vkDestroyImage(context_.device(), image_, nullptr);
        image_ = VK_NULL_HANDLE;
        throw;
    }
}

void Texture::createImageView() {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels_;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(context_.device(), &viewInfo, nullptr, &imageView_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create texture image view!");
    }
}

void Texture::createSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkPhysicalDeviceFeatures supportedFeatures{};
    vkGetPhysicalDeviceFeatures(context_.physicalDevice(), &supportedFeatures);
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(context_.physicalDevice(), &properties);
    const bool anisotropySupported = supportedFeatures.samplerAnisotropy == VK_TRUE;
    samplerInfo.anisotropyEnable = anisotropySupported ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = anisotropySupported
        ? std::min(8.0f, properties.limits.maxSamplerAnisotropy)
        : 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mipLevels_ - 1);

    if (vkCreateSampler(context_.device(), &samplerInfo, nullptr, &sampler_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create texture sampler!");
    }
}

void Texture::transitionImageLayout(VkImageLayout oldLayout, VkImageLayout newLayout) {
    if (!((oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
           newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) ||
          (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
           newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))) {
        throw std::invalid_argument("unsupported texture image layout transition!");
    }

    VkCommandBuffer commandBuffer = context_.beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels_;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage = 0;
    VkPipelineStageFlags destinationStage = 0;
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage,
        destinationStage,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );
    context_.endSingleTimeCommands(commandBuffer);
}

void Texture::copyBufferToImage(VkBuffer buffer, uint32_t width, uint32_t height) {
    VkCommandBuffer commandBuffer = context_.beginSingleTimeCommands();

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(
        commandBuffer,
        buffer,
        image_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );
    context_.endSingleTimeCommands(commandBuffer);
}

void Texture::generateMipmaps(uint32_t width, uint32_t height) {
    if (mipLevels_ == 1) {
        transitionImageLayout(
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
        return;
    }

    VkCommandBuffer commandBuffer = context_.beginSingleTimeCommands();
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    std::int32_t sourceWidth = static_cast<std::int32_t>(width);
    std::int32_t sourceHeight = static_cast<std::int32_t>(height);
    for (std::uint32_t level = 1; level < mipLevels_; ++level) {
        barrier.subresourceRange.baseMipLevel = level - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier
        );

        const std::int32_t targetWidth = std::max(1, sourceWidth / 2);
        const std::int32_t targetHeight = std::max(1, sourceHeight / 2);
        VkImageBlit blit{};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = level - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.srcOffsets[1] = {sourceWidth, sourceHeight, 1};
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = level;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;
        blit.dstOffsets[1] = {targetWidth, targetHeight, 1};
        vkCmdBlitImage(
            commandBuffer,
            image_,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image_,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &blit,
            VK_FILTER_LINEAR
        );

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier
        );

        sourceWidth = targetWidth;
        sourceHeight = targetHeight;
    }

    barrier.subresourceRange.baseMipLevel = mipLevels_ - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );
    context_.endSingleTimeCommands(commandBuffer);
}

void Texture::cleanup() {
    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(context_.device(), sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    if (imageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(context_.device(), imageView_, nullptr);
        imageView_ = VK_NULL_HANDLE;
    }
    if (image_ != VK_NULL_HANDLE) {
        vkDestroyImage(context_.device(), image_, nullptr);
        image_ = VK_NULL_HANDLE;
    }
    if (imageMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(context_.device(), imageMemory_, nullptr);
        imageMemory_ = VK_NULL_HANDLE;
    }
}

} // namespace VulkCore
