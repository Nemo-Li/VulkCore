#pragma once

#include <memory>
#include <vector>

#include <vulkan/vulkan.h>

struct GLFWwindow;

namespace VulkCore {

class Buffer;
class SwapChain;
class VulkanContext;

class Renderer {
public:
    explicit Renderer(GLFWwindow* window);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void drawFrame();

private:
    void createRenderPass();
    void createGraphicsPipeline();
    void createVertexBuffer();
    void createIndexBuffer();
    void createCommandBuffers();
    void createSyncObjects();
    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);
    void recreateSwapChain();

    VkShaderModule createShaderModule(const std::vector<char>& code) const;

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

    std::unique_ptr<VulkanContext> context_;
    std::unique_ptr<SwapChain> swapChain_;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline_ = VK_NULL_HANDLE;
    std::unique_ptr<Buffer> vertexBuffer_;
    std::unique_ptr<Buffer> indexBuffer_;

    std::vector<VkCommandBuffer> commandBuffers_;
    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    std::vector<VkFence> inFlightFences_;
    uint32_t currentFrame_ = 0;
    bool framebufferResized_ = false;
};

} // namespace VulkCore
