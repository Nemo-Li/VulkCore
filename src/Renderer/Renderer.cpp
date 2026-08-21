#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "vulkcore/Renderer/Renderer.hpp"
#include "vulkcore/Terrain/Terrain.hpp"

#include <stdexcept>

#include "vulkcore/Renderer/Buffer.hpp"
#include "vulkcore/Renderer/SwapChain.hpp"
#include "vulkcore/Renderer/Texture.hpp"
#include "vulkcore/Renderer/VulkanContext.hpp"

namespace VulkCore {

Renderer::Renderer(GLFWwindow* window)
    : window_(window), startTime_(std::chrono::steady_clock::now()) {
    if (window == nullptr) {
        throw std::invalid_argument("Renderer requires a valid GLFW window!");
    }

    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);

    context_ = std::make_unique<VulkanContext>(window);
    swapChain_ = std::make_unique<SwapChain>(*context_);

    createRenderPass();
    createSceneRenderResources();
    createDescriptorSetLayout();
    createGraphicsPipeline();
    createPostProcessPipeline();
    createBoundingBoxPipeline();
    createParticlePipelines();
    swapChain_->createFramebuffers(renderPass_);
    loadResourceSettings();
    createMeshData();
    discoverModelResources();
    discoverSceneResources();
    loadTransformSettings();
    createVertexBuffer();
    createIndexBuffer();
    createParticleBuffer();
    createBoundingBoxBuffers();
    createMaterialTextures();
    createParticleTextures();
    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();
    createCommandBuffers();
    createSyncObjects();
    initializeImGui();
}

Renderer::~Renderer() {
    if (!context_) {
        return;
    }

    saveTransformSettings();
    context_->waitIdle();
    shutdownImGui();
    swapChain_->cleanupFramebuffers();
    destroySceneRenderResources();
    screenshotBuffer_.reset();
    uniformBuffers_.clear();
    materialTextures_.clear();
    normalTextures_.clear();
    specularTextures_.clear();
    terrainDetailTextures_ = {};
    particleTextures_.clear();
    fallbackTexture_.reset();
    fallbackNormalTexture_.reset();
    indexBuffer_.reset();
    vertexBuffers_.clear();
    particleVertexBuffers_.clear();
    boundingBoxVertexBuffers_.clear();

    for (size_t i = 0; i < imageAvailableSemaphores_.size(); ++i) {
        vkDestroySemaphore(context_->device(), imageAvailableSemaphores_[i], nullptr);
        vkDestroySemaphore(context_->device(), renderFinishedSemaphores_[i], nullptr);
        vkDestroyFence(context_->device(), inFlightFences_[i], nullptr);
    }

    for (const auto& [key, pipeline] : nodeGraphicsPipelines_) {
        (void)key;
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(context_->device(), pipeline, nullptr);
        }
    }
    for (const auto& [key, pipeline] : nodeComputePipelines_) {
        (void)key;
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(context_->device(), pipeline, nullptr);
        }
    }
    if (graphicsPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(context_->device(), graphicsPipeline_, nullptr);
    }
    if (boundingBoxPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(context_->device(), boundingBoxPipeline_, nullptr);
    }
    if (particleAlphaPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(context_->device(), particleAlphaPipeline_, nullptr);
    }
    if (particleAdditivePipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(context_->device(), particleAdditivePipeline_, nullptr);
    }
    if (postProcessPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(context_->device(), postProcessPipeline_, nullptr);
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(context_->device(), pipelineLayout_, nullptr);
    }
    for (const auto& [shaderName, shaderModuleHandle] : shaderModules_) {
        (void)shaderName;
        if (shaderModuleHandle != VK_NULL_HANDLE) {
            vkDestroyShaderModule(context_->device(), shaderModuleHandle, nullptr);
        }
    }
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(context_->device(), descriptorPool_, nullptr);
    }
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(context_->device(), descriptorSetLayout_, nullptr);
    }
    if (renderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(context_->device(), renderPass_, nullptr);
    }

    swapChain_.reset();
    context_.reset();
}

void Renderer::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    (void)width;
    (void)height;
    auto* renderer = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
    if (renderer != nullptr) {
        renderer->framebufferResized_ = true;
    }
}

} // namespace VulkCore
