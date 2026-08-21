#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "vulkcore/Renderer/Vertex.hpp"

namespace VulkCore {

struct EmbeddedTextureData {
    std::vector<std::uint8_t> data;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool compressed = false;

    bool empty() const {
        return data.empty() || width == 0 || height == 0;
    }
};

enum class AnimationBehavior : std::uint8_t {
    Default = 0,
    Constant,
    Linear,
    Repeat,
};

struct SkeletonBone {
    std::string name;
    // Assimp bones are resolved against the imported node hierarchy. A bone
    // can still be kept when its node is missing so the Inspector can report
    // incomplete source data instead of dropping it.
    std::uint32_t nodeId = std::numeric_limits<std::uint32_t>::max();
    std::int32_t parentIndex = -1;
    glm::mat4 offsetMatrix{1.0f};
    glm::mat4 bindPoseGlobalTransform{1.0f};
    std::uint32_t influenceCount = 0;
};

struct AnimationVectorKey {
    double timeTicks = 0.0;
    glm::vec3 value{0.0f};
};

struct AnimationRotationKey {
    double timeTicks = 0.0;
    // Quaternion components are stored as (x, y, z, w), matching Assimp.
    glm::vec4 value{0.0f, 0.0f, 0.0f, 1.0f};
};

struct SkeletalAnimationChannel {
    std::string nodeName;
    std::uint32_t nodeId = std::numeric_limits<std::uint32_t>::max();
    std::int32_t boneIndex = -1;
    AnimationBehavior preBehavior = AnimationBehavior::Default;
    AnimationBehavior postBehavior = AnimationBehavior::Default;
    std::vector<AnimationVectorKey> positionKeys;
    std::vector<AnimationRotationKey> rotationKeys;
    std::vector<AnimationVectorKey> scalingKeys;
};

struct SkeletalAnimationClip {
    std::string name;
    double durationTicks = 0.0;
    double ticksPerSecond = 0.0;
    std::uint32_t meshChannelCount = 0;
    std::vector<SkeletalAnimationChannel> channels;
};

// Runtime playback state for one imported model instance. The ranges point
// into MeshData::skeletonBones and MeshData::skeletalAnimations after scene
// instances have been appended into the renderer's flattened scene data.
struct SkeletalAnimationInstance {
    std::uint32_t rootNodeId = 0;
    std::uint32_t boneStart = 0;
    std::uint32_t boneCount = 0;
    std::uint32_t animationStart = 0;
    std::uint32_t animationCount = 0;
    glm::mat4 globalInverseTransform{1.0f};
    std::uint32_t activeClip = 0;
    float timeSeconds = 0.0f;
    float playbackSpeed = 1.0f;
    bool enabled = true;
    bool playing = true;
    bool looping = true;
};

// Shader selections belong to a Material. Empty entries mean "use the
// renderer default for this stage". Compute shaders are kept alongside the
// graphics stages so a Material can own a complete shader configuration even
// though the current mesh pass only consumes graphics stages.
enum class ShaderStage : std::size_t {
    Vertex = 0,
    TessellationControl,
    TessellationEvaluation,
    Geometry,
    Fragment,
    Compute,
};

inline constexpr std::size_t kShaderStageCount = 6;

struct ShaderConfiguration {
    std::array<std::string, kShaderStageCount> shaderNames{};

    std::string& shader(ShaderStage stage) {
        return shaderNames[static_cast<std::size_t>(stage)];
    }

    const std::string& shader(ShaderStage stage) const {
        return shaderNames[static_cast<std::size_t>(stage)];
    }

    bool hasGraphicsOverride() const {
        return !shader(ShaderStage::Vertex).empty() ||
            !shader(ShaderStage::TessellationControl).empty() ||
            !shader(ShaderStage::TessellationEvaluation).empty() ||
            !shader(ShaderStage::Geometry).empty() ||
            !shader(ShaderStage::Fragment).empty();
    }

    bool hasComputeOverride() const {
        return !shader(ShaderStage::Compute).empty();
    }

    bool empty() const {
        return !hasGraphicsOverride() && !hasComputeOverride();
    }
};

struct WaterShaderSettings {
    glm::vec2 flowDirection{0.0f, 1.0f};
    float flowSpeed = 0.12f;
    float flowStrength = 1.0f;
    float flowTurbulence = 0.35f;
    float waveScale = 1.0f;
    float waveHeight = 0.025f;
};

struct MaterialData {
    std::string name = "Material";
    glm::vec4 baseColor{1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float opacity = 1.0f;
    glm::vec3 emissionColor{0.0f};
    float emissionStrength = 0.0f;
    enum class BlendMode : std::uint8_t {
        Opaque = 0,
        AlphaBlend,
        Additive,
        Multiply,
    };
    BlendMode blendMode = BlendMode::Opaque;
    bool depthWrite = true;
    bool doubleSided = false;
    bool userCreated = false;
    bool userModified = false;
    ShaderConfiguration shaderConfiguration;
    WaterShaderSettings waterShaderSettings;
    std::filesystem::path texturePath;
    EmbeddedTextureData embeddedTexture;
    std::filesystem::path normalTexturePath;
    EmbeddedTextureData embeddedNormalTexture;
    std::filesystem::path specularTexturePath;
    EmbeddedTextureData embeddedSpecularTexture;
};

struct MeshPart {
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t materialIndex = 0;
    std::uint32_t nodeId = 0;
};

enum class SceneObjectType : std::uint8_t {
    ImportedMesh = 0,
    ParticleSystem,
    Player,
};

// The node hierarchy is kept separately from the flattened render mesh. This
// lets the renderer draw the pre-transformed mesh while the UI still exposes
// the original model structure.
struct ModelNode {
    std::string name;
    std::uint32_t nodeId = 0;
    SceneObjectType objectType = SceneObjectType::ImportedMesh;
    std::uint32_t meshCount = 0;
    glm::mat4 localTransform{1.0f};
    glm::mat4 globalTransform{1.0f};
    // Indices into MeshData::parts for this node's directly attached meshes.
    std::vector<std::uint32_t> meshPartIndices;
    std::vector<ModelNode> children;
    bool hasBounds = false;
    glm::vec3 boundsMinimum{0.0f};
    glm::vec3 boundsMaximum{0.0f};
    // Editable transform owned by this node. Position is expressed in the
    // renderer's normalized scene units; rotation and scale use this node's
    // local model axes and pivot.
    glm::vec3 transformTranslation{0.0f};
    glm::vec3 transformRotationDegrees{0.0f};
    float transformScaleFactor = 1.0f;
    // Disabling a node also hides all of its descendants from the renderer.
    bool renderEnabled = true;
    bool rotationAnimationEnabled = false;
    glm::vec3 rotationAnimationInitialDegrees{0.0f};
    glm::vec3 rotationAnimationSpeedDegrees{0.0f};
    // -1 keeps the material assignments imported from the model. A valid
    // material index overrides the material for this node and its children.
    std::int32_t materialOverride = -1;
};

struct MeshData {
    std::vector<Vertex> vertices;
    // Immutable flattened bind-pose vertices used to restore disabled/paused
    // skinning without accumulating floating-point deformation errors.
    std::vector<Vertex> bindPoseVertices;
    // Skinned vertices must be evaluated from the mesh-local coordinates that
    // Assimp uses for aiBone::mOffsetMatrix. Static vertices may mirror the
    // flattened render vertex here.
    std::vector<Vertex> skinSourceVertices;
    std::vector<uint32_t> indices;
    std::vector<MaterialData> materials;
    std::vector<MeshPart> parts;
    std::vector<SkeletonBone> skeletonBones;
    std::size_t skinnedMeshCount = 0;
    std::size_t skinWeightCount = 0;
    std::vector<SkeletalAnimationClip> skeletalAnimations;
    std::vector<SkeletalAnimationInstance> skeletalAnimationInstances;
    ModelNode hierarchy;
    // Nodes that live alongside the imported root node, such as generated
    // scene primitives. They are intentionally not children of hierarchy so
    // the scene tree can present multiple top-level objects.
    std::vector<ModelNode> additionalNodes;
};

} // namespace VulkCore
