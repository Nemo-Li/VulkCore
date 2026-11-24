#include "vulkcore/Platform/Window.hpp"

#include "HdrComposition.hpp"
#include "SceneLoadTask.hpp"

#ifdef VULKCORE_HAS_EFFECT_EDITOR
#include "../Platform/EffectEditorView.hpp"
#endif

#include "vulkcore/Physics/PhysicsWorld.hpp"
#include "vulkcore/Renderer/Renderer.hpp"
#include "vulkcore/Terrain/Terrain.hpp"

#include <stdexcept>

#include "vulkcore/Renderer/Buffer.hpp"
#include "vulkcore/Renderer/SwapChain.hpp"
#include "vulkcore/Renderer/Texture.hpp"
#include "vulkcore/Renderer/VulkanContext.hpp"

namespace VulkCore {

Renderer::Renderer(
    Window* window,
    const std::filesystem::path& startupScene,
    bool hideTools,
    const std::function<void()>& onSceneLoading,
    Mode mode
)
    : window_(window), mode_(mode), startTime_(std::chrono::steady_clock::now()) {
    if (window == nullptr) {
        throw std::invalid_argument("Renderer requires a valid platform window!");
    }

    setWindowUserPointer(window, this);
    setResizeCallback(window, framebufferResizeCallback);

    physicsWorld_ = std::make_unique<PhysicsWorld>();
    context_ = std::make_unique<VulkanContext>(window);
    swapChain_ = std::make_unique<SwapChain>(*context_);

    createRenderPass();
    createSceneRenderResources();
    createSunShadowResources();
    createDescriptorSetLayout();
    createGraphicsPipeline();
    createPostProcessPipeline();
    skyboxPipeline_ = createSkyboxPipeline(sceneRenderPass_);
    createBoundingBoxPipeline();
    createParticlePipelines();
    swapChain_->createFramebuffers(renderPass_);
    if (mode_ == Mode::Scene) loadResourceSettings();
    createMeshData();
    if (mode_ == Mode::Scene) {
        discoverModelResources();
        discoverSceneResources();
        loadTransformSettings();
    } else {
        ambientLightColor_ = glm::vec3(0.45f, 0.47f, 0.48f);
    }
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
    toolUiVisible_ = !hideTools;
    if (mode_ == Mode::ModelViewer) return;
    if (onSceneLoading) onSceneLoading();
    if (!startupScene.empty() && !loadSceneResource(startupScene)) {
        throw std::runtime_error(
            sceneStatus_.empty()
                ? "failed to load startup scene: " + startupScene.string()
                : sceneStatus_
        );
    }
}

Renderer::~Renderer() {
    sceneLoadCancelled_.store(true, std::memory_order_relaxed);
    sceneLoad_.reset();
    if (!context_) {
        return;
    }

    context_->waitIdle();
#ifdef VULKCORE_HAS_EFFECT_EDITOR
    effectEditorView_.reset();
#endif
#ifndef VULKCORE_OHOS
    destroyResourcePreviewTargets();
    destroyResourcePreviewBuffers();
#endif
    shutdownImGui();
    realtimeEffects_.clear();
    hdrComposition_.reset();
    swapChain_->cleanupFramebuffers();
    destroySceneRenderResources();
    screenshotBuffer_.reset();
    uniformBuffers_.clear();
    materialTextures_.clear();
    normalTextures_.clear();
    specularTextures_.clear();
    alphaMaskTextures_.clear();
    terrainAlbedoLayers_.reset();
    terrainSurfaceLayers_.reset();
    fallbackTerrainLayers_.reset();
    particleTextures_.clear();
    skyboxTexture_.reset();
    terrainGrass_ = {};
    fallbackTexture_.reset();
    fallbackNormalTexture_.reset();
    indexBuffer_.reset();
    grassIndexBuffer_.reset();
    for (auto& frame : grassFrameBuffers_) frame.instances.reset();
    for (auto& frame : roadFrameBuffers_) frame.buffer.reset();
    vertexBuffers_.clear();
    particleVertexBuffers_.clear();
    boundingBoxVertexBuffers_.clear();

    for (size_t i = 0; i < imageAvailableSemaphores_.size(); ++i) {
        vkDestroySemaphore(context_->device(), imageAvailableSemaphores_[i], nullptr);
        vkDestroyFence(context_->device(), inFlightFences_[i], nullptr);
    }
    for (const VkSemaphore semaphore : renderFinishedSemaphores_) {
        vkDestroySemaphore(context_->device(), semaphore, nullptr);
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
    for (VkPipeline pipeline : {
             hdrSkyboxPipeline_, hdrParticleAlphaPipeline_, hdrParticleAdditivePipeline_
         }) {
        vkDestroyPipeline(context_->device(), pipeline, nullptr);
    }
    if (skyboxPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(context_->device(), skyboxPipeline_, nullptr);
    }
    destroySunShadowResources();
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
    physicsWorld_.reset();
}

void Renderer::framebufferResizeCallback(Window* window, int width, int height) {
    (void)width;
    (void)height;
    auto* renderer = static_cast<Renderer*>(windowUserPointer(window));
    if (renderer != nullptr) {
        renderer->framebufferResized_ = true;
    }
}

} // namespace VulkCore
