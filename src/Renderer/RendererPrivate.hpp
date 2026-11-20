#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <iosfwd>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "vulkcore/Core/Config.hpp"
#include "vulkcore/Renderer/Mesh.hpp"
#include "vulkcore/Renderer/ParticleSystem.hpp"

namespace VulkCore {

namespace renderer_detail {

constexpr std::uint32_t kMaterialImageBindingCount = 9;
constexpr std::uint32_t kGrassGroundBinding = 9;
constexpr std::uint32_t kGrassGroundTextureFlag = 64u;
constexpr std::uint32_t kAlphaMaskBinding = 8;
constexpr std::uint32_t kAlphaTestFlag = 64u;

struct MaterialPushConstants {
    glm::mat4 nodeTransform{1.0f};
    glm::vec4 baseColor;
    std::uint32_t textureFlags = 0;
    float metallic = 0.0f;
    float roughness = 0.5f;
    float opacity = 1.0f;
    glm::vec4 waterFlow{};
    glm::vec4 waterSurface{};
};

// The material pipeline layout is shared by graphics, particle, post-process,
// and optional compute pipelines. Vulkan requires vkCmdPushConstants' stage
// mask to include every stage in the overlapping pipeline-layout range.
constexpr VkShaderStageFlags kMaterialPushConstantStageFlags =
    VK_SHADER_STAGE_VERTEX_BIT |
    VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
    VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT |
    VK_SHADER_STAGE_GEOMETRY_BIT |
    VK_SHADER_STAGE_FRAGMENT_BIT |
    VK_SHADER_STAGE_COMPUTE_BIT;

constexpr float kMinimumModelScaleFactor = 0.1f;
constexpr float kMaximumModelScaleFactor = 3.0f;
// Reserve a stable scene-object ID for the optional procedural Sun. Keeping
// it independent from imported and particle node allocation makes scene
// files portable across model switches.
constexpr std::uint32_t kSunSceneNodeId = 0xF0000000u;
constexpr std::uint32_t kTerrainSceneNodeId = 0xE0000000u;
constexpr std::uint32_t kTerrainChunkNodeBase = 0xE1000000u;
constexpr std::uint32_t kWaterSceneNodeId = 0xE3000000u;
constexpr std::uint32_t kWaterChunkNodeBase = 0xE4000000u;
constexpr std::uint32_t kParticleSceneNodeBase = 0xD0000000u;
constexpr std::uint32_t kPlayerTornadoMeshNodeId = 0xDFFFFFF3u;
constexpr std::uint32_t kSceneModelNodeStride = 1'000'000u;
constexpr char kDefaultVertexShader[] = "shader_base.vert";
constexpr char kDefaultFragmentShader[] = "shader_base.frag";

struct PostProcessDemo {
    const char* name;
    const char* description;
    float defaultStrength;
    float defaultParameter;
};

// These modes mirror the screen-space chapters in 3D Game Shaders For
// Beginners. They intentionally share one full-screen pipeline so switching
// demos is immediate and does not rebuild Vulkan objects at run time.
constexpr std::array<PostProcessDemo, 23> kPostProcessDemos = {{
    {"Render to Texture (original)", "Displays the off-screen color target without modification.", 1.0f, 0.5f},
    {"Fog", "Reconstructs view depth and blends distant pixels into atmospheric color.", 0.75f, 0.45f},
    {"Box Blur", "A square neighborhood blur with an adjustable sampling radius.", 1.0f, 0.35f},
    {"Median Filter", "Rejects isolated color noise using a compact median-like filter.", 1.0f, 0.45f},
    {"Kuwahara Painterly", "Chooses the lowest-variance quadrant to preserve painted edges.", 1.0f, 0.55f},
    {"Bloom", "Extracts bright pixels and adds a soft glow around them.", 0.9f, 0.55f},
    {"SSAO", "Estimates ambient occlusion from neighboring depth samples.", 0.8f, 0.55f},
    {"Motion Blur", "Accumulates samples along an animated screen-space velocity.", 0.75f, 0.45f},
    {"Chromatic Aberration", "Offsets red and blue channels radially from the image center.", 0.8f, 0.45f},
    {"Screen-space Reflection", "Reflects nearby screen color using a depth-aware vertical trace.", 0.75f, 0.5f},
    {"Screen-space Refraction", "Distorts the scene with an animated refractive field.", 0.65f, 0.45f},
    {"Outlining", "Detects color and depth discontinuities to draw contours.", 0.9f, 0.45f},
    {"Depth of Field", "Blurs pixels away from the selected focus depth.", 1.0f, 0.25f},
    {"Posterization", "Quantizes the color range into a small number of graphic bands.", 1.0f, 0.45f},
    {"Pixelization", "Samples the scene on an adjustable coarse pixel grid.", 1.0f, 0.45f},
    {"Sharpen", "Boosts local contrast with a five-tap sharpening kernel.", 0.85f, 0.45f},
    {"Dilation", "Expands the brightest neighboring colors over a configurable radius.", 1.0f, 0.45f},
    {"Film Grain", "Adds animated luminance and color grain.", 0.65f, 0.5f},
    {"Lookup Table (LUT)", "Demonstrates a warm/cool cinematic color grade without external assets.", 0.85f, 0.5f},
    {"Gamma Correction", "Applies an adjustable display gamma transfer function.", 1.0f, 0.45f},
    {"Deferred Depth Buffer", "Visualizes the sampled depth attachment used by later passes.", 1.0f, 0.5f},
    {"Deferred Edge Buffer", "Visualizes depth-derived geometry edges as a G-buffer diagnostic.", 1.0f, 0.5f},
    {"Distant Blur", "Gradually blurs geometry as its reconstructed view distance increases.", 0.9f, 0.45f},
}};

struct SurfaceShaderDemo {
    const char* name;
    const char* vertexShader;
    const char* fragmentShader;
    const char* description;
};

constexpr std::array<SurfaceShaderDemo, 13> kSurfaceShaderDemos = {{
    {"Texturing + Blinn-Phong", "", "", "Default textured lighting with normal and specular maps."},
    {"Unlit Texturing", "", "shader_unlit.frag", "Shows base-color texture sampling without scene lighting."},
    {"Diffuse Lighting", "", "shader_diffuse.frag", "Lambert diffuse lighting plus ambient light."},
    {"Fresnel Factor", "", "shader_fresnel.frag", "Highlights grazing angles using a Fresnel approximation."},
    {"Rim Lighting", "", "shader_rim.frag", "Adds a controllable silhouette-facing rim light."},
    {"Cel Shading", "", "shader_cel.frag", "Quantizes diffuse and specular lighting into discrete bands."},
    {"Normal Mapping", "", "shader_base.frag", "Uses tangent-space normal maps with Blinn-Phong lighting."},
    {"Normal Buffer", "", "shader_normal_debug.frag", "Visualizes the final geometric or mapped world normal."},
    {"Position Buffer", "", "shader_position_debug.frag", "Visualizes world position as a deferred-rendering buffer."},
    {"Specular Only", "", "shader_specular_debug.frag", "Displays only the Blinn-Phong specular response."},
    {"Water Reflection / Refraction", "shader_water.vert", "shader_water.frag", "Animated waves with Fresnel reflection and refraction."},
    {"Water Flow Mapping", "shader_water.vert", "shader_water_flow.frag", "Moves UVs through a procedural flow field with seamless phase blending."},
    {"Water Foam", "shader_water.vert", "shader_water_foam.frag", "Flow-mapped water with depth-aware shoreline foam."},
}};
constexpr std::array<std::uint32_t, 24> kBoundingBoxEdges = {
    0, 1, 1, 2, 2, 3, 3, 0,
    4, 5, 5, 6, 6, 7, 7, 4,
    0, 4, 1, 5, 2, 6, 3, 7
};



const char* shaderStageLabel(ShaderStage stage);
const char* shaderStageExtension(ShaderStage stage);
VkShaderStageFlagBits vkShaderStage(ShaderStage stage);
bool shaderStageForExtension(const std::string& extension, ShaderStage& stage);
const char* defaultShaderForStage(ShaderStage stage);
bool isModelAssetPath(const std::filesystem::path& path);
bool isSceneResourcePath(const std::filesystem::path& path);
bool isTextureAssetPath(const std::filesystem::path& path);
std::filesystem::path normalizedPath(const std::filesystem::path& path);
bool pathsEqual(const std::filesystem::path& left, const std::filesystem::path& right);
std::filesystem::path locateAssetsDirectory();
bool isWaterShaderConfiguration(const ShaderConfiguration& configuration);
bool isTransparentMaterial(const MaterialData& material);
const char* materialBlendModeLabel(MaterialData::BlendMode mode);
bool findInheritedMaterialOverride(
    const ModelNode& node,
    std::uint32_t targetNodeId,
    std::int32_t inherited,
    std::int32_t& result
);
bool findInheritedRenderEnabled(
    const ModelNode& node,
    std::uint32_t targetNodeId,
    bool inherited,
    bool& result
);
ModelNode* findModelNodeById(ModelNode& node, std::uint32_t nodeId);
ModelNode* findSceneNodeById(MeshData& meshData, std::uint32_t nodeId);
void collectModelNodeIds(const ModelNode& node, std::vector<std::uint32_t>& nodeIds);
bool removeModelNodeFromChildren(
    ModelNode& parent,
    std::uint32_t nodeId,
    std::vector<std::uint32_t>& removedNodeIds
);
bool hasNodeAnimationSettings(const ModelNode& node);
bool hasNodeTransformSettings(const ModelNode& node);
void saveNodeTransformSettings(std::ostream& file, const ModelNode& node);
void saveNodeRenderSettings(std::ostream& file, const ModelNode& node);
void saveNodeAnimationSettings(std::ostream& file, const ModelNode& node);
void saveParticleSystemSettings(
    std::ostream& file,
    std::uint32_t nodeId,
    const ParticleSystemSettings& settings
);
std::string shaderConfigToken(const std::string& token);
std::size_t countModelNodes(const ModelNode& node);
std::size_t countMeshNodes(const ModelNode& node);

using MaterialIndexRemap = std::unordered_map<std::size_t, std::size_t>;
void saveNodeMaterialSettings(
    std::ostream& file,
    const ModelNode& node,
    const MaterialIndexRemap& materialIndexRemap
);
void saveUserMaterials(
    std::ostream& file,
    const std::vector<MaterialData>& materials,
    const MaterialIndexRemap& materialIndexRemap
);

} // namespace renderer_detail

} // namespace VulkCore
