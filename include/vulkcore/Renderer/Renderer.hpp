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
#include <string_view>
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
#include "vulkcore/Terrain/Grass.hpp"
#include "vulkcore/Terrain/RoadPaint.hpp"
#include "vulkcore/Utils/SbfresLoader.hpp"

#include "vulkcore/Platform/Window.hpp"


namespace VulkCore {

class Buffer;
class PhysicsWorld;
class SwapChain;
class VulkanContext;
class TerrainScene;
class HdrComposition;
class RealtimeEffect;
class SceneLoadTask;
struct TerrainMeshData;
enum class EditorFileType;
#ifdef VULKCORE_HAS_EFFECT_EDITOR
class EffectEditorView;
#endif
struct UniformBufferObject;

class Renderer {
public:
    enum class Mode { Scene, ModelViewer };

    explicit Renderer(
        Window* window,
        const std::filesystem::path& startupScene = {},
        bool hideTools = false,
        const std::function<void()>& onSceneLoading = {},
        Mode mode = Mode::Scene
    );
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Returns true once the presentation queue accepts the frame.
    bool drawFrame();
    void setFullscreen(bool enabled) {
        fullscreen_ = enabled;
        if (enabled) sceneEditMode_ = false;
    }
    bool hasPendingSceneClose() const { return pendingSceneChange_ == EditorSceneChange::Close; }
    void setTerrainFrustumCullingEnabled(bool enabled) { terrainFrustumCullingEnabled_ = enabled; }
    void initializeResourceBrowser(const std::filesystem::path& root);
    void queueEditorDrop(std::vector<std::filesystem::path> paths, double x, double y);
    void queueSceneOpen(const std::filesystem::path& path);
    void queueStartupScene(const std::filesystem::path& path);
    void queueModelViewerDrop(std::vector<std::filesystem::path> paths);
    void captureTransitionFrame(float progress, float time, const std::filesystem::path& output);

private:
    bool toolUiVisible() const { return !fullscreen_ && (toolUiVisible_ || hasPendingSceneClose()); }

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

    struct ResourceBrowser {
        struct Entry {
            std::filesystem::path path;
            bool directory = false;
        };
        struct DropZone {
            std::filesystem::path directory;
            float left, top, right, bottom;
            bool contains(double x, double y) const {
                return x >= left && x < right && y >= top && y < bottom;
            }
        };
        struct Transfer {
            std::filesystem::path source, directory;
            bool move = false;
        };
        std::filesystem::path root, directory, selected, dragged;
        std::filesystem::path pendingModelImport, pendingTrash;
        std::vector<Entry> entries;
        std::vector<DropZone> dropZones;
        std::vector<Transfer> transfers;
        std::chrono::steady_clock::time_point nextScan{};
        std::array<char, 256> folderName{};
        std::string status;
        bool open = true;
    };
    struct ResourcePreview {
        MeshData mesh;
        std::unique_ptr<Buffer> indices;
        std::unique_ptr<Texture> fallback;
        std::array<std::unique_ptr<Buffer>, kMaxFramesInFlight> vertices, uniforms;
        std::vector<std::array<std::unique_ptr<Texture>, 4>> textures;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> descriptorSets;
        std::array<SceneRenderTarget, kMaxFramesInFlight> targets;
        std::array<VkDescriptorSet, kMaxFramesInFlight> images{};
        std::filesystem::path model, animation, requestedModel;
        std::vector<std::filesystem::path> modelChoices;
        std::size_t modelIndex = 0;
        glm::vec3 camera{0.0f, 0.0f, 4.0f};
        std::string message;
        bool reload = false;
        bool draw = false;
        bool targetsFailed = false;
    };

    struct SceneModelResource {
        std::uint32_t rootNodeId = 0;
        std::filesystem::path path;
        std::uint32_t materialStart = 0;
        std::uint32_t materialCount = 0;
        bool playerModel = false;
    };

    struct EnemyPatrol {
        bool enabled = true;
        glm::vec2 offset{0.0f, -6.0f}; // X/Z metres from the authored position.
        float speed = 1.4f;
        float wait = 0.8f;
        float facingOffsetDegrees = 0.0f;
        std::string walkClip, idleClip; // Empty selects an imported default.
        float distance = 0.0f;
        float waitRemaining = 0.0f;
        float animationTime = 0.0f;
        bool returning = false;
        bool walking = false;
    };

    struct ModelResourceCacheEntry {
        std::shared_ptr<MeshData> mesh;
        std::vector<std::filesystem::path> texturePaths;
        std::string error;
    };

    void initializeImGui();
    void shutdownImGui();
    bool buildImGuiFrame();
    void buildSceneLoadingFrame();
    bool drawModelViewerUi();
    void loadModelViewerDrop();
    void selectViewerModel(std::size_t index);
    void selectViewerShape(std::optional<std::size_t> shape);
    void fitViewerModel();
    void drawViewerPoints(const glm::mat4& clipFromScene);
    void drawViewerTextures();
    void updateViewerTexture();
    void destroyViewerTexture();
    bool drawGameEditor();
    void queueSceneClose();
    void drawRoadBrushControls();
    void updateRoadEditorCamera(float deltaSeconds, bool viewportHovered);
    void updateRoadBrush(const glm::mat4& clipFromScene, bool viewportHovered);
    glm::vec3 updateGameEditorSelection(float deltaSeconds, bool viewportHovered);
    void drawGameSelectionOutline(const glm::mat4& clipFromScene);
    bool drawMiniMap(const glm::vec2& viewportOrigin, const glm::vec2& viewportSize);
    bool canIssuePlayerMove() const;
    void issuePlayerMove(const glm::vec3& destination);
    void updatePlayerMoveClick(const glm::mat4& clipFromScene, bool viewportHovered);
    void appendPlayerMoveMarker(float deltaSeconds);
    void drawResourceBrowser();
    void scanResourceDirectory();
    void processResourceTransfers();
    void processResourceTrash();
    bool editorDeletePressed() const;
    void acceptSceneModelDrop();
    void processSceneModelDrop();
    void drawResourcePreview();
    void importRealtimeEffect(const std::filesystem::path& path);
    void frameRealtimeEffect(const ModelNode& node);
    void loadResourcePreview();
    void createResourcePreviewBuffers();
    void destroyResourcePreviewBuffers();
    void createResourcePreviewTargets();
    void destroyResourcePreviewTargets();
    void recordResourcePreview(VkCommandBuffer commandBuffer);
    void drawGameInspector();
    void changeEditorMaterialTexture(std::uint32_t index, const std::filesystem::path& path);
    void drawGameOutliner();
    void openEditorFilePicker(const char* title, EditorFileType type, const std::filesystem::path& directory,
                              std::function<void(const std::filesystem::path&)> accept);
    void drawEditorFilePicker();
    void drawEditorTexture(const char* label, const std::filesystem::path& path,
                           const std::filesystem::path& defaultPath,
                           std::function<void(const std::filesystem::path&)> accept);
    void drawEditorMaterial(std::uint32_t index, std::string_view family);
    void drawEditorShader(ShaderStage stage, const std::string& name, std::string_view family,
                          std::optional<std::uint32_t> material = std::nullopt);
    void drawEditorShaderDialog();
    void applyEditorShader();
    std::filesystem::path defaultSkyboxPath() const;
    void applySkyboxTexture(const std::filesystem::path& path);
    struct TerrainGrassImages {
        std::array<EmbeddedTextureData, 5> images;
        GrassCoverMask coverMask;
    };
    static std::optional<TerrainGrassImages> decodeTerrainGrass(const std::filesystem::path& terrainRoot);
    SceneLoadTask uploadTerrainGrass(TerrainGrassImages images);
    void loadResourceSettings();
    void loadTransformSettings(
        std::size_t firstUnmappedMaterial = std::numeric_limits<std::size_t>::max()
    );
    bool saveTransformSettings() const;
    void initializeEmptyScene();
    void appendTerrainToScene(
        const TerrainMeshData& terrainMesh,
        const std::filesystem::path& terrainRoot
    );
    void appendWaterToScene(const TerrainMeshData& waterMesh);
    void appendPlayerToScene();
    void appendGrassToScene(const TerrainScene& terrain);
    void updateGrassCoverage();
    void updatePlayerController(float deltaSeconds, bool viewportHovered);
    void updatePlayerCamera();
    void clearSceneData(bool rebuildResources = true);
    const SceneModelResource* sceneModelResourceForNode(const ModelNode& node) const;
    void recalculateSceneBounds();
    void appendImportedModel(
        MeshData importedMesh,
        const std::filesystem::path& path,
        std::optional<std::uint32_t> preferredRootNodeId,
        bool playerModel,
        bool updateBounds
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
    bool createNewEmptyScene();
    std::filesystem::path createSceneFile();
    void discoverSceneResources();
    bool saveSceneResource(const std::filesystem::path& path);
    // Positions use the same scene-world units as ordinary particle objects.
    bool playRealtimeEffect(std::uint32_t nodeId, std::optional<glm::vec3> worldPosition = std::nullopt);
    bool stopRealtimeEffect(std::uint32_t nodeId);
    bool loadSceneResource(const std::filesystem::path& path);
    SceneLoadTask loadSceneSteps(std::filesystem::path path, bool incremental);
    void advanceSceneLoad();
    void createBoundingBoxPipeline();
    void createParticlePipelines();
    void createPostProcessPipeline();
    VkPipeline createSkyboxPipeline(VkRenderPass pass);
    VkPipeline createParticlePipeline(bool additiveBlending, VkRenderPass pass);
    void configureHdrComposition();
    void updateTransition(float seconds);
    void updateCameraOrbit(bool viewportHovered);
    glm::vec3 realtimeEffectSceneUp() const;
    void restoreSceneCamera();
    void createBoundingBoxBuffers();
    void updateBoundingBoxVertices();
    void updateBoundingBoxBuffer();
    void createParticleBuffer();
    void updateParticleBuffer();
    void addRealtimeEffect(std::uint32_t id, const std::filesystem::path& path);
    void loadRealtimeEffect(std::uint32_t id, const std::filesystem::path& path, bool autoPlay, bool loop);
    bool realtimeNeedsOpaqueSnapshot() const;
    bool hasActiveRealtimePost(bool afterBloom) const;
    void updateRealtimeEffects(float dt);
    void prepareRealtimeEffects(UniformBufferObject& scene);
    void drawRealtimeEffects(VkCommandBuffer commands, bool hdr);
    unsigned drawRealtimePost(VkCommandBuffer commands, unsigned source, bool afterBloom);
    bool hasRealtimePostEffects() const;
    HdrSettings realtimeHdrSettings(bool includeInactive) const;
    void invalidateRealtimeEffectPipelines();
    bool drawRealtimeEffectControls(ModelNode& node);
    void saveRealtimeEffects(std::ostream& file,
        const std::function<std::string(const std::filesystem::path&)>& reference) const;
    std::unordered_map<std::uint32_t, std::shared_ptr<RealtimeEffect>> realtimeEffects_;
    void createParticleTextures();
    void loadParticleTexture(std::uint32_t nodeId);
    void createParticleDescriptorSets(std::uint32_t nodeId);
    void updateParticleTextureDescriptors(std::uint32_t nodeId);
    void updateParticleSystems(
        float deltaSeconds,
        const glm::vec3& cameraRight,
        const glm::vec3& cameraUp
    );
    void createTornadoPreview();
    void updatePlayerSkillEffects();
    bool removeSceneNodeById(std::uint32_t nodeId);
    void selectModelNode(ModelNode& node);
    void updateSceneNodeTransforms(float elapsedSeconds, float deltaSeconds, const glm::vec3& editMovement);
    bool translateSceneNode(ModelNode& node, const glm::mat4& parentTransform, const glm::vec3& movement);
    bool updateEnemyPatrol(const ModelNode& node, glm::mat4& transform, bool visible, float deltaSeconds);
    void updateModelGravity(ModelNode& node, const glm::mat4& parentTransform, glm::mat4& transform,
        bool visible, bool patrolControlled);
    void drawEnemyPatrol(const ModelNode& modelRoot);
    SkeletalAnimationInstance* sceneAnimation(std::uint32_t rootNodeId);
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
    void createSunShadowResources();
    void destroySunShadowResources();
    void updateSunShadowUniforms(UniformBufferObject& frame);
    VkDescriptorImageInfo sunShadowImageInfo(std::uint32_t frame) const;
    VkPipeline sunShadowPipelineForMaterial(const MaterialData& material);
    void createSceneRenderTarget(SceneRenderTarget& target, VkExtent2D extent, bool sampleDepth);
    void destroySceneRenderTarget(SceneRenderTarget& target);
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
    SceneLoadTask createVertexBufferSteps();
    void createIndexBuffer();
    void createMaterialTextures();
    SceneLoadTask createMaterialTextureSteps(bool incremental);
    void createSkyboxTexture();
    void clearMaterialResources();
    void rebuildMaterialResources();
    void createUniformBuffers();
    void createDescriptorPool();
    void createDescriptorSets();
    void bindRoadIndex(std::uint32_t frame);
    void updateRoadIndex();
    bool roadEditorActive() const;
    void updateSceneTextureDescriptors();
    void createCommandBuffers();
    void createSyncObjects();
    void recreatePresentSemaphores();
    void updateCameraBasis();
    std::optional<glm::mat4> updateUniformBuffer();
    void drawMaterialControls(
        MaterialData& material,
        std::int32_t materialKey,
        bool& shouldSaveSettings,
        const std::filesystem::path* modelResourcePath = nullptr
    );
    bool importModelResource(
        const std::filesystem::path& path,
        std::optional<std::uint32_t> preferredRootNodeId = std::nullopt,
        bool playerModel = false,
        bool rebuildResources = true
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
    SceneLoadTask prepareScenePipelines(bool incremental);
    VkPipeline computePipelineForMaterial(const MaterialData& material);
    std::string graphicsPipelineKey(
        const ShaderConfiguration& configuration,
        const MaterialData& material, bool hdr, VkCullModeFlags cullMode
    ) const;
    std::uint32_t materialIndexForPart(const MeshPart& part) const;
    std::int32_t inheritedMaterialOverrideForSceneNode(std::uint32_t nodeId) const;
    void drawSkeletalAnimationControls(ModelNode& node, bool& shouldSaveSettings);
    void drawParticleSystemControls(std::uint32_t nodeId, bool& shouldSaveSettings, ModelNode* node = nullptr);
    void recordLegacySceneTransfer(VkCommandBuffer commandBuffer, uint32_t imageIndex);
    void recordCommandBuffer(
        VkCommandBuffer commandBuffer,
        uint32_t imageIndex,
        bool captureScreenshot,
        const std::optional<glm::mat4>& clipFromScene
    );
    void ensureScreenshotBuffer();
    void recordScreenshotCopy(VkCommandBuffer commandBuffer, uint32_t imageIndex);
    void saveScreenshot(const std::filesystem::path& output);
    void recreateSwapChain();

    VkShaderModule createShaderModule(const std::vector<char>& code) const;

    static void framebufferResizeCallback(Window* window, int width, int height);

    Window* window_ = nullptr;
    Mode mode_ = Mode::Scene;
    std::vector<std::filesystem::path> viewerDropPaths_;
    std::string viewerFilename_;
    std::string viewerAnimationFilename_;
    std::string viewerError_;
    std::size_t viewerModelIndex_ = 0;
    std::optional<std::size_t> viewerShapeIndex_;
    bool viewerHasSurface_ = false;
    struct ViewerPoint {
        std::uint32_t nodeId;
        std::uint32_t vertexIndex;
    };
    std::vector<ViewerPoint> viewerPoints_;
    bool viewerShowingTextures_ = false;
    struct ViewerTextures {
        SbfresTextureArchive archive;
        std::string filename;
        std::string error;
        std::size_t index = 0;
        std::uint32_t layer = 0;
        bool showAlpha = false;
        bool reload = false;
        std::unique_ptr<Texture> image;
        VkDescriptorSet descriptor = VK_NULL_HANDLE;
    } viewerTextures_;
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
    DistanceFogSettings distanceFogSettings_;
    AuthoredView authoredView_;
    bool cameraOrbitDragging_ = false;
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
    static constexpr std::size_t kSunShadowCascadeCount = 3;
    struct SunShadowTarget {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
    };
    std::array<SunShadowTarget, kMaxFramesInFlight> sunShadowTargets_;
    std::array<glm::mat4, kSunShadowCascadeCount> sunShadowClipFromWorld_{};
    VkRenderPass sunShadowRenderPass_ = VK_NULL_HANDLE;
    VkSampler sunShadowSampler_ = VK_NULL_HANDLE;
    VkPipeline sunShadowParticlePipeline_ = VK_NULL_HANDLE;
    std::uint32_t sunShadowTileSize_ = 0;
    std::uint32_t uniformSliceStride_ = 0;
    bool sunShadowsActive_ = false;
    MeshData meshData_;
    std::unique_ptr<TerrainScene> terrainScene_;
    RoadPaint roadPaint_;
    static constexpr float kDefaultOchreDirtRatio = 0.80f;
    float ochreDirtRatio_ = kDefaultOchreDirtRatio;
    std::uint64_t roadIndexRevision_ = 0;
    std::vector<std::uint32_t> roadIndex_;
    struct RoadFrameBuffer {
        std::unique_ptr<Buffer> buffer;
        std::uint64_t revision = 0;
    };
    std::array<RoadFrameBuffer, kMaxFramesInFlight> roadFrameBuffers_;
    GrassCoverage grassCoverage_;
    std::uint64_t grassRevision_ = 0;
    struct GrassFrameBuffer {
        std::unique_ptr<Buffer> instances;
        std::uint64_t uploadedRevision = 0;
    };
    std::array<GrassFrameBuffer, kMaxFramesInFlight> grassFrameBuffers_;
    std::unique_ptr<Buffer> grassIndexBuffer_;
    static constexpr std::uint32_t kGrassSceneNodeId = 0xE5000000u;
    static constexpr std::uint32_t kGrassLodNodeBase = 0xE5100000u;
    bool terrainFrustumCullingEnabled_ = true;
    std::size_t terrainDrawCandidates_ = 0;
    std::size_t terrainDrawCulled_ = 0;
    std::unique_ptr<PhysicsWorld> physicsWorld_;
    // Presence enables gravity. The matrix tracks the last synchronized model transform.
    std::unordered_map<std::uint32_t, std::optional<glm::mat4>> modelGravity_;
    std::unordered_map<std::uint32_t, EnemyPatrol> enemyPatrols_;
    std::filesystem::path terrainResourcePath_;
    std::filesystem::path waterResourcePath_;
    PlayerController playerController_;
    bool playerClickToMove_ = false;
    struct PlayerMoveMarker {
        glm::vec3 position;
        float age = 0.0f;
    };
    std::optional<PlayerMoveMarker> playerMoveMarker_;
    static constexpr std::size_t kMoveMarkerRingSegments = 24;
    static constexpr std::size_t kMoveMarkerArrowCount = 4;
    static constexpr std::size_t kMoveMarkerVertexCount = 6 * (kMoveMarkerRingSegments + kMoveMarkerArrowCount);
    std::optional<glm::vec3> playerSpawnPosition_;
    std::filesystem::path playerModelResourcePath_;
    float playerModelAutoScale_ = 1.0f;
    float playerModelGroundOffset_ = 0.0f;
    std::optional<std::uint32_t> playerRunAnimationClipIndex_;
    std::array<std::optional<std::uint32_t>, kPlayerSkillCount> playerSkillAnimationClips_;
    struct PlayerSkillEffect {
        std::uint32_t nodeId = 0;
        float forwardOffset = 0.0f;
        float heightOffset = 0.0f;
        bool followPlayer = false;
    };
    std::array<std::optional<PlayerSkillEffect>, kPlayerSkillCount> playerSkillEffects_;
    static constexpr std::uint32_t kPlayerSceneNodeId = 0xE2000000u;
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
        std::optional<std::uint32_t> nodeId; // No node for the transient ground marker.
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
    std::vector<std::unique_ptr<Texture>> alphaMaskTextures_;
    std::unique_ptr<Texture> terrainAlbedoLayers_;
    std::unique_ptr<Texture> terrainSurfaceLayers_;
    std::unique_ptr<Texture> fallbackTerrainLayers_;
    std::unordered_map<std::uint32_t, std::unique_ptr<Texture>> particleTextures_;
    std::unique_ptr<Texture> skyboxTexture_;
    struct TerrainGrassResources {
        std::unique_ptr<Texture> groundAlbedo;
        std::unique_ptr<Texture> groundCombined;
        std::unique_ptr<Texture> bladeAlbedo;
        std::unique_ptr<Texture> crossAlbedo;
        std::unique_ptr<Texture> specular;
    } terrainGrass_;
    std::filesystem::path skyboxTexturePath_;
    bool skyboxEnabled_ = false;
    float grassHeightScale_ = 1.0f;
    GrassGrowthSettings grassGrowth_;
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
    bool fullscreen_ = false;
    bool sceneEditMode_ = false;
    bool miniMapVisible_ = true;
    bool screenshotFromToolbar_ = false;
#ifdef VULKCORE_HAS_EFFECT_EDITOR
    bool effectEditorSelected_ = false;
    bool selectSceneWorkspace_ = false;
    std::unique_ptr<EffectEditorView> effectEditorView_;
#endif
#ifndef VULKCORE_OHOS
    struct RoadEditorCamera {
        glm::vec3 originalPosition;
        glm::vec3 originalTarget;
        float yaw;
        float pitch;
        float distance;
        bool dragging = false;
    };
    std::optional<RoadEditorCamera> roadEditorCamera_;
    bool roadBrushEnabled_ = true;
    bool roadTopDown_ = false;
    bool roadBrushDragging_ = false;
    bool roadViewportOwnsInput_ = false;
    bool roadStrokeHasPoint_ = false;
    RoadSurface roadBrushSurface_ = RoadSurface::Mud;
    float roadBrushRadius_ = RoadPaint::kDefaultRadius;
    glm::vec4 sceneEditorViewport_{0.0f}; // left, top, right, bottom in ImGui coordinates
    ResourceBrowser resourceBrowser_;
    ResourcePreview resourcePreview_;
    std::optional<std::uint32_t> pendingSceneNodeRemoval_;
#endif
    enum class EditorSceneChange { None, Open, Create, Close };
    EditorSceneChange pendingSceneChange_ = EditorSceneChange::None;
    std::filesystem::path pendingSceneOpen_;
    std::unique_ptr<SceneLoadTask> sceneLoad_;
    std::filesystem::path loadingScenePath_;
    bool sceneLoadApplying_ = false;
    bool editorDirty_ = false;
    bool editorSceneChangeConfirmed_ = false;
    enum class EditorSelection { None, Node, Skybox, Environment, GrassGround, PlayerSettings };
    EditorSelection editorSelection_ = EditorSelection::None;
    std::optional<std::uint32_t> editorPart_;
    struct EditorFilePicker {
        std::string title;
        EditorFileType type{};
        std::filesystem::path directory;
        std::array<char, 2048> path{};
        std::function<void(const std::filesystem::path&)> accept;
    } editorFilePicker_;
    struct EditorShaderDialog {
        ShaderStage stage = ShaderStage::Vertex;
        std::string family;
        std::string chosen;
        std::optional<std::uint32_t> material;
        std::string source;
        std::string original;
        std::string status;
        std::vector<std::string> candidates;
    } editorShader_;
    bool editorShaderRequested_ = false;
    bool editorShaderApplyRequested_ = false;
    glm::vec3 cameraPosition_{0.0f, 0.0f, 4.0f};
    glm::vec3 cameraForward_{0.0f, 0.0f, -1.0f};
    glm::vec3 cameraRight_{1.0f, 0.0f, 0.0f};
    glm::vec3 cameraUp_{0.0f, 1.0f, 0.0f};
    bool cameraBasisInitialized_ = false;
    glm::vec3 cameraTarget_{0.0f};
    struct EffectCamera {
        glm::vec3 up{0.0f, 1.0f, 0.0f};
        float fov = 43.0f;
        float aspect = 16.0f / 9.0f;
    };
    std::optional<EffectCamera> effectCamera_;
    struct SceneCamera {
        glm::vec3 position;
        glm::vec3 target;
        std::optional<EffectCamera> framing;
    };
    std::optional<SceneCamera> previousSceneCamera_;
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
    std::string sceneStatus_;
    bool sceneStatusFailed_ = false;
    std::vector<std::string> sceneResourceWarnings_;
    ResourceSelectionType selectedResourceType_ = ResourceSelectionType::Model;
    ResourceTextureSlot selectedResourceTextureSlot_ = ResourceTextureSlot::BaseColor;
    std::int32_t selectedResourceMaterialIndex_ = -1;
    std::string resourceImportStatus_;
    bool resourceImportFailed_ = false;
    std::array<Vertex, 24> boundingBoxVertices_{};
    std::unordered_map<std::uint32_t, ParticleSystem> particleSystems_;
    std::array<char, 512> particleTexturePathBuffer_{};
    std::uint32_t particleTexturePathNodeId_ = std::numeric_limits<std::uint32_t>::max();
    glm::mat4 tornadoPreviewTransform_{1.0f};
    float lastFrameElapsedSeconds_ = 0.0f;

    static constexpr std::uint32_t kMaximumParticleSystems = 16;
};

} // namespace VulkCore
