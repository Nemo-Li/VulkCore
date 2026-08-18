#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "vulkcore/Renderer/SwapChain.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "vulkcore/Renderer/VulkanContext.hpp"

namespace VulkCore {

SwapChain::SwapChain(VulkanContext& context)
    : context_(context) {
    createSwapChain();
    createImageViews();
}

SwapChain::~SwapChain() {
    cleanup();
}

void SwapChain::recreate(VkRenderPass renderPass) {
    cleanup();
    createSwapChain();
    createImageViews();
    createFramebuffers(renderPass);
}

void SwapChain::createFramebuffers(VkRenderPass renderPass) {
    cleanupFramebuffers();
    framebuffers_.resize(imageViews_.size());

    for (size_t i = 0; i < imageViews_.size(); ++i) {
        VkImageView attachments[] = {imageViews_[i]};

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
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

VkExtent2D SwapChain::extent() const {
    return extent_;
}

const std::vector<VkFramebuffer>& SwapChain::framebuffers() const {
    return framebuffers_;
}

void SwapChain::createSwapChain() {
    const SwapChainSupportDetails support = context_.querySwapChainSupport(context_.physicalDevice());
    const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(support.formats);
    const VkPresentModeKHR presentMode = choosePresentMode(support.presentModes);
    const VkExtent2D selectedExtent = chooseExtent(support.capabilities);

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
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

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

    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
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

void SwapChain::cleanup() {
    cleanupFramebuffers();

    for (const VkImageView imageView : imageViews_) {
        vkDestroyImageView(context_.device(), imageView, nullptr);
    }
    imageViews_.clear();

    if (swapChain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(context_.device(), swapChain_, nullptr);
        swapChain_ = VK_NULL_HANDLE;
    }
    images_.clear();
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

VkExtent2D SwapChain::chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(context_.window(), &width, &height);

    VkExtent2D actualExtent{
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)
    };
    actualExtent.width = std::clamp(
        actualExtent.width,
        capabilities.minImageExtent.width,
        capabilities.maxImageExtent.width
    );
    actualExtent.height = std::clamp(
        actualExtent.height,
        capabilities.minImageExtent.height,
        capabilities.maxImageExtent.height
    );
    return actualExtent;
}

} // namespace VulkCore
