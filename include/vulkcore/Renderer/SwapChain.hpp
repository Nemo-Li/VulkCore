#pragma once

#include <vector>

#include <vulkan/vulkan.h>

namespace VulkCore {

class VulkanContext;

class SwapChain {
public:
    explicit SwapChain(VulkanContext& context);
    ~SwapChain();

    SwapChain(const SwapChain&) = delete;
    SwapChain& operator=(const SwapChain&) = delete;

    void recreate(VkRenderPass renderPass);
    void createFramebuffers(VkRenderPass renderPass);
    void cleanupFramebuffers();

    VkSwapchainKHR handle() const;
    VkFormat imageFormat() const;
    VkFormat depthFormat() const;
    VkExtent2D extent() const;
    const std::vector<VkFramebuffer>& framebuffers() const;

private:
    void createSwapChain();
    void createImageViews();
    void createDepthResources();
    void cleanupDepthResources();
    void cleanup();

    VkFormat findDepthFormat() const;
    static VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats);
    static VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& presentModes);
    VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;

    VulkanContext& context_;
    VkSwapchainKHR swapChain_ = VK_NULL_HANDLE;
    std::vector<VkImage> images_;
    VkFormat imageFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    std::vector<VkImageView> imageViews_;
    std::vector<VkFramebuffer> framebuffers_;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory_ = VK_NULL_HANDLE;
    VkImageView depthImageView_ = VK_NULL_HANDLE;
};

} // namespace VulkCore
