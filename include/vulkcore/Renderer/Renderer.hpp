#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
    struct SceneRenderTarget {
        VkImage colorImage = VK_NULL_HANDLE;
        VkDeviceMemory colorMemory = VK_NULL_HANDLE;
        VkImageView colorImageView = VK_NULL_HANDLE;
        VkSampler colorSampler = VK_NULL_HANDLE;
        VkImage depthImage = VK_NULL_HANDLE;
        VkDeviceMemory depthMemory = VK_NULL_HANDLE;
        VkImageView depthImageView = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
    };

    void initializeImGui();
    void shutdownImGui();
    void buildImGuiFrame();
    void loadTransformSettings();
    void saveTransformSettings() const;
    void loadSunNodeSettings();
    void createBoundingBoxPipeline();
    void createBoundingBoxBuffers();
    void updateBoundingBoxVertices();
    void updateBoundingBoxBuffer();
    void selectModelNode(ModelNode& node);
    glm::mat4 nodeAnimationTransform(const ModelNode& node, float elapsedSeconds) const;
    glm::mat4 animationTransformForNode(
        const ModelNode& node,
        std::uint32_t targetNodeId,
        float elapsedSeconds,
        const glm::mat4& parentAnimation,
        bool& found
    ) const;
    glm::mat4 animationTransformForSceneNode(
        std::uint32_t targetNodeId,
        float elapsedSeconds,
        bool& found
    ) const;
    void createRenderPass();
    void createSceneRenderResources();
    void destroySceneRenderResources();
    void createDescriptorSetLayout();
    void createGraphicsPipeline();
    void createMeshData();
    void createSunMesh();
    void createVertexBuffer();
    void createIndexBuffer();
    void createMaterialTextures();
    void rebuildMaterialResources();
    void createUniformBuffers();
    void createDescriptorPool();
    void createDescriptorSets();
    void updateSceneTextureDescriptors();
    void createCommandBuffers();
    void createSyncObjects();
    void updateUniformBuffer();
    void drawModelHierarchy(ModelNode& node);
    void discoverShaderBinaries();
    VkShaderModule shaderModule(const std::string& shaderName);
    VkPipeline createMaterialGraphicsPipeline(
        const ShaderConfiguration& configuration,
        const MaterialData& material
    );
    VkPipeline graphicsPipelineForMaterial(
        const MaterialData& material
    );
    VkPipeline computePipelineForMaterial(const MaterialData& material);
    std::string graphicsPipelineKey(
        const ShaderConfiguration& configuration,
        const MaterialData& material
    ) const;
    std::uint32_t materialIndexForPart(const MeshPart& part) const;
    std::int32_t inheritedMaterialOverrideForSceneNode(std::uint32_t nodeId) const;
    void drawNodeMaterialControls(ModelNode& node, bool& shouldSaveSettings);
    void drawMaterialShaderControls(MaterialData& material, bool& shouldSaveSettings);
    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);
    void recreateSwapChain();

    VkShaderModule createShaderModule(const std::vector<char>& code) const;

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

    std::chrono::steady_clock::time_point startTime_;
    std::unique_ptr<VulkanContext> context_;
    std::unique_ptr<SwapChain> swapChain_;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkRenderPass sceneRenderPass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline_ = VK_NULL_HANDLE;
    VkPipeline boundingBoxPipeline_ = VK_NULL_HANDLE;
    std::array<std::vector<std::string>, kShaderStageCount> availableShaders_;
    std::unordered_map<std::string, VkShaderModule> shaderModules_;
    std::unordered_map<std::string, VkPipeline> nodeGraphicsPipelines_;
    std::unordered_map<std::string, VkPipeline> nodeComputePipelines_;
    std::unordered_set<std::string> failedNodeGraphicsPipelineKeys_;
    std::unordered_set<std::string> failedNodeComputePipelineKeys_;
    std::vector<SceneRenderTarget> sceneRenderTargets_;
    MeshData meshData_;
    glm::vec3 modelCenter_{0.0f};
    float modelScale_ = 1.0f;
    float modelScaleFactor_ = 1.0f;
    std::unique_ptr<Buffer> vertexBuffer_;
    std::unique_ptr<Buffer> indexBuffer_;
    std::vector<std::unique_ptr<Buffer>> boundingBoxVertexBuffers_;
    std::vector<std::unique_ptr<Texture>> materialTextures_;
    std::vector<std::unique_ptr<Texture>> normalTextures_;
    std::vector<std::unique_ptr<Texture>> specularTextures_;
    std::unique_ptr<Texture> fallbackTexture_;
    std::unique_ptr<Texture> fallbackNormalTexture_;
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
    bool autoRotate_ = false;
    glm::vec3 modelTranslation_{0.0f};
    glm::vec3 modelRotationDegrees_{0.0f};
    float rotationSpeedDegrees_ = 12.0f;
    glm::vec3 ambientLightColor_{0.18f, 0.21f, 0.24f};
    float ambientLightIntensity_ = 1.0f;
    glm::vec3 sunLightDirection_{0.45f, 0.55f, 0.75f};
    glm::vec3 sunLightColor_{0.82f, 0.86f, 0.9f};
    float sunLightIntensity_ = 1.0f;
    std::uint32_t sunNodeId_ = 0;
    std::uint32_t sunMaterialIndex_ = 0;
    ModelNode* selectedModelNode_ = nullptr;
    std::array<char, 128> materialNameBuffer_{};
    std::int32_t materialNameMaterialIndex_ = -1;
    std::array<char, 512> materialTexturePathBuffer_{};
    std::int32_t materialTexturePathMaterialIndex_ = -1;
    std::array<char, 512> materialNormalTexturePathBuffer_{};
    std::int32_t materialNormalTexturePathMaterialIndex_ = -1;
    std::array<char, 512> materialSpecularTexturePathBuffer_{};
    std::int32_t materialSpecularTexturePathMaterialIndex_ = -1;
    std::array<Vertex, 24> boundingBoxVertices_{};
};

} // namespace VulkCore
