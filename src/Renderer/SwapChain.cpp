#include "vulkcore/Platform/Window.hpp"

#include "vulkcore/Renderer/SwapChain.hpp"

#include <algorithm>
#include <stdexcept>

#include "vulkcore/Renderer/VulkanContext.hpp"
#include "vulkcore/Renderer/SurfacePresentation.hpp"

namespace VulkCore {

SwapChain::SwapChain(VulkanContext& context)
    : context_(context) {
    createSwapChain();
    createImageViews();
    createDepthResources();
}

SwapChain::~SwapChain() {
    cleanup();
}

void SwapChain::recreate(VkRenderPass renderPass) {
    cleanup();
    createSwapChain();
    createImageViews();
    createDepthResources();
    createFramebuffers(renderPass);
}

void SwapChain::createFramebuffers(VkRenderPass renderPass) {
    cleanupFramebuffers();
    framebuffers_.resize(imageViews_.size());

    for (size_t i = 0; i < imageViews_.size(); ++i) {
        VkImageView attachments[] = {imageViews_[i], depthTargets_[i].view};

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 2;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = extent_.width;
        framebufferInfo.height = extent_.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(
                context_.device(),
                &framebufferInfo,
                nullptr,
                &framebuffers_[i]
            ) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }
}

void SwapChain::cleanupFramebuffers() {
    for (const VkFramebuffer framebuffer : framebuffers_) {
        vkDestroyFramebuffer(context_.device(), framebuffer, nullptr);
    }
    framebuffers_.clear();
}

VkSwapchainKHR SwapChain::handle() const {
    return swapChain_;
}

VkFormat SwapChain::imageFormat() const {
    return imageFormat_;
}

VkFormat SwapChain::depthFormat() const {
    return depthFormat_;
}

VkExtent2D SwapChain::extent() const {
    return extent_;
}

VkImage SwapChain::image(uint32_t imageIndex) const {
    if (imageIndex >= images_.size()) {
        throw std::out_of_range("swap chain image index is out of range");
    }
    return images_[imageIndex];
}

bool SwapChain::supportsTransferSource() const {
    return transferSourceSupported_;
}

VkImage SwapChain::depthImage(uint32_t imageIndex) const {
    return depthTargets_.at(imageIndex).image;
}

const std::vector<VkFramebuffer>& SwapChain::framebuffers() const {
    return framebuffers_;
}

void SwapChain::createSwapChain() {
    prepareWindowSurface(context_.window());
    const SwapChainSupportDetails support = context_.querySwapChainSupport(context_.physicalDevice());
    const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(support.formats);
    const VkPresentModeKHR presentMode = choosePresentMode(support.presentModes);
    int width = 0, height = 0;
    framebufferSize(context_.window(), &width, &height);
#ifdef VULKCORE_OHOS
    constexpr bool renderInWindowCoordinates = true;
#else
    constexpr bool renderInWindowCoordinates = false;
#endif
    const auto presentation = chooseSurfacePresentation(support.capabilities,
        {static_cast<uint32_t>(std::max(width, 0)), static_cast<uint32_t>(std::max(height, 0))},
        renderInWindowCoordinates);
    const VkExtent2D selectedExtent = presentation.extent;

    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount) {
        imageCount = support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = context_.surface();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = selectedExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    transferSourceSupported_ = (support.capabilities.supportedUsageFlags &
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    if (transferSourceSupported_) {
        createInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    const QueueFamilyIndices indices = context_.findQueueFamilies(context_.physicalDevice());
    const uint32_t queueFamilyIndices[] = {
        indices.graphicsFamily.value(),
        indices.presentFamily.value()
    };

    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = presentation.transform;
    for (const auto alpha : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
             VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
             VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
             VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) {
        if ((support.capabilities.supportedCompositeAlpha & alpha) != 0) {
            createInfo.compositeAlpha = alpha;
            break;
        }
    }
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(context_.device(), &createInfo, nullptr, &swapChain_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create swap chain!");
    }

    vkGetSwapchainImagesKHR(context_.device(), swapChain_, &imageCount, nullptr);
    images_.resize(imageCount);
    vkGetSwapchainImagesKHR(context_.device(), swapChain_, &imageCount, images_.data());

    imageFormat_ = surfaceFormat.format;
    extent_ = selectedExtent;
    configureWindowPresentation(context_.window());
}

void SwapChain::createImageViews() {
    imageViews_.resize(images_.size());

    for (size_t i = 0; i < images_.size(); ++i) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = images_[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = imageFormat_;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(
                context_.device(),
                &createInfo,
                nullptr,
                &imageViews_[i]
            ) != VK_SUCCESS) {
            throw std::runtime_error("failed to create swap chain image views!");
        }
    }
}

void SwapChain::createDepthResources() {
    depthFormat_ = findDepthFormat();

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = extent_.width;
    imageInfo.extent.height = extent_.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat_;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage =
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    depthTargets_.resize(images_.size());
    try {
        for (auto& target : depthTargets_) {
            if (vkCreateImage(context_.device(), &imageInfo, nullptr, &target.image) != VK_SUCCESS) {
                throw std::runtime_error("failed to create depth image!");
            }
            VkMemoryRequirements memoryRequirements{};
            vkGetImageMemoryRequirements(context_.device(), target.image, &memoryRequirements);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memoryRequirements.size;
            allocInfo.memoryTypeIndex = context_.findMemoryType(
                memoryRequirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            );

            if (vkAllocateMemory(context_.device(), &allocInfo, nullptr, &target.memory) != VK_SUCCESS) {
                throw std::runtime_error("failed to allocate depth image memory!");
            }
            if (vkBindImageMemory(context_.device(), target.image, target.memory, 0) != VK_SUCCESS) {
                throw std::runtime_error("failed to bind depth image memory!");
            }

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = target.image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = depthFormat_;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(context_.device(), &viewInfo, nullptr, &target.view) != VK_SUCCESS) {
                throw std::runtime_error("failed to create depth image view!");
            }
        }
    } catch (...) {
        cleanupDepthResources();
        throw;
    }
}

void SwapChain::cleanupDepthResources() {
    for (const auto& target : depthTargets_) {
        vkDestroyImageView(context_.device(), target.view, nullptr);
        vkDestroyImage(context_.device(), target.image, nullptr);
        vkFreeMemory(context_.device(), target.memory, nullptr);
    }
    depthTargets_.clear();
    depthFormat_ = VK_FORMAT_UNDEFINED;
}

void SwapChain::cleanup() {
    cleanupFramebuffers();
    cleanupDepthResources();

    for (const VkImageView imageView : imageViews_) {
        vkDestroyImageView(context_.device(), imageView, nullptr);
    }
    imageViews_.clear();

    transferSourceSupported_ = false;

    if (swapChain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(context_.device(), swapChain_, nullptr);
        swapChain_ = VK_NULL_HANDLE;
    }
    images_.clear();
}

VkFormat SwapChain::findDepthFormat() const {
    const std::vector<VkFormat> candidates = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT
    };

    for (const VkFormat format : candidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(context_.physicalDevice(), format, &properties);
        // The renderer also samples the off-screen depth attachment for the
        // shader demo lab (fog, SSAO, outlines, and depth of field). Select a
        // format that is valid for both attachment and sampled-image usage.
        if ((properties.optimalTilingFeatures &
                (VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
                 VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) ==
            (VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
             VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
            return format;
        }
    }

    throw std::runtime_error("failed to find a supported depth format!");
}

VkSurfaceFormatKHR SwapChain::chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats.front();
}

VkPresentModeKHR SwapChain::choosePresentMode(const std::vector<VkPresentModeKHR>& presentModes) {
    for (const VkPresentModeKHR presentMode : presentModes) {
        if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return presentMode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

} // namespace VulkCore
