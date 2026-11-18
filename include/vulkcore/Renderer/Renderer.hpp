#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <vulkan/vulkan.h>

#include "vulkcore/Core/Config.hpp"
#include "vulkcore/Gameplay/Player.hpp"
#include "vulkcore/Renderer/Mesh.hpp"
#include "vulkcore/Renderer/SceneEffects.hpp"
#include "vulkcore/Renderer/ParticleSystem.hpp"
#include "vulkcore/Renderer/Texture.hpp"

#include "vulkcore/Platform/Window.hpp"

namespace VulkCore {

class Buffer;
class PhysicsWorld;
class SwapChain;
class VulkanContext;
class TerrainScene;
class HdrComposition;

class Renderer {
public:
    explicit Renderer(
        Window* window,
        const std::filesystem::path& startupScene = {},
        bool hideTools = false,
        const std::function<void()>& onSceneLoading = {}
    );
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Returns true once the presentation queue accepts the frame.
    bool drawFrame();
    void captureTransitionFrame(float progress, float time, const std::filesystem::path& output);

private:
    struct SceneRenderTarget {
        VkImage colorImage = VK_NULL_HANDLE;
        VkDeviceMemory colorMemory = VK_NULL_HANDLE;
        VkImageView colorImageView = VK_NULL_HANDLE;
        VkSampler colorSampler = VK_NULL_HANDLE;
        VkImage depthImage = VK_NULL_HANDLE;
        VkDeviceMemory depthMemory = VK_NULL_HANDLE;
        VkImageView depthImageView = VK_NULL_HANDLE;
        VkSampler depthSampler = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
    };

    struct SceneModelResource {
        std::uint32_t rootNodeId = 0;
        std::filesystem::path path;
        std::uint32_t materialStart = 0;
        std::uint32_t materialCount = 0;
        bool playerModel = false;
    };

    struct ModelResourceCacheEntry {
        std::shared_ptr<MeshData> mesh;
        std::vector<std::filesystem::path> texturePaths;
        std::string error;
    };

    void initializeImGui();
    void shutdownImGui();
    void buildImGuiFrame();
    void drawMiniMap();
    void loadResourceSettings();
    void loadTransformSettings();
    bool saveTransformSettings() const;
    void initializeEmptyScene();
    void appendTerrainToScene(
        const TerrainScene& terrain,
        const std::filesystem::path& terrainRoot
    );
    void appendWaterToScene(const TerrainScene& terrain);
    void appendPlayerToScene();
    void appendGrassToScene(const TerrainScene& terrain, const glm::vec3& center);
    void updatePlayerController(float deltaSeconds);
    void updatePlayerCamera();
    void clearSceneData();
    const SceneModelResource* sceneModelResourceForNode(const ModelNode& node) const;
    void recalculateSceneBounds();
    void appendImportedModel(
        MeshData importedMesh,
        const std::filesystem::path& path,
        std::optional<std::uint32_t> preferredRootNodeId = std::nullopt,
        bool playerModel = false
    );
    void synchronizeModelResourceMaterial(
        const std::filesystem::path& path,
        std::size_t materialIndex,
        const MaterialData& material
    );
    void refreshModelResourceCacheFromScene(const std::filesystem::path& path);
    void discoverModelTextureResources(
        const std::filesystem::path& modelPath,
        ModelResourceCacheEntry& cache
    );
    void createNewEmptyScene();
    void discoverSceneResources();
    bool saveSceneResource(const std::filesystem::path& path);
    bool loadSceneResource(const std::filesystem::path& path);
    void createBoundingBoxPipeline();
    void createParticlePipelines();
    void createPostProcessPipeline();
    VkPipeline createSkyboxPipeline(VkRenderPass pass);
    VkPipeline createParticlePipeline(bool additiveBlending, VkRenderPass pass);
    void configureHdrComposition();
    void updateTransition(float seconds);
    void createBoundingBoxBuffers();
    void updateBoundingBoxVertices();
    void updateBoundingBoxBuffer();
    void createParticleBuffer();
    void updateParticleBuffer();
    void createParticleTextures();
    void loadParticleTexture(std::uint32_t nodeId);
    void createParticleDescriptorSets(std::uint32_t nodeId);
    void updateParticleTextureDescriptors(std::uint32_t nodeId);
    void updateParticleSystems(
        float deltaSeconds,
        const glm::vec3& cameraRight,
        const glm::vec3& cameraUp
    );
    void createParticleSystemObject();
    void createTornadoMeshEffect(bool alwaysVisible = false);
    void createPlayerAttackEffect();
    void updatePlayerAttackEffect();
    bool removeSceneNodeById(std::uint32_t nodeId);
    void selectModelNode(ModelNode& node);
    void updateSceneNodeTransforms(float elapsedSeconds, float deltaSeconds);
    void configurePlayerRunAnimation();
    void updateSkeletalAnimations(float deltaSeconds);
    glm::mat4 transformForSceneNode(std::uint32_t nodeId) const;
    bool isSceneNodeRenderEnabled(std::uint32_t nodeId) const;
    glm::mat4 nodeManualTransform(const ModelNode& node) const;
    glm::mat4 particleObjectTransform(const ModelNode& node) const;
    glm::mat4 playerVisualTransform() const;
    glm::mat4 nodeAnimationTransform(const ModelNode& node, float elapsedSeconds) const;
    void createRenderPass();
    void createSceneRenderResources();
    void destroySceneRenderResources();
    void createDescriptorSetLayout();
    void createGraphicsPipeline();
    void createMeshData();
    void createSunMesh(
        const ModelNode* preservedNode = nullptr,
        const MaterialData* preservedMaterial = nullptr
    );
    void removeSunMesh();
    void rebuildSceneGeometryResources();
    void createVertexBuffer();
    void createIndexBuffer();
    void createMaterialTextures();
    void createSkyboxTexture();
    void rebuildMaterialResources();
    void createUniformBuffers();
    void createDescriptorPool();
    void createDescriptorSets();
    void updateSceneTextureDescriptors();
    void createCommandBuffers();
    void createSyncObjects();
    void recreatePresentSemaphores();
    void updateCameraBasis();
    void updateUniformBuffer();
    void drawModelHierarchy(ModelNode& node);
    void drawResourceModelHierarchy(const ModelNode& node);
    void drawResourceManager(bool& shouldSaveSettings);
    void drawMaterialControls(
        MaterialData& material,
        std::int32_t materialKey,
        bool& shouldSaveSettings,
        const std::filesystem::path* modelResourcePath = nullptr
    );
    bool importModelResource(
        const std::filesystem::path& path,
        std::optional<std::uint32_t> preferredRootNodeId = std::nullopt,
        bool playerModel = false
    );
    void discoverModelResources();
    void selectModelResource(const std::filesystem::path& path);
    void discoverShaderBinaries();
    VkShaderModule shaderModule(const std::string& shaderName);
    VkPipeline createMaterialGraphicsPipeline(
        const ShaderConfiguration& configuration,
        const MaterialData& material,
        VkRenderPass pass,
        VkCullModeFlags cullMode
    );
    VkPipeline graphicsPipelineForMaterial(
        const MaterialData& material, bool hdr, VkCullModeFlags cullMode
    );
    VkPipeline computePipelineForMaterial(const MaterialData& material);
    std::string graphicsPipelineKey(
        const ShaderConfiguration& configuration,
        const MaterialData& material, bool hdr, VkCullModeFlags cullMode
    ) const;
    std::uint32_t materialIndexForPart(const MeshPart& part) const;
    std::int32_t inheritedMaterialOverrideForSceneNode(std::uint32_t nodeId) const;
    void drawNodeMaterialControls(ModelNode& node, bool& shouldSaveSettings);
    void drawSkeletalAnimationControls(ModelNode& node, bool& shouldSaveSettings);
    void drawMaterialShaderControls(MaterialData& material, bool& shouldSaveSettings);
    void drawShaderDemoControls(bool& shouldSaveSettings);
    void drawParticleSystemControls(ModelNode& node, bool& shouldSaveSettings);
    void recordLegacySceneTransfer(VkCommandBuffer commandBuffer, uint32_t imageIndex);
    void recordCommandBuffer(
        VkCommandBuffer commandBuffer,
        uint32_t imageIndex,
        bool captureScreenshot
    );
    void ensureScreenshotBuffer();
    void recordScreenshotCopy(VkCommandBuffer commandBuffer, uint32_t imageIndex);
    void saveScreenshot(const std::filesystem::path& output);
    void recreateSwapChain();

    VkShaderModule createShaderModule(const std::vector<char>& code) const;

    static void framebufferResizeCallback(Window* window, int width, int height);

    Window* window_ = nullptr;
    std::chrono::steady_clock::time_point startTime_;
    std::unique_ptr<VulkanContext> context_;
    std::unique_ptr<SwapChain> swapChain_;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkRenderPass sceneRenderPass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline_ = VK_NULL_HANDLE;
    VkPipeline boundingBoxPipeline_ = VK_NULL_HANDLE;
    VkPipeline particleAlphaPipeline_ = VK_NULL_HANDLE;
    VkPipeline particleAdditivePipeline_ = VK_NULL_HANDLE;
    VkPipeline postProcessPipeline_ = VK_NULL_HANDLE;
    VkPipeline skyboxPipeline_ = VK_NULL_HANDLE;
    VkPipeline hdrSkyboxPipeline_ = VK_NULL_HANDLE;
    VkPipeline hdrParticleAlphaPipeline_ = VK_NULL_HANDLE;
    VkPipeline hdrParticleAdditivePipeline_ = VK_NULL_HANDLE;
    std::unique_ptr<HdrComposition> hdrComposition_;
    HdrSettings hdrSettings_;
    AuthoredView authoredView_;
    TransitionSettings transitionSettings_;
    TransitionPlayer transitionPlayer_;
    glm::vec3 transitionCameraStart_{0.0f};
    float transitionCameraBaseDistance_ = 0.0f;
    float transitionCameraDistance_ = 0.0f;
    float transitionCameraOffset_ = 0.0f;
    float transitionCameraFov_ = 45.0f;
    bool transitionHeld_ = false;
    std::filesystem::path fixedFrameOutput_;
    std::array<std::vector<std::string>, kShaderStageCount> availableShaders_;
    std::unordered_map<std::string, VkShaderModule> shaderModules_;
    std::unordered_map<std::string, VkPipeline> nodeGraphicsPipelines_;
    std::unordered_map<std::string, VkPipeline> nodeComputePipelines_;
    std::unordered_set<std::string> failedNodeGraphicsPipelineKeys_;
    std::unordered_set<std::string> failedNodeComputePipelineKeys_;
    std::vector<SceneRenderTarget> sceneRenderTargets_;
    MeshData meshData_;
    std::unique_ptr<TerrainScene> terrainScene_;
    std::unique_ptr<PhysicsWorld> physicsWorld_;
    std::filesystem::path terrainResourcePath_;
    std::filesystem::path waterResourcePath_;
    PlayerController playerController_;
    std::optional<glm::vec3> playerSpawnPosition_;
    std::filesystem::path playerModelResourcePath_;
    float playerModelAutoScale_ = 1.0f;
    float playerModelGroundOffset_ = 0.0f;
    std::optional<std::uint32_t> playerRunAnimationClipIndex_;
    std::optional<std::uint32_t> playerAttackAnimationClipIndex_;
    std::optional<std::uint32_t> playerLightningAnimationClipIndex_;
    std::optional<std::uint32_t> playerFlameAnimationClipIndex_;
    static constexpr std::uint32_t kPlayerSceneNodeId = 0xE2000000u;
    static constexpr std::uint32_t kPlayerAttackParticleNodeId = 0xDFFFFFF0u;
    static constexpr std::uint32_t kPlayerLightningParticleNodeId = 0xDFFFFFF1u;
    static constexpr std::uint32_t kPlayerFlameParticleNodeId = 0xDFFFFFF2u;
    std::unordered_map<std::uint32_t, glm::mat4> sceneNodeTransforms_;
    std::unordered_map<std::uint32_t, bool> sceneNodeRenderEnabled_;
    std::unordered_set<std::uint32_t> removedSceneNodeIds_;
    glm::vec3 modelCenter_{0.0f};
    float modelScale_ = 1.0f;
    std::vector<std::unique_ptr<Buffer>> vertexBuffers_;
    std::unique_ptr<Buffer> indexBuffer_;
    std::vector<std::unique_ptr<Buffer>> particleVertexBuffers_;
    std::vector<ParticleVertex> particleVertices_;
    struct ParticleDrawRange {
        std::uint32_t nodeId = 0;
        std::uint32_t firstVertex = 0;
        std::uint32_t vertexCount = 0;
        ParticleSystemSettings::BlendMode blendMode =
            ParticleSystemSettings::BlendMode::Additive;
    };
    std::vector<ParticleDrawRange> particleDrawRanges_;
    std::vector<std::unique_ptr<Buffer>> boundingBoxVertexBuffers_;
    std::vector<std::unique_ptr<Texture>> materialTextures_;
    std::vector<std::unique_ptr<Texture>> normalTextures_;
    std::vector<std::unique_ptr<Texture>> specularTextures_;
    static constexpr std::size_t kTerrainDetailTextureCount = 8;
    std::array<std::unique_ptr<Texture>, kTerrainDetailTextureCount>
        terrainDetailTextures_{};
    bool terrainDetailTexturesReady_ = false;
    std::unordered_map<std::uint32_t, std::unique_ptr<Texture>> particleTextures_;
    std::unique_ptr<Texture> skyboxTexture_;
    std::unique_ptr<Texture> fallbackTexture_;
    std::unique_ptr<Texture> fallbackNormalTexture_;
    std::vector<std::unique_ptr<Buffer>> uniformBuffers_;
    std::unique_ptr<Buffer> screenshotBuffer_;
    VkDeviceSize screenshotBufferSize_ = 0;
    bool screenshotKeyDown_ = false;
    std::uint64_t screenshotSequence_ = 0;
    std::string screenshotStatus_ = "F12: Save the current frame";
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets_;
    std::vector<VkDescriptorSet> skyboxDescriptorSets_;
    std::unordered_map<std::uint32_t, std::array<VkDescriptorSet, kMaxFramesInFlight>>
        particleDescriptorSets_;

    std::vector<VkCommandBuffer> commandBuffers_;
    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    std::vector<VkFence> inFlightFences_;
    uint32_t currentFrame_ = 0;
    bool framebufferResized_ = false;
    bool imguiInitialized_ = false;
    bool toolUiVisible_ = true;
    glm::vec3 cameraPosition_{0.0f, 0.0f, 4.0f};
    glm::vec3 cameraForward_{0.0f, 0.0f, -1.0f};
    glm::vec3 cameraRight_{1.0f, 0.0f, 0.0f};
    glm::vec3 cameraUp_{0.0f, 1.0f, 0.0f};
    bool cameraBasisInitialized_ = false;
    glm::vec3 cameraTarget_{0.0f};
    glm::vec3 ambientLightColor_{0.18f, 0.21f, 0.24f};
    float ambientLightIntensity_ = 1.0f;
    glm::vec3 sunLightDirection_{0.45f, 0.55f, 0.75f};
    glm::vec3 sunLightColor_{0.82f, 0.86f, 0.9f};
    float sunLightIntensity_ = 1.0f;
    std::uint32_t postProcessEffect_ = 0;
    float postProcessStrength_ = 1.0f;
    float postProcessParameter_ = 0.5f;
    float postProcessSplitPosition_ = 0.5f;
    bool postProcessSplitView_ = false;
    std::uint32_t sunNodeId_ = 0;
    std::uint32_t sunMaterialIndex_ = 0;
    std::uint32_t sunPartIndex_ = 0;
    std::uint32_t sunVertexStart_ = 0;
    std::uint32_t sunIndexStart_ = 0;
    bool sunPresent_ = false;
    ModelNode* selectedModelNode_ = nullptr;
    std::array<char, 128> materialNameBuffer_{};
    std::int32_t materialNameMaterialIndex_ = -1;
    std::array<char, 512> materialTexturePathBuffer_{};
    std::int32_t materialTexturePathMaterialIndex_ = -1;
    std::array<char, 512> materialNormalTexturePathBuffer_{};
    std::int32_t materialNormalTexturePathMaterialIndex_ = -1;
    std::array<char, 512> materialSpecularTexturePathBuffer_{};
    std::int32_t materialSpecularTexturePathMaterialIndex_ = -1;
    std::string materialEditorContext_;
    enum class ResourceSelectionType : std::uint8_t {
        Model = 0,
        Material,
        Texture,
    };
    enum class ResourceTextureSlot : std::uint8_t {
        BaseColor = 0,
        Normal,
        Specular,
    };
    std::array<char, 1024> modelImportPathBuffer_{};
    std::filesystem::path assetsDirectory_;
    std::vector<std::filesystem::path> modelResourcePaths_;
    std::vector<SceneModelResource> sceneModelResources_;
    std::uint32_t nextSceneModelInstanceIndex_ = 0;
    std::filesystem::path selectedModelResourcePath_;
    std::shared_ptr<MeshData> selectedModelPreview_;
    std::string selectedModelPreviewError_;
    std::unordered_map<std::string, ModelResourceCacheEntry> modelResourceCache_;
    std::filesystem::path scenesDirectory_;
    std::vector<std::filesystem::path> sceneResourcePaths_;
    std::filesystem::path currentScenePath_;
    std::array<char, 512> scenePathBuffer_{};
    std::array<char, 512> ohosExportPathBuffer_{};
    std::string ohosExportStatus_;
    bool ohosExportFailed_ = false;
    std::string sceneStatus_;
    bool sceneStatusFailed_ = false;
    ResourceSelectionType selectedResourceType_ = ResourceSelectionType::Model;
    ResourceTextureSlot selectedResourceTextureSlot_ = ResourceTextureSlot::BaseColor;
    std::int32_t selectedResourceMaterialIndex_ = -1;
    std::string resourceImportStatus_;
    bool resourceImportFailed_ = false;
    std::array<Vertex, 24> boundingBoxVertices_{};
    std::unordered_map<std::uint32_t, ParticleSystem> particleSystems_;
    std::array<char, 512> particleTexturePathBuffer_{};
    std::uint32_t particleTexturePathNodeId_ = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t tornadoMeshMaterialIndex_ = 0;
    std::uint32_t tornadoMeshPartIndex_ = 0;
    bool tornadoMeshPresent_ = false;
    bool tornadoMeshVisible_ = false;
    bool tornadoMeshAlwaysVisible_ = false;
    glm::mat4 tornadoMeshTransform_{1.0f};
    float lastFrameElapsedSeconds_ = 0.0f;

    static constexpr std::uint32_t kMaximumParticleSystems = 16;
};

} // namespace VulkCore
