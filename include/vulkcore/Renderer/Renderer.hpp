#pragma once

#include <chrono>
#include <memory>
#include <vector>

#include <vulkan/vulkan.h>

#include "vulkcore/Renderer/Mesh.hpp"

struct GLFWwindow;

namespace VulkCore {

class Buffer;
class SwapChain;
class Texture;
class VulkanContext;

class Renderer {
public:
    explicit Renderer(GLFWwindow* window);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void drawFrame();

private:
    void initializeImGui();
    void shutdownImGui();
    void buildImGuiFrame();
    void createRenderPass();
    void createDescriptorSetLayout();
    void createGraphicsPipeline();
    void createMeshData();
    void createVertexBuffer();
    void createIndexBuffer();
    void createTexture();
    void createUniformBuffers();
    void createDescriptorPool();
    void createDescriptorSets();
    void createCommandBuffers();
    void createSyncObjects();
    void updateUniformBuffer();
    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);
    void recreateSwapChain();

    VkShaderModule createShaderModule(const std::vector<char>& code) const;

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

    std::chrono::steady_clock::time_point startTime_;
    std::unique_ptr<VulkanContext> context_;
    std::unique_ptr<SwapChain> swapChain_;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline_ = VK_NULL_HANDLE;
    MeshData meshData_;
    glm::vec3 modelCenter_{0.0f};
    float modelScale_ = 1.0f;
    std::unique_ptr<Buffer> vertexBuffer_;
    std::unique_ptr<Buffer> indexBuffer_;
    uint32_t indexCount_ = 0;
    std::unique_ptr<Texture> texture_;
    std::vector<std::unique_ptr<Buffer>> uniformBuffers_;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets_;

    std::vector<VkCommandBuffer> commandBuffers_;
    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    std::vector<VkFence> inFlightFences_;
    uint32_t currentFrame_ = 0;
    bool framebufferResized_ = false;
    bool imguiInitialized_ = false;
    bool showImGuiDemo_ = false;
    float rotationSpeedDegrees_ = 12.0f;
};

} // namespace VulkCore
