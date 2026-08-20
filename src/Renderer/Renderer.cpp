#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "vulkcore/Renderer/Renderer.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <stdexcept>
#include <string_view>

#include "vulkcore/Core/Config.hpp"
#include "vulkcore/Renderer/Buffer.hpp"
#include "vulkcore/Renderer/SwapChain.hpp"
#include "vulkcore/Renderer/Texture.hpp"
#include "vulkcore/Renderer/UniformBuffer.hpp"
#include "vulkcore/Renderer/Vertex.hpp"
#include "vulkcore/Renderer/VulkanContext.hpp"
#include "vulkcore/Utils/FileUtils.hpp"
#include "vulkcore/Utils/ModelLoader.hpp"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace VulkCore {

namespace {

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

constexpr float kMinimumModelScaleFactor = 0.1f;
constexpr float kMaximumModelScaleFactor = 3.0f;
constexpr char kDefaultVertexShader[] = "shader_base.vert";
constexpr char kDefaultFragmentShader[] = "shader_base.frag";
constexpr std::array<std::uint32_t, 24> kBoundingBoxEdges = {
    0, 1, 1, 2, 2, 3, 3, 0,
    4, 5, 5, 6, 6, 7, 7, 4,
    0, 4, 1, 5, 2, 6, 3, 7
};

const char* shaderStageLabel(ShaderStage stage) {
    switch (stage) {
    case ShaderStage::Vertex:
        return "Vertex shader";
    case ShaderStage::TessellationControl:
        return "Tessellation control";
    case ShaderStage::TessellationEvaluation:
        return "Tessellation evaluation";
    case ShaderStage::Geometry:
        return "Geometry shader";
    case ShaderStage::Fragment:
        return "Fragment shader";
    case ShaderStage::Compute:
        return "Compute shader";
    }
    return "Shader";
}

const char* shaderStageExtension(ShaderStage stage) {
    switch (stage) {
    case ShaderStage::Vertex:
        return ".vert";
    case ShaderStage::TessellationControl:
        return ".tesc";
    case ShaderStage::TessellationEvaluation:
        return ".tese";
    case ShaderStage::Geometry:
        return ".geom";
    case ShaderStage::Fragment:
        return ".frag";
    case ShaderStage::Compute:
        return ".comp";
    }
    return "";
}

VkShaderStageFlagBits vkShaderStage(ShaderStage stage) {
    switch (stage) {
    case ShaderStage::Vertex:
        return VK_SHADER_STAGE_VERTEX_BIT;
    case ShaderStage::TessellationControl:
        return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    case ShaderStage::TessellationEvaluation:
        return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    case ShaderStage::Geometry:
        return VK_SHADER_STAGE_GEOMETRY_BIT;
    case ShaderStage::Fragment:
        return VK_SHADER_STAGE_FRAGMENT_BIT;
    case ShaderStage::Compute:
        return VK_SHADER_STAGE_COMPUTE_BIT;
    }
    return VK_SHADER_STAGE_VERTEX_BIT;
}

bool shaderStageForExtension(const std::string& extension, ShaderStage& stage) {
    for (std::size_t index = 0; index < kShaderStageCount; ++index) {
        const auto candidate = static_cast<ShaderStage>(index);
        if (extension == shaderStageExtension(candidate)) {
            stage = candidate;
            return true;
        }
    }
    return false;
}

const char* defaultShaderForStage(ShaderStage stage) {
    switch (stage) {
    case ShaderStage::Vertex:
        return kDefaultVertexShader;
    case ShaderStage::Fragment:
        return kDefaultFragmentShader;
    default:
        return "";
    }
}

bool isWaterShaderConfiguration(const ShaderConfiguration& configuration) {
    const std::string& fragmentShader = configuration.shader(ShaderStage::Fragment);
    return fragmentShader == "shader_water.frag" ||
        fragmentShader == "shader_water_flow.frag";
}

bool isTransparentMaterial(const MaterialData& material) {
    return material.blendMode != MaterialData::BlendMode::Opaque;
}

const char* materialBlendModeLabel(MaterialData::BlendMode mode) {
    switch (mode) {
    case MaterialData::BlendMode::Opaque:
        return "Opaque";
    case MaterialData::BlendMode::AlphaBlend:
        return "Alpha blend";
    case MaterialData::BlendMode::Additive:
        return "Additive";
    case MaterialData::BlendMode::Multiply:
        return "Multiply";
    }
    return "Opaque";
}

bool findInheritedMaterialOverride(
    const ModelNode& node,
    std::uint32_t targetNodeId,
    std::int32_t inherited,
    std::int32_t& result
) {
    if (node.materialOverride >= 0) {
        inherited = node.materialOverride;
    }
    if (node.nodeId == targetNodeId) {
        result = inherited;
        return true;
    }
    for (const ModelNode& child : node.children) {
        if (findInheritedMaterialOverride(child, targetNodeId, inherited, result)) {
            return true;
        }
    }
    return false;
}

ModelNode* findModelNodeById(ModelNode& node, std::uint32_t nodeId) {
    if (node.nodeId == nodeId) {
        return &node;
    }
    for (ModelNode& child : node.children) {
        if (ModelNode* result = findModelNodeById(child, nodeId); result != nullptr) {
            return result;
        }
    }
    return nullptr;
}

ModelNode* findSceneNodeById(MeshData& meshData, std::uint32_t nodeId) {
    if (ModelNode* result = findModelNodeById(meshData.hierarchy, nodeId); result != nullptr) {
        return result;
    }
    for (ModelNode& node : meshData.additionalNodes) {
        if (ModelNode* result = findModelNodeById(node, nodeId); result != nullptr) {
            return result;
        }
    }
    return nullptr;
}

std::uint32_t maximumModelNodeId(const ModelNode& node) {
    std::uint32_t maximum = node.nodeId;
    for (const ModelNode& child : node.children) {
        maximum = std::max(maximum, maximumModelNodeId(child));
    }
    return maximum;
}

bool hasNodeAnimationSettings(const ModelNode& node) {
    return node.rotationAnimationEnabled ||
        node.rotationAnimationInitialDegrees != glm::vec3(0.0f) ||
        node.rotationAnimationSpeedDegrees != glm::vec3(0.0f);
}

bool hasNodeTransformSettings(const ModelNode& node) {
    return node.transformTranslation != glm::vec3(0.0f) ||
        node.transformRotationDegrees != glm::vec3(0.0f) ||
        node.transformScaleFactor != 1.0f;
}

void saveNodeTransformSettings(std::ostream& file, const ModelNode& node) {
    if (hasNodeTransformSettings(node)) {
        file << "node_transform " << node.nodeId << ' '
             << node.transformTranslation.x << ' '
             << node.transformTranslation.y << ' '
             << node.transformTranslation.z << ' '
             << node.transformRotationDegrees.x << ' '
             << node.transformRotationDegrees.y << ' '
             << node.transformRotationDegrees.z << ' '
             << node.transformScaleFactor << "\n";
    }
    for (const ModelNode& child : node.children) {
        saveNodeTransformSettings(file, child);
    }
}

void saveNodeAnimationSettings(std::ostream& file, const ModelNode& node) {
    if (hasNodeAnimationSettings(node)) {
        file << "node_animation " << node.nodeId << ' '
             << (node.rotationAnimationEnabled ? 1 : 0) << ' '
             << node.rotationAnimationInitialDegrees.x << ' '
             << node.rotationAnimationInitialDegrees.y << ' '
             << node.rotationAnimationInitialDegrees.z << ' '
             << node.rotationAnimationSpeedDegrees.x << ' '
             << node.rotationAnimationSpeedDegrees.y << ' '
             << node.rotationAnimationSpeedDegrees.z << "\n";
    }
    for (const ModelNode& child : node.children) {
        saveNodeAnimationSettings(file, child);
    }
}

void saveNodeMaterialSettings(std::ostream& file, const ModelNode& node) {
    if (node.materialOverride >= 0) {
        file << "node_material " << node.nodeId << ' '
             << node.materialOverride << "\n";
    }
    for (const ModelNode& child : node.children) {
        saveNodeMaterialSettings(file, child);
    }
}

void saveUserMaterials(std::ostream& file, const std::vector<MaterialData>& materials) {
    for (std::size_t materialIndex = 0; materialIndex < materials.size(); ++materialIndex) {
        const MaterialData& material = materials[materialIndex];
        if (!material.userCreated && !material.userModified) {
            continue;
        }
        file << "material " << materialIndex << ' '
             << std::quoted(material.name) << ' '
             << material.baseColor.r << ' ' << material.baseColor.g << ' '
             << material.baseColor.b << ' ' << material.baseColor.a << ' '
             << material.metallic << ' ' << material.roughness << ' '
             << material.opacity << ' '
             << static_cast<int>(material.blendMode) << ' '
             << (material.depthWrite ? 1 : 0) << ' '
             << (material.doubleSided ? 1 : 0) << ' '
             << std::quoted(material.texturePath.string());
        for (const std::string& shaderName : material.shaderConfiguration.shaderNames) {
            file << ' ' << (shaderName.empty() ? "-" : shaderName);
        }
        const WaterShaderSettings& water = material.waterShaderSettings;
        file << ' ' << water.flowDirection.x << ' ' << water.flowDirection.y
             << ' ' << water.flowSpeed << ' ' << water.flowStrength
             << ' ' << water.flowTurbulence << ' ' << water.waveScale
             << ' ' << water.waveHeight
             << ' ' << material.emissionColor.r << ' '
             << material.emissionColor.g << ' ' << material.emissionColor.b << ' '
             << material.emissionStrength << "\n";
        file << "material_textures " << materialIndex << ' '
             << std::quoted(material.normalTexturePath.string()) << ' '
             << std::quoted(material.specularTexturePath.string()) << "\n";
    }
}

void saveParticleSystemSettings(
    std::ostream& file,
    std::uint32_t nodeId,
    const ParticleSystemSettings& settings
) {
    file << "particle_system_object " << nodeId << ' '
         << (settings.enabled ? 1 : 0) << ' '
         << (settings.playOnAwake ? 1 : 0) << ' '
         << (settings.looping ? 1 : 0) << ' '
         << settings.maxParticles << ' '
         << settings.duration << ' '
         << settings.emissionRate << ' '
         << settings.startLifetime << ' '
         << settings.lifetimeRandomness << ' '
         << settings.startSpeed << ' '
         << settings.speedRandomness << ' '
         << settings.startSize << ' '
         << settings.endSize << ' '
         << settings.rotationSpeedDegrees << ' '
         << settings.gravityModifier << ' '
         << settings.position.x << ' ' << settings.position.y << ' ' << settings.position.z << ' '
         << settings.emissionDirection.x << ' ' << settings.emissionDirection.y << ' '
         << settings.emissionDirection.z << ' '
         << settings.spreadDegrees << ' '
         << settings.shapeRadius << ' '
         << static_cast<int>(settings.shape) << ' '
         << static_cast<int>(settings.blendMode) << ' '
         << settings.startColor.r << ' ' << settings.startColor.g << ' '
         << settings.startColor.b << ' ' << settings.startColor.a << ' '
         << settings.endColor.r << ' ' << settings.endColor.g << ' '
         << settings.endColor.b << ' ' << settings.endColor.a << ' '
         << std::quoted(settings.texturePath.string()) << "\n";
}

std::string shaderConfigToken(const std::string& token) {
    return token == "-" ? std::string{} : token;
}

} // namespace

void Renderer::selectModelNode(ModelNode& node) {
    selectedModelNode_ = &node;
    updateBoundingBoxVertices();
}

glm::mat4 Renderer::nodeManualTransform(const ModelNode& node) const {
    if (!hasNodeTransformSettings(node)) {
        return glm::mat4(1.0f);
    }

    const float safeModelScale = std::max(modelScale_, std::numeric_limits<float>::epsilon());
    const glm::mat4 rotation =
        glm::rotate(
            glm::mat4(1.0f),
            glm::radians(node.transformRotationDegrees.z),
            glm::vec3(0.0f, 0.0f, 1.0f)
        ) *
        glm::rotate(
            glm::mat4(1.0f),
            glm::radians(node.transformRotationDegrees.y),
            glm::vec3(0.0f, 1.0f, 0.0f)
        ) *
        glm::rotate(
            glm::mat4(1.0f),
            glm::radians(node.transformRotationDegrees.x),
            glm::vec3(1.0f, 0.0f, 0.0f)
        );
    const glm::mat4 localTransform =
        glm::translate(
            glm::mat4(1.0f),
            node.transformTranslation / safeModelScale
        ) *
        rotation *
        glm::scale(
            glm::mat4(1.0f),
            glm::vec3(std::max(node.transformScaleFactor, kMinimumModelScaleFactor))
        );

    // The imported geometry has already been flattened into model-root
    // coordinates. Conjugating the local transform keeps the selected node
    // as the pivot. The root uses the scene center to retain the previous
    // whole-model transform behavior.
    const glm::mat4 pivot = &node == &meshData_.hierarchy
        ? glm::translate(glm::mat4(1.0f), modelCenter_)
        : node.globalTransform;
    return pivot * localTransform * glm::inverse(pivot);
}

glm::mat4 Renderer::particleObjectTransform(const ModelNode& node) const {
    const glm::mat4 rotation =
        glm::rotate(
            glm::mat4(1.0f),
            glm::radians(node.transformRotationDegrees.z),
            glm::vec3(0.0f, 0.0f, 1.0f)
        ) *
        glm::rotate(
            glm::mat4(1.0f),
            glm::radians(node.transformRotationDegrees.y),
            glm::vec3(0.0f, 1.0f, 0.0f)
        ) *
        glm::rotate(
            glm::mat4(1.0f),
            glm::radians(node.transformRotationDegrees.x),
            glm::vec3(1.0f, 0.0f, 0.0f)
        );
    return glm::translate(glm::mat4(1.0f), node.transformTranslation) *
        rotation *
        glm::scale(
            glm::mat4(1.0f),
            glm::vec3(std::max(node.transformScaleFactor, kMinimumModelScaleFactor))
        );
}

glm::mat4 Renderer::nodeAnimationTransform(
    const ModelNode& node,
    float elapsedSeconds
) const {
    if (!node.rotationAnimationEnabled) {
        return glm::mat4(1.0f);
    }

    const glm::vec3 rotationDegrees = node.rotationAnimationInitialDegrees +
        node.rotationAnimationSpeedDegrees * elapsedSeconds;
    const glm::mat4 rotation =
        glm::rotate(glm::mat4(1.0f), glm::radians(rotationDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f)) *
        glm::rotate(glm::mat4(1.0f), glm::radians(rotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f)) *
        glm::rotate(glm::mat4(1.0f), glm::radians(rotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));

    // The flattened vertex buffer is in model-root coordinates. Conjugating
    // the local rotation by the node's global transform makes the selected
    // node's own coordinate system the animation frame.
    return node.globalTransform * rotation * glm::inverse(node.globalTransform);
}

void Renderer::updateSceneNodeTransforms(float elapsedSeconds) {
    sceneNodeTransforms_.clear();

    const auto updateNode = [this, elapsedSeconds](
                                const auto& self,
                                const ModelNode& node,
                                const glm::mat4& parentTransform
                            ) -> void {
        glm::mat4 transform = parentTransform * nodeManualTransform(node);
        if (node.rotationAnimationEnabled) {
            // Parent animation is applied after child-local animation, so
            // transforms are accumulated in hierarchy order from root to leaf.
            transform *= nodeAnimationTransform(node, elapsedSeconds);
        }
        sceneNodeTransforms_.insert_or_assign(node.nodeId, transform);
        for (const ModelNode& child : node.children) {
            self(self, child, transform);
        }
    };

    updateNode(updateNode, meshData_.hierarchy, glm::mat4(1.0f));
    for (const ModelNode& node : meshData_.additionalNodes) {
        updateNode(updateNode, node, glm::mat4(1.0f));
    }
}

glm::mat4 Renderer::transformForSceneNode(std::uint32_t nodeId) const {
    const auto transformIt = sceneNodeTransforms_.find(nodeId);
    return transformIt != sceneNodeTransforms_.end()
        ? transformIt->second
        : glm::mat4(1.0f);
}

void Renderer::updateBoundingBoxVertices() {
    boundingBoxVertices_.fill(Vertex{});
    if (selectedModelNode_ == nullptr || !selectedModelNode_->hasBounds) {
        return;
    }

    const glm::mat4 nodeTransform = transformForSceneNode(selectedModelNode_->nodeId);

    const glm::vec3& minimum = selectedModelNode_->boundsMinimum;
    const glm::vec3& maximum = selectedModelNode_->boundsMaximum;
    const std::array<glm::vec3, 8> corners = {
        glm::vec3(minimum.x, minimum.y, minimum.z),
        glm::vec3(maximum.x, minimum.y, minimum.z),
        glm::vec3(maximum.x, maximum.y, minimum.z),
        glm::vec3(minimum.x, maximum.y, minimum.z),
        glm::vec3(minimum.x, minimum.y, maximum.z),
        glm::vec3(maximum.x, minimum.y, maximum.z),
        glm::vec3(maximum.x, maximum.y, maximum.z),
        glm::vec3(minimum.x, maximum.y, maximum.z)
    };

    for (std::size_t vertexIndex = 0; vertexIndex < kBoundingBoxEdges.size(); ++vertexIndex) {
        boundingBoxVertices_[vertexIndex].position = glm::vec3(
            nodeTransform * glm::vec4(corners[kBoundingBoxEdges[vertexIndex]], 1.0f)
        );
    }
}

std::int32_t Renderer::inheritedMaterialOverrideForSceneNode(std::uint32_t nodeId) const {
    std::int32_t result = -1;
    if (findInheritedMaterialOverride(meshData_.hierarchy, nodeId, -1, result)) {
        return result;
    }
    for (const ModelNode& node : meshData_.additionalNodes) {
        if (findInheritedMaterialOverride(node, nodeId, -1, result)) {
            return result;
        }
    }
    return -1;
}

void Renderer::drawModelHierarchy(ModelNode& node) {
    const bool hasChildren = !node.children.empty();
    const bool isRootNode = &node == &meshData_.hierarchy;
    const bool nodeSelected = selectedModelNode_ == &node;
    std::string label = node.name.empty() ? "<unnamed>" : node.name;
    if (node.objectType == SceneObjectType::ParticleSystem) {
        label += "  [Particle System]";
    } else if (!isRootNode && node.meshCount > 0) {
        label += "  [" + std::to_string(node.meshCount) +
            (node.meshCount == 1 ? " mesh]" : " meshes]");
    }

    ImGui::PushID(&node);
    if (!hasChildren) {
        ImGui::TreeNodeEx(
            label.c_str(),
            ImGuiTreeNodeFlags_Leaf |
                ImGuiTreeNodeFlags_NoTreePushOnOpen |
                ImGuiTreeNodeFlags_SpanAvailWidth |
                (nodeSelected ? ImGuiTreeNodeFlags_Selected : 0)
        );
        if (ImGui::IsItemClicked()) {
            selectModelNode(node);
        }
    } else {
        const bool isOpen = ImGui::TreeNodeEx(
            label.c_str(),
            ImGuiTreeNodeFlags_SpanAvailWidth |
                (nodeSelected ? ImGuiTreeNodeFlags_Selected : 0)
        );
        if (ImGui::IsItemClicked()) {
            selectModelNode(node);
        }
        if (!isOpen) {
            ImGui::PopID();
            return;
        }

        for (ModelNode& child : node.children) {
            drawModelHierarchy(child);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void Renderer::drawNodeMaterialControls(ModelNode& node, bool& shouldSaveSettings) {
    ImGui::SeparatorText("Node Material");
    ImGui::TextDisabled("A material override applies to this node and its children.");

    const std::int32_t inheritedOverride = inheritedMaterialOverrideForSceneNode(node.nodeId);
    const std::int32_t localOverride = node.materialOverride;
    const std::int32_t effectiveOverride = localOverride >= 0
        ? localOverride
        : inheritedOverride;
    const bool hasEffectiveMaterial = effectiveOverride >= 0 &&
        static_cast<std::size_t>(effectiveOverride) < meshData_.materials.size();
    const char* preview = localOverride >= 0
        ? (hasEffectiveMaterial ? meshData_.materials[effectiveOverride].name.c_str() : "<Invalid material>")
        : (inheritedOverride >= 0 ? "<Inherited material>" : "<Use mesh material>");

    if (ImGui::BeginCombo("Material", preview)) {
        const bool useMeshMaterial = node.materialOverride < 0;
        if (ImGui::Selectable("<Use mesh material>", useMeshMaterial)) {
            node.materialOverride = -1;
            shouldSaveSettings = true;
        }
        for (std::size_t materialIndex = 0; materialIndex < meshData_.materials.size(); ++materialIndex) {
            const bool selected = node.materialOverride == static_cast<std::int32_t>(materialIndex);
            const std::string label = meshData_.materials[materialIndex].name.empty()
                ? "Material " + std::to_string(materialIndex)
                : meshData_.materials[materialIndex].name;
            if (ImGui::Selectable(label.c_str(), selected)) {
                node.materialOverride = static_cast<std::int32_t>(materialIndex);
                shouldSaveSettings = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::Button("Create new Material")) {
        MaterialData material;
        material.name = "CustomMaterial_" + std::to_string(meshData_.materials.size());
        material.userCreated = true;
        meshData_.materials.push_back(material);
        node.materialOverride = static_cast<std::int32_t>(meshData_.materials.size() - 1);
        rebuildMaterialResources();
        shouldSaveSettings = true;
    }
    if (hasEffectiveMaterial) {
        ImGui::SameLine();
        if (ImGui::Button("Duplicate for Node")) {
            MaterialData material = meshData_.materials[effectiveOverride];
            material.name += "_Copy";
            material.userCreated = true;
            meshData_.materials.push_back(std::move(material));
            node.materialOverride = static_cast<std::int32_t>(meshData_.materials.size() - 1);
            rebuildMaterialResources();
            shouldSaveSettings = true;
        }
    }

    if (!hasEffectiveMaterial) {
        ImGui::TextDisabled("Select or create a material to edit its parameters.");
        return;
    }

    MaterialData& material = meshData_.materials[effectiveOverride];
    const auto markMaterialChanged = [&]() {
        material.userModified = true;
        shouldSaveSettings = true;
    };
    ImGui::Text("Editing: %s", material.name.c_str());
    if (materialNameMaterialIndex_ != effectiveOverride) {
        materialNameMaterialIndex_ = effectiveOverride;
        materialNameBuffer_.fill('\0');
        const std::size_t copyCount = std::min(
            material.name.size(),
            materialNameBuffer_.size() - 1
        );
        std::copy_n(material.name.data(), copyCount, materialNameBuffer_.data());
    }
    if (ImGui::InputText("Name", materialNameBuffer_.data(), materialNameBuffer_.size())) {
        material.name = materialNameBuffer_.data();
        markMaterialChanged();
    }
    ImGui::ColorEdit4("Base color", &material.baseColor.x);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        markMaterialChanged();
    }
    ImGui::SliderFloat("Metallic", &material.metallic, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        markMaterialChanged();
    }
    ImGui::SliderFloat("Roughness", &material.roughness, 0.04f, 1.0f, "%.2f");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        markMaterialChanged();
    }
    ImGui::SliderFloat("Opacity", &material.opacity, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        markMaterialChanged();
    }
    ImGui::SeparatorText("Emission");
    ImGui::ColorEdit3("Emission color", &material.emissionColor.x);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        markMaterialChanged();
    }
    ImGui::SliderFloat("Emission strength", &material.emissionStrength, 0.0f, 20.0f, "%.2f");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        markMaterialChanged();
    }

    const std::array<MaterialData::BlendMode, 4> blendModes = {
        MaterialData::BlendMode::Opaque,
        MaterialData::BlendMode::AlphaBlend,
        MaterialData::BlendMode::Additive,
        MaterialData::BlendMode::Multiply,
    };
    const char* blendPreview = materialBlendModeLabel(material.blendMode);
    if (ImGui::BeginCombo("Blend mode", blendPreview)) {
        for (const MaterialData::BlendMode blendMode : blendModes) {
            const bool selected = material.blendMode == blendMode;
            if (ImGui::Selectable(materialBlendModeLabel(blendMode), selected)) {
                material.blendMode = blendMode;
                markMaterialChanged();
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::Checkbox("Depth write", &material.depthWrite)) {
        markMaterialChanged();
    }
    if (ImGui::Checkbox("Double-sided", &material.doubleSided)) {
        markMaterialChanged();
    }

    if (materialTexturePathMaterialIndex_ != effectiveOverride) {
        materialTexturePathMaterialIndex_ = effectiveOverride;
        materialTexturePathBuffer_.fill('\0');
        const std::string path = material.texturePath.string();
        const std::size_t copyCount = std::min(
            path.size(),
            materialTexturePathBuffer_.size() - 1
        );
        std::copy_n(path.data(), copyCount, materialTexturePathBuffer_.data());
    }
    ImGui::InputText(
        "Base color texture",
        materialTexturePathBuffer_.data(),
        materialTexturePathBuffer_.size()
    );
    if (ImGui::Button("Load material texture")) {
        material.texturePath = materialTexturePathBuffer_.data();
        material.embeddedTexture = EmbeddedTextureData{};
        rebuildMaterialResources();
        markMaterialChanged();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear texture")) {
        material.texturePath.clear();
        material.embeddedTexture = EmbeddedTextureData{};
        materialTexturePathBuffer_.fill('\0');
        rebuildMaterialResources();
        markMaterialChanged();
    }

    const auto syncTexturePathBuffer = [](
        std::array<char, 512>& buffer,
        std::int32_t& bufferMaterialIndex,
        std::int32_t materialIndex,
        const std::filesystem::path& path
    ) {
        if (bufferMaterialIndex == materialIndex) {
            return;
        }
        bufferMaterialIndex = materialIndex;
        buffer.fill('\0');
        const std::string pathString = path.string();
        const std::size_t copyCount = std::min(pathString.size(), buffer.size() - 1);
        std::copy_n(pathString.data(), copyCount, buffer.data());
    };

    syncTexturePathBuffer(
        materialNormalTexturePathBuffer_,
        materialNormalTexturePathMaterialIndex_,
        effectiveOverride,
        material.normalTexturePath
    );
    ImGui::InputText(
        "Normal texture",
        materialNormalTexturePathBuffer_.data(),
        materialNormalTexturePathBuffer_.size()
    );
    if (ImGui::Button("Load normal texture")) {
        material.normalTexturePath = materialNormalTexturePathBuffer_.data();
        material.embeddedNormalTexture = EmbeddedTextureData{};
        rebuildMaterialResources();
        markMaterialChanged();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear normal")) {
        material.normalTexturePath.clear();
        material.embeddedNormalTexture = EmbeddedTextureData{};
        materialNormalTexturePathBuffer_.fill('\0');
        rebuildMaterialResources();
        markMaterialChanged();
    }

    syncTexturePathBuffer(
        materialSpecularTexturePathBuffer_,
        materialSpecularTexturePathMaterialIndex_,
        effectiveOverride,
        material.specularTexturePath
    );
    ImGui::InputText(
        "Specular texture",
        materialSpecularTexturePathBuffer_.data(),
        materialSpecularTexturePathBuffer_.size()
    );
    if (ImGui::Button("Load specular texture")) {
        material.specularTexturePath = materialSpecularTexturePathBuffer_.data();
        material.embeddedSpecularTexture = EmbeddedTextureData{};
        rebuildMaterialResources();
        markMaterialChanged();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear specular")) {
        material.specularTexturePath.clear();
        material.embeddedSpecularTexture = EmbeddedTextureData{};
        materialSpecularTexturePathBuffer_.fill('\0');
        rebuildMaterialResources();
        markMaterialChanged();
    }

    drawMaterialShaderControls(material, shouldSaveSettings);
}

void Renderer::drawMaterialShaderControls(MaterialData& material, bool& shouldSaveSettings) {
    const auto markMaterialChanged = [&]() {
        material.userModified = true;
        shouldSaveSettings = true;
    };

    ImGui::SeparatorText("Material Shaders");
    ImGui::TextDisabled("Empty stages fall back to the default shader.");

    const std::array<ShaderStage, kShaderStageCount> stages = {
        ShaderStage::Vertex,
        ShaderStage::TessellationControl,
        ShaderStage::TessellationEvaluation,
        ShaderStage::Geometry,
        ShaderStage::Fragment,
        ShaderStage::Compute
    };
    for (const ShaderStage stage : stages) {
        std::string preview = material.shaderConfiguration.shader(stage).empty()
            ? "<Default>"
            : material.shaderConfiguration.shader(stage);
        ImGui::PushID(static_cast<int>(stage));
        if (ImGui::BeginCombo(shaderStageLabel(stage), preview.c_str())) {
            const bool defaultSelected = material.shaderConfiguration.shader(stage).empty();
            if (ImGui::Selectable("<Default>", defaultSelected)) {
                material.shaderConfiguration.shader(stage).clear();
                markMaterialChanged();
            }
            if (defaultSelected) {
                ImGui::SetItemDefaultFocus();
            }

            const auto& shaders = availableShaders_[static_cast<std::size_t>(stage)];
            for (const std::string& shaderName : shaders) {
                const bool selected = material.shaderConfiguration.shader(stage) == shaderName;
                if (ImGui::Selectable(shaderName.c_str(), selected)) {
                    material.shaderConfiguration.shader(stage) = shaderName;
                    markMaterialChanged();
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }

    if (ImGui::Button("Apply water shader")) {
        material.shaderConfiguration = ShaderConfiguration{};
        material.shaderConfiguration.shader(ShaderStage::Vertex) = "shader_water.vert";
        material.shaderConfiguration.shader(ShaderStage::Fragment) = "shader_water.frag";
        markMaterialChanged();
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply flowing water")) {
        material.shaderConfiguration = ShaderConfiguration{};
        material.shaderConfiguration.shader(ShaderStage::Vertex) = "shader_water.vert";
        material.shaderConfiguration.shader(ShaderStage::Fragment) = "shader_water_flow.frag";
        markMaterialChanged();
    }

    if (isWaterShaderConfiguration(material.shaderConfiguration)) {
        ImGui::SeparatorText("Water Flow");
        ImGui::TextDisabled("Direction is in the mesh UV space: +X right, +Y up.");
        ImGui::DragFloat2(
            "Flow direction",
            &material.waterShaderSettings.flowDirection.x,
            0.01f,
            -1.0f,
            1.0f,
            "%.2f"
        );
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            if (glm::length(material.waterShaderSettings.flowDirection) < 0.001f) {
                material.waterShaderSettings.flowDirection = glm::vec2(0.0f, 1.0f);
            }
            markMaterialChanged();
        }
        ImGui::SliderFloat("Flow speed", &material.waterShaderSettings.flowSpeed, 0.0f, 4.0f, "%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit()) markMaterialChanged();
        ImGui::SliderFloat("Flow strength", &material.waterShaderSettings.flowStrength, 0.0f, 4.0f, "%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit()) markMaterialChanged();
        ImGui::SliderFloat("Flow turbulence", &material.waterShaderSettings.flowTurbulence, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit()) markMaterialChanged();
        ImGui::SliderFloat("Wave scale", &material.waterShaderSettings.waveScale, 0.1f, 8.0f, "%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit()) markMaterialChanged();
        ImGui::SliderFloat("Wave height", &material.waterShaderSettings.waveHeight, 0.0f, 0.2f, "%.3f");
        if (ImGui::IsItemDeactivatedAfterEdit()) markMaterialChanged();
        if (ImGui::Button("Reset water parameters")) {
            material.waterShaderSettings = WaterShaderSettings{};
            markMaterialChanged();
        }
    }

    if (material.shaderConfiguration.hasComputeOverride()) {
        if (computePipelineForMaterial(material) != VK_NULL_HANDLE) {
            ImGui::TextDisabled("Compute pipeline ready (not dispatched by the mesh pass).");
        } else {
            ImGui::TextColored(
                ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
                "Compute shader pipeline could not be created."
            );
        }
    }
    if (ImGui::Button("Reset material shaders")) {
        material.shaderConfiguration = ShaderConfiguration{};
        markMaterialChanged();
    }
}

Renderer::Renderer(GLFWwindow* window)
    : startTime_(std::chrono::steady_clock::now()) {
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
    createBoundingBoxPipeline();
    createParticlePipelines();
    swapChain_->createFramebuffers(renderPass_);
    createMeshData();
    createSunMesh();
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
    uniformBuffers_.clear();
    materialTextures_.clear();
    normalTextures_.clear();
    specularTextures_.clear();
    particleTextures_.clear();
    fallbackTexture_.reset();
    fallbackNormalTexture_.reset();
    indexBuffer_.reset();
    vertexBuffer_.reset();
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

void Renderer::initializeImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    if (!ImGui_ImplGlfw_InitForVulkan(context_->window(), true)) {
        ImGui::DestroyContext();
        throw std::runtime_error("failed to initialize Dear ImGui GLFW backend!");
    }

    const QueueFamilyIndices queueFamilies =
        context_->findQueueFamilies(context_->physicalDevice());
    const uint32_t imageCount = static_cast<uint32_t>(swapChain_->framebuffers().size());

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_0;
    initInfo.Instance = context_->instance();
    initInfo.PhysicalDevice = context_->physicalDevice();
    initInfo.Device = context_->device();
    initInfo.QueueFamily = queueFamilies.graphicsFamily.value();
    initInfo.Queue = context_->graphicsQueue();
    // Let the backend create a private pool with the flags it needs for
    // texture descriptor updates. The engine's material pool stays isolated.
    initInfo.DescriptorPoolSize = 1000;
    initInfo.MinImageCount = std::max(2u, imageCount);
    initInfo.ImageCount = std::max(2u, imageCount);
    initInfo.PipelineInfoMain.RenderPass = renderPass_;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        throw std::runtime_error("failed to initialize Dear ImGui Vulkan backend!");
    }

    imguiInitialized_ = true;
}

void Renderer::shutdownImGui() {
    if (!imguiInitialized_) {
        return;
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    imguiInitialized_ = false;
}

void Renderer::loadTransformSettings() {
    const std::filesystem::path configPath(kConfigPath);
    std::ifstream file(configPath);
    if (!file.is_open()) {
        return;
    }

    glm::vec3 legacyTranslation(0.0f);
    glm::vec3 legacyRotationDegrees(0.0f);
    float legacyScaleFactor = 1.0f;
    bool hasLegacyTransformSettings = false;
    glm::vec3 ambientLightColor = ambientLightColor_;
    float ambientLightIntensity = ambientLightIntensity_;
    glm::vec3 sunLightDirection = sunLightDirection_;
    glm::vec3 sunLightColor = sunLightColor_;
    float sunLightIntensity = sunLightIntensity_;
    struct PendingNodeAnimation {
        std::uint32_t nodeId = 0;
        bool enabled = false;
        glm::vec3 initialDegrees{0.0f};
        glm::vec3 speedDegrees{0.0f};
    };
    struct PendingNodeTransform {
        std::uint32_t nodeId = 0;
        glm::vec3 translation{0.0f};
        glm::vec3 rotationDegrees{0.0f};
        float scaleFactor = 1.0f;
    };
    struct PendingNodeShaders {
        std::uint32_t nodeId = 0;
        ShaderConfiguration configuration;
    };
    struct PendingNodeWater {
        std::uint32_t nodeId = 0;
        WaterShaderSettings settings;
    };
    struct PendingMaterial {
        std::uint32_t materialIndex = 0;
        MaterialData material;
    };
    struct PendingMaterialTextures {
        std::uint32_t materialIndex = 0;
        std::filesystem::path normalTexturePath;
        std::filesystem::path specularTexturePath;
    };
    struct PendingNodeMaterial {
        std::uint32_t nodeId = 0;
        std::int32_t materialIndex = -1;
    };
    struct PendingParticleSystem {
        std::uint32_t nodeId = 0;
        ParticleSystemSettings settings;
    };
    std::vector<PendingNodeAnimation> pendingNodeAnimations;
    std::vector<PendingNodeTransform> pendingNodeTransforms;
    std::vector<PendingNodeShaders> pendingNodeShaders;
    std::vector<PendingNodeWater> pendingNodeWater;
    std::vector<PendingMaterial> pendingMaterials;
    std::vector<PendingMaterialTextures> pendingMaterialTextures;
    std::vector<PendingNodeMaterial> pendingNodeMaterials;
    std::vector<PendingParticleSystem> pendingParticleSystems;
    bool valid = true;
    std::string key;
    while (file >> key) {
        if (key == "position") {
            if (!(file >> legacyTranslation.x >> legacyTranslation.y >> legacyTranslation.z)) {
                valid = false;
                break;
            }
            hasLegacyTransformSettings = true;
        } else if (key == "rotation_degrees") {
            if (!(file >> legacyRotationDegrees.x >> legacyRotationDegrees.y >>
                  legacyRotationDegrees.z)) {
                valid = false;
                break;
            }
            hasLegacyTransformSettings = true;
        } else if (key == "scale") {
            if (!(file >> legacyScaleFactor)) {
                valid = false;
                break;
            }
            hasLegacyTransformSettings = true;
        } else if (key == "node_transform") {
            PendingNodeTransform transform;
            if (!(file >> transform.nodeId >> transform.translation.x >>
                  transform.translation.y >> transform.translation.z >>
                  transform.rotationDegrees.x >> transform.rotationDegrees.y >>
                  transform.rotationDegrees.z >> transform.scaleFactor) ||
                transform.scaleFactor <= 0.0f) {
                valid = false;
                break;
            }
            pendingNodeTransforms.push_back(transform);
        } else if (key == "node_animation") {
            PendingNodeAnimation animation;
            int enabled = 0;
            if (!(file >> animation.nodeId >> enabled >>
                  animation.initialDegrees.x >> animation.initialDegrees.y >>
                  animation.initialDegrees.z >> animation.speedDegrees.x >>
                  animation.speedDegrees.y >> animation.speedDegrees.z)) {
                valid = false;
                break;
            }
            if (enabled != 0 && enabled != 1) {
                valid = false;
                break;
            }
            animation.enabled = enabled != 0;
            pendingNodeAnimations.push_back(animation);
        } else if (key == "node_shaders") {
            PendingNodeShaders shaders;
            if (!(file >> shaders.nodeId)) {
                valid = false;
                break;
            }
            for (std::string& shaderName : shaders.configuration.shaderNames) {
                std::string token;
                if (!(file >> token)) {
                    valid = false;
                    break;
                }
                shaderName = shaderConfigToken(token);
            }
            if (!valid) {
                break;
            }
            pendingNodeShaders.push_back(std::move(shaders));
        } else if (key == "node_water") {
            PendingNodeWater water;
            if (!(file >> water.nodeId >> water.settings.flowDirection.x >>
                  water.settings.flowDirection.y >> water.settings.flowSpeed >>
                  water.settings.flowStrength >> water.settings.flowTurbulence >>
                  water.settings.waveScale >> water.settings.waveHeight)) {
                valid = false;
                break;
            }
            pendingNodeWater.push_back(water);
        } else if (key == "material") {
            PendingMaterial pending;
            int blendMode = 0;
            int depthWrite = 1;
            int doubleSided = 0;
            std::string texturePath;
            std::string materialLine;
            std::getline(file, materialLine);
            std::istringstream materialStream(materialLine);
            if (!(materialStream >> pending.materialIndex >>
                  std::quoted(pending.material.name) >>
                  pending.material.baseColor.r >> pending.material.baseColor.g >>
                  pending.material.baseColor.b >> pending.material.baseColor.a >>
                  pending.material.metallic >> pending.material.roughness >>
                  pending.material.opacity >> blendMode >> depthWrite >> doubleSided >>
                  std::quoted(texturePath))) {
                valid = false;
                break;
            }
            if (blendMode < 0 || blendMode > 3 ||
                (depthWrite != 0 && depthWrite != 1) ||
                (doubleSided != 0 && doubleSided != 1)) {
                valid = false;
                break;
            }
            pending.material.blendMode = static_cast<MaterialData::BlendMode>(blendMode);
            pending.material.depthWrite = depthWrite != 0;
            pending.material.doubleSided = doubleSided != 0;
            pending.material.texturePath = texturePath;
            pending.material.userCreated = true;
            // Material lines written by the first Material implementation did
            // not contain shader/water fields. Keep those materials valid and
            // use default shader/water settings when the optional suffix is
            // absent.
            std::string firstOptionalToken;
            if (materialStream >> firstOptionalToken) {
                pending.material.shaderConfiguration.shader(ShaderStage::Vertex) =
                    shaderConfigToken(firstOptionalToken);
                for (std::size_t stageIndex = 1; stageIndex < kShaderStageCount; ++stageIndex) {
                    std::string token;
                    if (!(materialStream >> token)) {
                        valid = false;
                        break;
                    }
                    pending.material.shaderConfiguration.shader(
                        static_cast<ShaderStage>(stageIndex)
                    ) = shaderConfigToken(token);
                }
                if (!valid || !(materialStream >> pending.material.waterShaderSettings.flowDirection.x >>
                                pending.material.waterShaderSettings.flowDirection.y >>
                                pending.material.waterShaderSettings.flowSpeed >>
                                pending.material.waterShaderSettings.flowStrength >>
                                pending.material.waterShaderSettings.flowTurbulence >>
                                pending.material.waterShaderSettings.waveScale >>
                                pending.material.waterShaderSettings.waveHeight)) {
                    valid = false;
                    break;
                }
                // Emission was added after the original material settings.
                // Keep older config files valid when this optional suffix is
                // absent.
                if (materialStream >> pending.material.emissionColor.r) {
                    if (!(materialStream >> pending.material.emissionColor.g >>
                          pending.material.emissionColor.b >>
                          pending.material.emissionStrength)) {
                        valid = false;
                        break;
                    }
                }
            }
            pendingMaterials.push_back(std::move(pending));
        } else if (key == "material_textures") {
            PendingMaterialTextures pending;
            std::string normalTexturePath;
            std::string specularTexturePath;
            if (!(file >> pending.materialIndex >> std::quoted(normalTexturePath) >>
                  std::quoted(specularTexturePath))) {
                valid = false;
                break;
            }
            pending.normalTexturePath = std::move(normalTexturePath);
            pending.specularTexturePath = std::move(specularTexturePath);
            pendingMaterialTextures.push_back(std::move(pending));
        } else if (key == "node_material") {
            PendingNodeMaterial material;
            if (!(file >> material.nodeId >> material.materialIndex) ||
                material.materialIndex < -1) {
                valid = false;
                break;
            }
            pendingNodeMaterials.push_back(material);
        } else if (key == "ambient_color") {
            if (!(file >> ambientLightColor.r >> ambientLightColor.g >> ambientLightColor.b)) {
                valid = false;
                break;
            }
        } else if (key == "ambient_intensity") {
            if (!(file >> ambientLightIntensity)) {
                valid = false;
                break;
            }
        } else if (key == "sun_light_direction") {
            if (!(file >> sunLightDirection.x >> sunLightDirection.y >> sunLightDirection.z)) {
                valid = false;
                break;
            }
        } else if (key == "sun_light_color") {
            if (!(file >> sunLightColor.r >> sunLightColor.g >> sunLightColor.b)) {
                valid = false;
                break;
            }
        } else if (key == "sun_light_intensity") {
            if (!(file >> sunLightIntensity)) {
                valid = false;
                break;
            }
        } else if (key == "particle_system_object") {
            int enabled = 0;
            int playOnAwake = 0;
            int looping = 0;
            int shape = 0;
            int blendMode = 0;
            PendingParticleSystem pending;
            ParticleSystemSettings& settings = pending.settings;
            std::string particleLine;
            std::getline(file, particleLine);
            std::istringstream particleStream(particleLine);
            if (!(particleStream >> pending.nodeId >> enabled >> playOnAwake >> looping
                  >> settings.maxParticles >> settings.duration >> settings.emissionRate
                  >> settings.startLifetime >> settings.lifetimeRandomness >> settings.startSpeed
                  >> settings.speedRandomness >> settings.startSize >> settings.endSize
                  >> settings.rotationSpeedDegrees >> settings.gravityModifier
                  >> settings.position.x >> settings.position.y >> settings.position.z
                  >> settings.emissionDirection.x >> settings.emissionDirection.y
                  >> settings.emissionDirection.z >> settings.spreadDegrees >> settings.shapeRadius
                  >> shape >> blendMode >> settings.startColor.r >> settings.startColor.g
                  >> settings.startColor.b >> settings.startColor.a >> settings.endColor.r
                  >> settings.endColor.g >> settings.endColor.b >> settings.endColor.a) ||
                (enabled != 0 && enabled != 1) ||
                (playOnAwake != 0 && playOnAwake != 1) ||
                (looping != 0 && looping != 1) ||
                shape < 0 || shape > static_cast<int>(ParticleSystemSettings::Shape::Cone) ||
                blendMode < 0 ||
                blendMode > static_cast<int>(ParticleSystemSettings::BlendMode::Additive)) {
                valid = false;
                break;
            }
            std::string texturePath;
            if (particleStream >> std::quoted(texturePath)) {
                settings.texturePath = std::move(texturePath);
            }
            settings.enabled = enabled != 0;
            settings.playOnAwake = playOnAwake != 0;
            settings.looping = looping != 0;
            settings.shape = static_cast<ParticleSystemSettings::Shape>(shape);
            settings.blendMode = static_cast<ParticleSystemSettings::BlendMode>(blendMode);
            pendingParticleSystems.push_back(std::move(pending));
        } else {
            std::string ignoredLine;
            std::getline(file, ignoredLine);
        }
    }

    const bool finiteValues =
        std::isfinite(legacyTranslation.x) && std::isfinite(legacyTranslation.y) &&
        std::isfinite(legacyTranslation.z) && std::isfinite(legacyRotationDegrees.x) &&
        std::isfinite(legacyRotationDegrees.y) && std::isfinite(legacyRotationDegrees.z) &&
        std::isfinite(legacyScaleFactor);
    const bool finiteLightingValues =
        std::isfinite(ambientLightColor.r) && std::isfinite(ambientLightColor.g) &&
        std::isfinite(ambientLightColor.b) && std::isfinite(ambientLightIntensity) &&
        std::isfinite(sunLightDirection.x) && std::isfinite(sunLightDirection.y) &&
        std::isfinite(sunLightDirection.z) && std::isfinite(sunLightColor.r) &&
        std::isfinite(sunLightColor.g) && std::isfinite(sunLightColor.b) &&
        std::isfinite(sunLightIntensity);
    const bool finiteNodeValues = std::all_of(
        pendingNodeAnimations.begin(),
        pendingNodeAnimations.end(),
        [](const PendingNodeAnimation& animation) {
            return std::isfinite(animation.initialDegrees.x) &&
                std::isfinite(animation.initialDegrees.y) &&
                std::isfinite(animation.initialDegrees.z) &&
                std::isfinite(animation.speedDegrees.x) &&
                std::isfinite(animation.speedDegrees.y) &&
                std::isfinite(animation.speedDegrees.z);
        }
    );
    const bool finiteNodeTransformValues = std::all_of(
        pendingNodeTransforms.begin(),
        pendingNodeTransforms.end(),
        [](const PendingNodeTransform& transform) {
            return std::isfinite(transform.translation.x) &&
                std::isfinite(transform.translation.y) &&
                std::isfinite(transform.translation.z) &&
                std::isfinite(transform.rotationDegrees.x) &&
                std::isfinite(transform.rotationDegrees.y) &&
                std::isfinite(transform.rotationDegrees.z) &&
                std::isfinite(transform.scaleFactor);
        }
    );
    const bool finiteWaterValues = std::all_of(
        pendingNodeWater.begin(),
        pendingNodeWater.end(),
        [](const PendingNodeWater& water) {
            return std::isfinite(water.settings.flowDirection.x) &&
                std::isfinite(water.settings.flowDirection.y) &&
                std::isfinite(water.settings.flowSpeed) &&
                std::isfinite(water.settings.flowStrength) &&
                std::isfinite(water.settings.flowTurbulence) &&
                std::isfinite(water.settings.waveScale) &&
                std::isfinite(water.settings.waveHeight);
        }
    );
    const bool finiteMaterialValues = std::all_of(
        pendingMaterials.begin(),
        pendingMaterials.end(),
        [](const PendingMaterial& pending) {
            const glm::vec4& color = pending.material.baseColor;
            return std::isfinite(color.r) && std::isfinite(color.g) &&
                std::isfinite(color.b) && std::isfinite(color.a) &&
                std::isfinite(pending.material.metallic) &&
                std::isfinite(pending.material.roughness) &&
                std::isfinite(pending.material.opacity) &&
                std::isfinite(pending.material.emissionColor.r) &&
                std::isfinite(pending.material.emissionColor.g) &&
                std::isfinite(pending.material.emissionColor.b) &&
                std::isfinite(pending.material.emissionStrength) &&
                std::isfinite(pending.material.waterShaderSettings.flowDirection.x) &&
                std::isfinite(pending.material.waterShaderSettings.flowDirection.y) &&
                std::isfinite(pending.material.waterShaderSettings.flowSpeed) &&
                std::isfinite(pending.material.waterShaderSettings.flowStrength) &&
                std::isfinite(pending.material.waterShaderSettings.flowTurbulence) &&
                std::isfinite(pending.material.waterShaderSettings.waveScale) &&
                std::isfinite(pending.material.waterShaderSettings.waveHeight);
        }
    );
    const auto finiteVector = [](const glm::vec3& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    const auto finiteColor = [](const glm::vec4& value) {
        return std::isfinite(value.r) && std::isfinite(value.g) &&
            std::isfinite(value.b) && std::isfinite(value.a);
    };
    const bool finiteParticleValues = std::all_of(
        pendingParticleSystems.begin(),
        pendingParticleSystems.end(),
        [&finiteVector, &finiteColor](const PendingParticleSystem& pending) {
            const ParticleSystemSettings& settings = pending.settings;
            return std::isfinite(settings.duration) &&
                std::isfinite(settings.emissionRate) &&
                std::isfinite(settings.startLifetime) &&
                std::isfinite(settings.lifetimeRandomness) &&
                std::isfinite(settings.startSpeed) &&
                std::isfinite(settings.speedRandomness) &&
                std::isfinite(settings.startSize) &&
                std::isfinite(settings.endSize) &&
                std::isfinite(settings.rotationSpeedDegrees) &&
                std::isfinite(settings.gravityModifier) &&
                finiteVector(settings.position) &&
                finiteVector(settings.emissionDirection) &&
                std::isfinite(settings.spreadDegrees) &&
                std::isfinite(settings.shapeRadius) &&
                finiteColor(settings.startColor) &&
                finiteColor(settings.endColor);
        }
    );
    if (!valid || !finiteValues || !finiteLightingValues || !finiteNodeValues ||
        !finiteNodeTransformValues || !finiteWaterValues || !finiteMaterialValues ||
        !finiteParticleValues) {
        std::cerr << "warning: ignoring invalid VulkCore transform settings in '"
                  << configPath.string() << "'\n";
        return;
    }

    ambientLightColor_ = glm::clamp(ambientLightColor, glm::vec3(0.0f), glm::vec3(1.0f));
    ambientLightIntensity_ = std::clamp(ambientLightIntensity, 0.0f, 4.0f);
    sunLightDirection_ = sunLightDirection;
    if (glm::length(sunLightDirection_) < 0.001f) {
        sunLightDirection_ = glm::vec3(0.45f, 0.55f, 0.75f);
    } else {
        sunLightDirection_ = glm::normalize(sunLightDirection_);
    }
    sunLightColor_ = glm::clamp(sunLightColor, glm::vec3(0.0f), glm::vec3(1.0f));
    sunLightIntensity_ = std::clamp(sunLightIntensity, 0.0f, 4.0f);
    for (PendingParticleSystem& pending : pendingParticleSystems) {
        ModelNode* node = findSceneNodeById(meshData_, pending.nodeId);
        if (node == nullptr) {
            if (particleSystems_.size() >= kMaximumParticleSystems) {
                continue;
            }
            ModelNode particleNode;
            particleNode.name = "Particle System " + std::to_string(pending.nodeId);
            particleNode.nodeId = pending.nodeId;
            particleNode.objectType = SceneObjectType::ParticleSystem;
            meshData_.additionalNodes.push_back(std::move(particleNode));
            node = &meshData_.additionalNodes.back();
        }
        if (node->objectType != SceneObjectType::ParticleSystem) {
            continue;
        }
        if (!particleSystems_.contains(node->nodeId) &&
            particleSystems_.size() >= kMaximumParticleSystems) {
            continue;
        }
        auto [systemIt, inserted] = particleSystems_.try_emplace(node->nodeId);
        (void)inserted;
        systemIt->second.setSettings(pending.settings);
        if (pending.settings.enabled && pending.settings.playOnAwake) {
            systemIt->second.restart();
        } else {
            systemIt->second.stop();
        }
    }
    for (PendingMaterial& pending : pendingMaterials) {
        pending.material.baseColor = glm::clamp(
            pending.material.baseColor,
            glm::vec4(0.0f),
            glm::vec4(1.0f)
        );
        pending.material.metallic = std::clamp(pending.material.metallic, 0.0f, 1.0f);
        pending.material.roughness = std::clamp(pending.material.roughness, 0.04f, 1.0f);
        pending.material.opacity = std::clamp(pending.material.opacity, 0.0f, 1.0f);
        pending.material.emissionColor = glm::clamp(
            pending.material.emissionColor,
            glm::vec3(0.0f),
            glm::vec3(1.0f)
        );
        pending.material.emissionStrength = std::clamp(
            pending.material.emissionStrength,
            0.0f,
            20.0f
        );
        if (glm::length(pending.material.waterShaderSettings.flowDirection) < 0.001f) {
            pending.material.waterShaderSettings.flowDirection = glm::vec2(0.0f, 1.0f);
        }
        pending.material.waterShaderSettings.flowSpeed = std::clamp(
            pending.material.waterShaderSettings.flowSpeed, 0.0f, 4.0f);
        pending.material.waterShaderSettings.flowStrength = std::clamp(
            pending.material.waterShaderSettings.flowStrength, 0.0f, 4.0f);
        pending.material.waterShaderSettings.flowTurbulence = std::clamp(
            pending.material.waterShaderSettings.flowTurbulence, 0.0f, 1.0f);
        pending.material.waterShaderSettings.waveScale = std::clamp(
            pending.material.waterShaderSettings.waveScale, 0.1f, 8.0f);
        pending.material.waterShaderSettings.waveHeight = std::clamp(
            pending.material.waterShaderSettings.waveHeight, 0.0f, 0.2f);
        if (pending.materialIndex < meshData_.materials.size()) {
            // Older material records predate the normal/specular texture
            // fields. Preserve the imported defaults unless a separate
            // material_textures record explicitly overrides them.
            const MaterialData& importedMaterial = meshData_.materials[pending.materialIndex];
            if (pending.material.normalTexturePath.empty() &&
                pending.material.embeddedNormalTexture.empty()) {
                pending.material.normalTexturePath = importedMaterial.normalTexturePath;
                pending.material.embeddedNormalTexture = importedMaterial.embeddedNormalTexture;
            }
            if (pending.material.specularTexturePath.empty() &&
                pending.material.embeddedSpecularTexture.empty()) {
                pending.material.specularTexturePath = importedMaterial.specularTexturePath;
                pending.material.embeddedSpecularTexture = importedMaterial.embeddedSpecularTexture;
            }
            meshData_.materials[pending.materialIndex] = pending.material;
        } else if (pending.materialIndex == meshData_.materials.size()) {
            meshData_.materials.push_back(std::move(pending.material));
        }
    }
    for (const PendingMaterialTextures& pending : pendingMaterialTextures) {
        if (pending.materialIndex >= meshData_.materials.size()) {
            continue;
        }
        MaterialData& material = meshData_.materials[pending.materialIndex];
        material.normalTexturePath = pending.normalTexturePath;
        material.specularTexturePath = pending.specularTexturePath;
        material.embeddedNormalTexture = EmbeddedTextureData{};
        material.embeddedSpecularTexture = EmbeddedTextureData{};
    }
    const bool hasRootNodeTransform = std::any_of(
        pendingNodeTransforms.begin(),
        pendingNodeTransforms.end(),
        [this](const PendingNodeTransform& transform) {
            return transform.nodeId == meshData_.hierarchy.nodeId;
        }
    );
    if (hasLegacyTransformSettings && !hasRootNodeTransform) {
        ModelNode& rootNode = meshData_.hierarchy;
        rootNode.transformTranslation = legacyTranslation;
        rootNode.transformRotationDegrees = legacyRotationDegrees;
        rootNode.transformScaleFactor = std::clamp(
            legacyScaleFactor,
            kMinimumModelScaleFactor,
            kMaximumModelScaleFactor
        );
    }
    for (const PendingNodeTransform& transform : pendingNodeTransforms) {
        ModelNode* node = findSceneNodeById(meshData_, transform.nodeId);
        if (node == nullptr) {
            continue;
        }
        node->transformTranslation = transform.translation;
        node->transformRotationDegrees = transform.rotationDegrees;
        node->transformScaleFactor = std::clamp(
            transform.scaleFactor,
            kMinimumModelScaleFactor,
            kMaximumModelScaleFactor
        );
    }
    for (const PendingNodeAnimation& animation : pendingNodeAnimations) {
        ModelNode* node = findSceneNodeById(meshData_, animation.nodeId);
        if (node == nullptr) {
            continue;
        }
        node->rotationAnimationEnabled = animation.enabled;
        node->rotationAnimationInitialDegrees = animation.initialDegrees;
        node->rotationAnimationSpeedDegrees = animation.speedDegrees;
    }
    for (const PendingNodeMaterial& material : pendingNodeMaterials) {
        ModelNode* node = findSceneNodeById(meshData_, material.nodeId);
        if (node == nullptr) {
            continue;
        }
        if (material.materialIndex >= 0 &&
            static_cast<std::size_t>(material.materialIndex) >= meshData_.materials.size()) {
            continue;
        }
        node->materialOverride = material.materialIndex;
    }

    // Migrate the previous node-owned shader/water settings into a material
    // so existing projects keep their appearance after shader ownership moves
    // from ModelNode to MaterialData.
    std::unordered_set<std::uint32_t> legacyNodeIds;
    for (const PendingNodeShaders& shaders : pendingNodeShaders) {
        legacyNodeIds.insert(shaders.nodeId);
    }
    for (const PendingNodeWater& water : pendingNodeWater) {
        legacyNodeIds.insert(water.nodeId);
    }
    for (const std::uint32_t nodeId : legacyNodeIds) {
        ModelNode* node = findSceneNodeById(meshData_, nodeId);
        if (node == nullptr) {
            continue;
        }
        const auto shaderIt = std::find_if(
            pendingNodeShaders.begin(),
            pendingNodeShaders.end(),
            [nodeId](const PendingNodeShaders& shaders) {
                return shaders.nodeId == nodeId;
            }
        );
        const auto waterIt = std::find_if(
            pendingNodeWater.begin(),
            pendingNodeWater.end(),
            [nodeId](const PendingNodeWater& water) {
                return water.nodeId == nodeId;
            }
        );

        std::int32_t materialIndex = node->materialOverride;
        if (materialIndex < 0 ||
            static_cast<std::size_t>(materialIndex) >= meshData_.materials.size()) {
            const bool hasSourcePart = !node->meshPartIndices.empty() &&
                node->meshPartIndices.front() < meshData_.parts.size();
            const std::uint32_t sourceMaterialIndex = hasSourcePart
                ? meshData_.parts[node->meshPartIndices.front()].materialIndex
                : 0;
            MaterialData migratedMaterial = sourceMaterialIndex < meshData_.materials.size()
                ? meshData_.materials[sourceMaterialIndex]
                : MaterialData{};
            migratedMaterial.name = "MigratedMaterial_" + std::to_string(nodeId);
            migratedMaterial.userCreated = true;
            migratedMaterial.userModified = false;
            meshData_.materials.push_back(std::move(migratedMaterial));
            materialIndex = static_cast<std::int32_t>(meshData_.materials.size() - 1);
            node->materialOverride = materialIndex;
        }

        MaterialData& material = meshData_.materials[materialIndex];
        if (shaderIt != pendingNodeShaders.end() && material.shaderConfiguration.empty()) {
            material.shaderConfiguration = shaderIt->configuration;
        }
        if (waterIt != pendingNodeWater.end()) {
            material.waterShaderSettings = waterIt->settings;
        }
    }
}

void Renderer::saveTransformSettings() const {
    const std::filesystem::path configPath(kConfigPath);
    std::ofstream file(configPath, std::ios::trunc);
    if (!file.is_open()) {
        std::cerr << "warning: failed to save VulkCore transform settings to '"
                  << configPath.string() << "'\n";
        return;
    }

    file << "# VulkCore transform settings\n"
         << std::setprecision(9)
         << "ambient_color " << ambientLightColor_.r << ' '
         << ambientLightColor_.g << ' ' << ambientLightColor_.b << "\n"
         << "ambient_intensity " << ambientLightIntensity_ << "\n"
         << "sun_light_direction " << sunLightDirection_.x << ' '
         << sunLightDirection_.y << ' ' << sunLightDirection_.z << "\n"
         << "sun_light_color " << sunLightColor_.r << ' '
         << sunLightColor_.g << ' ' << sunLightColor_.b << "\n"
         << "sun_light_intensity " << sunLightIntensity_ << "\n";
    for (const ModelNode& node : meshData_.additionalNodes) {
        if (node.objectType != SceneObjectType::ParticleSystem) {
            continue;
        }
        const auto systemIt = particleSystems_.find(node.nodeId);
        if (systemIt != particleSystems_.end()) {
            saveParticleSystemSettings(file, node.nodeId, systemIt->second.settings());
        }
    }
    saveNodeTransformSettings(file, meshData_.hierarchy);
    for (const ModelNode& node : meshData_.additionalNodes) {
        saveNodeTransformSettings(file, node);
    }
    saveNodeAnimationSettings(file, meshData_.hierarchy);
    for (const ModelNode& node : meshData_.additionalNodes) {
        saveNodeAnimationSettings(file, node);
    }
    saveUserMaterials(file, meshData_.materials);
    saveNodeMaterialSettings(file, meshData_.hierarchy);
    for (const ModelNode& node : meshData_.additionalNodes) {
        saveNodeMaterialSettings(file, node);
    }
}

void Renderer::createParticleSystemObject() {
    if (particleSystems_.size() >= kMaximumParticleSystems) {
        return;
    }
    std::uint32_t maximumNodeId = maximumModelNodeId(meshData_.hierarchy);
    for (const ModelNode& node : meshData_.additionalNodes) {
        maximumNodeId = std::max(maximumNodeId, maximumModelNodeId(node));
    }

    ModelNode particleNode;
    particleNode.name = "Particle System " + std::to_string(maximumNodeId + 1);
    particleNode.nodeId = maximumNodeId + 1;
    particleNode.objectType = SceneObjectType::ParticleSystem;
    meshData_.additionalNodes.push_back(std::move(particleNode));
    ModelNode& createdNode = meshData_.additionalNodes.back();
    particleSystems_.try_emplace(createdNode.nodeId);
    createParticleDescriptorSets(createdNode.nodeId);
    selectModelNode(createdNode);
    saveTransformSettings();
}

void Renderer::drawParticleSystemControls(ModelNode& node, bool& shouldSaveSettings) {
    const auto systemIt = particleSystems_.find(node.nodeId);
    if (systemIt == particleSystems_.end()) {
        ImGui::TextDisabled("Particle component is not available for this object.");
        return;
    }
    ParticleSystem& particleSystem = systemIt->second;
    ParticleSystemSettings& settings = particleSystem.settings();
    bool settingsChanged = false;
    if (particleTexturePathNodeId_ != node.nodeId) {
        particleTexturePathNodeId_ = node.nodeId;
        particleTexturePathBuffer_.fill('\0');
        const std::string path = settings.texturePath.string();
        const std::size_t copyCount = std::min(
            path.size(),
            particleTexturePathBuffer_.size() - 1
        );
        std::copy_n(path.data(), copyCount, particleTexturePathBuffer_.data());
    }

    ImGui::SeparatorText("Particle System");
    ImGui::Text("GameObject: %s", node.name.c_str());
    ImGui::TextDisabled("CPU simulation + camera-facing billboard batch");

    ImGui::SeparatorText("GameObject Transform");
    ImGui::DragFloat3(
        "Object position",
        &node.transformTranslation.x,
        0.01f,
        -2.0f,
        2.0f,
        "%.2f"
    );
    shouldSaveSettings |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::DragFloat3(
        "Object rotation",
        &node.transformRotationDegrees.x,
        1.0f,
        -360.0f,
        360.0f,
        "%.1f deg"
    );
    shouldSaveSettings |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat(
        "Object scale",
        &node.transformScaleFactor,
        kMinimumModelScaleFactor,
        kMaximumModelScaleFactor,
        "%.2fx"
    );
    shouldSaveSettings |= ImGui::IsItemDeactivatedAfterEdit();

    bool enabled = settings.enabled;
    if (ImGui::Checkbox("Enabled", &enabled)) {
        particleSystem.setEnabled(enabled);
        shouldSaveSettings = true;
    }
    ImGui::SameLine();
    ImGui::Text("Alive: %zu", particleSystem.aliveCount());
    bool playOnAwake = settings.playOnAwake;
    if (ImGui::Checkbox("Play on awake", &playOnAwake)) {
        settings.playOnAwake = playOnAwake;
        shouldSaveSettings = true;
    }
    bool looping = settings.looping;
    if (ImGui::Checkbox("Looping", &looping)) {
        settings.looping = looping;
        settingsChanged = true;
    }
    if (ImGui::Button("Play")) {
        particleSystem.play();
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        particleSystem.stop();
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart")) {
        particleSystem.restart();
    }

    int maxParticles = static_cast<int>(settings.maxParticles);
    if (ImGui::SliderInt("Max particles", &maxParticles, 1, static_cast<int>(
            ParticleSystem::kMaximumSupportedParticles))) {
        settings.maxParticles = static_cast<std::uint32_t>(maxParticles);
        settingsChanged = true;
    }
    ImGui::SliderFloat("Duration", &settings.duration, 0.05f, 60.0f, "%.2f s");
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Emission rate", &settings.emissionRate, 0.0f, 200.0f, "%.1f /s");
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::SeparatorText("Main Module");
    ImGui::SliderFloat("Start lifetime", &settings.startLifetime, 0.02f, 20.0f, "%.2f s");
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Lifetime randomness", &settings.lifetimeRandomness, 0.0f, 1.0f, "%.2f");
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Start speed", &settings.startSpeed, 0.0f, 8.0f, "%.2f");
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Speed randomness", &settings.speedRandomness, 0.0f, 1.0f, "%.2f");
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Gravity", &settings.gravityModifier, -4.0f, 4.0f, "%.2f");
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::DragFloat3(
        "Position",
        &settings.position.x,
        0.01f,
        -2.0f,
        2.0f,
        "%.2f"
    );
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::SeparatorText("Shape");
    const char* shapeLabels[] = {"Point", "Sphere", "Cone"};
    int shape = static_cast<int>(settings.shape);
    if (ImGui::Combo("Emitter shape", &shape, shapeLabels, IM_ARRAYSIZE(shapeLabels))) {
        settings.shape = static_cast<ParticleSystemSettings::Shape>(shape);
        settingsChanged = true;
    }
    ImGui::SliderFloat("Spread", &settings.spreadDegrees, 0.0f, 89.0f, "%.1f deg");
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::DragFloat3(
        "Direction",
        &settings.emissionDirection.x,
        0.01f,
        -1.0f,
        1.0f,
        "%.2f"
    );
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Shape radius", &settings.shapeRadius, 0.0f, 1.0f, "%.2f");
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::SeparatorText("Over Lifetime");
    ImGui::SliderFloat("Start size", &settings.startSize, 0.001f, 0.5f, "%.3f");
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("End size", &settings.endSize, 0.001f, 0.5f, "%.3f");
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat(
        "Rotation speed",
        &settings.rotationSpeedDegrees,
        -360.0f,
        360.0f,
        "%.1f deg/s"
    );
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::ColorEdit4("Start color", &settings.startColor.x);
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::ColorEdit4("End color", &settings.endColor.x);
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();

    const char* blendLabels[] = {"Alpha", "Additive"};
    int blendMode = static_cast<int>(settings.blendMode);
    if (ImGui::Combo("Blend mode", &blendMode, blendLabels, IM_ARRAYSIZE(blendLabels))) {
        settings.blendMode = static_cast<ParticleSystemSettings::BlendMode>(blendMode);
        settingsChanged = true;
    }

    ImGui::SeparatorText("Renderer");
    ImGui::InputText(
        "Particle texture",
        particleTexturePathBuffer_.data(),
        particleTexturePathBuffer_.size()
    );
    if (ImGui::Button("Load particle texture")) {
        settings.texturePath = particleTexturePathBuffer_.data();
        loadParticleTexture(node.nodeId);
        shouldSaveSettings = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear particle texture")) {
        settings.texturePath.clear();
        particleTexturePathBuffer_.fill('\0');
        loadParticleTexture(node.nodeId);
        shouldSaveSettings = true;
    }
    if (particleTextures_.contains(node.nodeId)) {
        ImGui::TextDisabled("Texture loaded; RGBA is multiplied by particle color.");
    } else {
        ImGui::TextDisabled("No valid texture; using the soft-circle fallback.");
    }
    if (ImGui::Button("Reset particle settings")) {
        particleSystem.setSettings(ParticleSystemSettings{});
        particleTexturePathBuffer_.fill('\0');
        loadParticleTexture(node.nodeId);
        shouldSaveSettings = true;
    }
    if (settingsChanged) {
        particleSystem.setSettings(ParticleSystem::sanitizeSettings(settings));
        shouldSaveSettings = true;
    }
}

void Renderer::buildImGuiFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    constexpr ImGuiWindowFlags kMainWindowFlags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;

    ImGui::Begin("VulkCore Main Screen", nullptr, kMainWindowFlags);
    constexpr float kPanelMargin = 16.0f;
    constexpr float kLeftPanelWidth = 300.0f;
    constexpr float kRightPanelWidth = 360.0f;

    ImGui::SetCursorPos(ImVec2(kPanelMargin, kPanelMargin));
    ImGui::BeginChild("SceneControls", ImVec2(kLeftPanelWidth, 0.0f), true);
    ImGui::TextUnformatted("VulkCore");
    ImGui::TextDisabled("Vulkan scene rendered with Dear ImGui UI");
    ImGui::Separator();
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Text("Frame time: %.2f ms", 1000.0f / std::max(ImGui::GetIO().Framerate, 0.001f));
    bool shouldSaveTransformSettings = false;
    ImGui::SeparatorText("Environment Light");
    ImGui::ColorEdit3("Ambient color", &ambientLightColor_.x);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        ambientLightColor_ = glm::clamp(
            ambientLightColor_,
            glm::vec3(0.0f),
            glm::vec3(1.0f)
        );
        shouldSaveTransformSettings = true;
    }
    ImGui::SliderFloat(
        "Ambient intensity",
        &ambientLightIntensity_,
        0.0f,
        4.0f,
        "%.2f"
    );
    shouldSaveTransformSettings |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::ColorEdit3("Sun light color", &sunLightColor_.x);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        sunLightColor_ = glm::clamp(sunLightColor_, glm::vec3(0.0f), glm::vec3(1.0f));
        shouldSaveTransformSettings = true;
    }
    ImGui::SliderFloat("Sun light intensity", &sunLightIntensity_, 0.0f, 4.0f, "%.2f");
    shouldSaveTransformSettings |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::DragFloat3(
        "Sun light direction",
        &sunLightDirection_.x,
        0.01f,
        -1.0f,
        1.0f,
        "%.2f"
    );
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        if (glm::length(sunLightDirection_) < 0.001f) {
            sunLightDirection_ = glm::vec3(0.45f, 0.55f, 0.75f);
        } else {
            sunLightDirection_ = glm::normalize(sunLightDirection_);
        }
        shouldSaveTransformSettings = true;
    }
    if (ImGui::Button("Reset environment light")) {
        ambientLightColor_ = glm::vec3(0.18f, 0.21f, 0.24f);
        ambientLightIntensity_ = 1.0f;
        sunLightDirection_ = glm::vec3(0.45f, 0.55f, 0.75f);
        sunLightColor_ = glm::vec3(0.82f, 0.86f, 0.9f);
        sunLightIntensity_ = 1.0f;
        shouldSaveTransformSettings = true;
    }
    ImGui::SeparatorText("Transform");
    if (selectedModelNode_ != nullptr) {
        ImGui::SeparatorText("Selected Node");
        ImGui::Text("Node: %s", selectedModelNode_->name.c_str());
        if (selectedModelNode_->objectType == SceneObjectType::ParticleSystem) {
            drawParticleSystemControls(*selectedModelNode_, shouldSaveTransformSettings);
        } else {
            if (selectedModelNode_->nodeId == sunNodeId_) {
                ImGui::TextDisabled("Generated spherical sun mesh");
            }
            const bool isNamedMeshNode = selectedModelNode_->meshCount > 0 &&
                !selectedModelNode_->name.empty() &&
                selectedModelNode_->name != "<unnamed>";
            if (isNamedMeshNode) {
                ImGui::TextDisabled("Position is in scene units; rotation and scale use local axes.");
                ImGui::DragFloat3(
                    "Position (X/Y/Z)",
                    &selectedModelNode_->transformTranslation.x,
                    0.01f,
                    -10.0f,
                    10.0f,
                    "%.2f"
                );
                shouldSaveTransformSettings |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::DragFloat3(
                    "Rotation (X/Y/Z)",
                    &selectedModelNode_->transformRotationDegrees.x,
                    1.0f,
                    -360.0f,
                    360.0f,
                    "%.1f deg"
                );
                shouldSaveTransformSettings |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::SliderFloat(
                    "Scale",
                    &selectedModelNode_->transformScaleFactor,
                    kMinimumModelScaleFactor,
                    kMaximumModelScaleFactor,
                    "%.2fx"
                );
                shouldSaveTransformSettings |= ImGui::IsItemDeactivatedAfterEdit();
                if (ImGui::Button("Reset selected transform")) {
                    selectedModelNode_->transformTranslation = glm::vec3(0.0f);
                    selectedModelNode_->transformRotationDegrees = glm::vec3(0.0f);
                    selectedModelNode_->transformScaleFactor = 1.0f;
                    shouldSaveTransformSettings = true;
                }
            } else {
                ImGui::TextDisabled("Transform is available for named mesh nodes only.");
            }

            ImGui::SeparatorText("Node Animation");
            ImGui::Checkbox(
                "Enable rotation animation",
                &selectedModelNode_->rotationAnimationEnabled
            );
            shouldSaveTransformSettings |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DragFloat3(
                "Initial rotation (X/Y/Z)",
                &selectedModelNode_->rotationAnimationInitialDegrees.x,
                1.0f,
                -360.0f,
                360.0f,
                "%.1f deg"
            );
            shouldSaveTransformSettings |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DragFloat3(
                "Rotation speed (X/Y/Z)",
                &selectedModelNode_->rotationAnimationSpeedDegrees.x,
                1.0f,
                -360.0f,
                360.0f,
                "%.1f deg/s"
            );
            shouldSaveTransformSettings |= ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::Button("Reset node animation")) {
                selectedModelNode_->rotationAnimationEnabled = false;
                selectedModelNode_->rotationAnimationInitialDegrees = glm::vec3(0.0f);
                selectedModelNode_->rotationAnimationSpeedDegrees = glm::vec3(0.0f);
                shouldSaveTransformSettings = true;
            }
            drawNodeMaterialControls(*selectedModelNode_, shouldSaveTransformSettings);
        }
    } else {
        ImGui::TextDisabled("Select a scene object to edit its transform.");
    }
    if (shouldSaveTransformSettings) {
        saveTransformSettings();
    }
    ImGui::Spacing();
    ImGui::TextWrapped("The 3D model remains the main viewport and is rendered before this transparent ImGui layer.");
    const bool leftPanelHovered = ImGui::IsWindowHovered();
    ImGui::EndChild();

    ImGui::SetCursorPos(ImVec2(
        std::max(kPanelMargin, ImGui::GetWindowWidth() - kRightPanelWidth - kPanelMargin),
        kPanelMargin
    ));
    ImGui::BeginChild("ModelHierarchy", ImVec2(kRightPanelWidth, 0.0f), true);
    ImGui::TextUnformatted("Scene Objects");
    ImGui::TextDisabled("Imported model and generated GameObjects");
    const bool canCreateParticleSystem = particleSystems_.size() < kMaximumParticleSystems;
    ImGui::BeginDisabled(!canCreateParticleSystem);
    if (ImGui::Button("Create Particle System")) {
        createParticleSystemObject();
    }
    ImGui::EndDisabled();
    if (!canCreateParticleSystem) {
        ImGui::TextDisabled("Particle System limit reached (%u)", kMaximumParticleSystems);
    }
    ImGui::Separator();
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (!meshData_.hierarchy.name.empty()) {
        drawModelHierarchy(meshData_.hierarchy);
    }
    for (ModelNode& node : meshData_.additionalNodes) {
        drawModelHierarchy(node);
    }
    if (meshData_.hierarchy.name.empty() && meshData_.additionalNodes.empty()) {
        ImGui::TextDisabled("No model hierarchy available");
    }
    const bool rightPanelHovered = ImGui::IsWindowHovered();
    ImGui::EndChild();

    if (selectedModelNode_ != nullptr &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !leftPanelHovered &&
        !rightPanelHovered &&
        ImGui::IsWindowHovered()) {
        selectedModelNode_ = nullptr;
        updateBoundingBoxVertices();
    }
    ImGui::End();

    ImGui::Render();
}

void Renderer::createDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT |
        VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
        VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT |
        VK_SHADER_STAGE_GEOMETRY_BIT |
        VK_SHADER_STAGE_FRAGMENT_BIT |
        VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT |
        VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
        VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT |
        VK_SHADER_STAGE_GEOMETRY_BIT |
        VK_SHADER_STAGE_FRAGMENT_BIT |
        VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(
            context_->device(),
            &layoutInfo,
            nullptr,
            &descriptorSetLayout_
        ) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout!");
    }
}

void Renderer::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapChain_->imageFormat();
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentReference{};
    colorAttachmentReference.attachment = 0;
    colorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = swapChain_->depthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentReference{};
    depthAttachmentReference.attachment = 1;
    depthAttachmentReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentReference;
    subpass.pDepthStencilAttachment = &depthAttachmentReference;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    const std::array<VkAttachmentDescription, 2> attachments = {
        colorAttachment,
        depthAttachment
    };

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(context_->device(), &renderPassInfo, nullptr, &renderPass_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass!");
    }
}

void Renderer::createSceneRenderResources() {
    const VkExtent2D extent = swapChain_->extent();

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapChain_->imageFormat();
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorReference{};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = swapChain_->depthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthReference{};
    depthReference.attachment = 1;
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;

    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dependencies[0].dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].srcAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask =
        VK_PIPELINE_STAGE_TRANSFER_BIT |
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].dstAccessMask =
        VK_ACCESS_TRANSFER_READ_BIT |
        VK_ACCESS_SHADER_READ_BIT;

    const std::array<VkAttachmentDescription, 2> attachments = {
        colorAttachment,
        depthAttachment
    };
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();
    if (vkCreateRenderPass(context_->device(), &renderPassInfo, nullptr, &sceneRenderPass_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create scene render pass!");
    }

    const auto createImage = [&](VkFormat format,
                                 VkImageUsageFlags usage,
                                 VkImage& image,
                                 VkDeviceMemory& memory) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = {extent.width, extent.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = usage;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (vkCreateImage(context_->device(), &imageInfo, nullptr, &image) != VK_SUCCESS) {
            throw std::runtime_error("failed to create scene render image!");
        }
        try {
            VkMemoryRequirements memoryRequirements{};
            vkGetImageMemoryRequirements(context_->device(), image, &memoryRequirements);

            VkMemoryAllocateInfo allocateInfo{};
            allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocateInfo.allocationSize = memoryRequirements.size;
            allocateInfo.memoryTypeIndex = context_->findMemoryType(
                memoryRequirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            );
            if (vkAllocateMemory(context_->device(), &allocateInfo, nullptr, &memory) != VK_SUCCESS) {
                throw std::runtime_error("failed to allocate scene render image memory!");
            }
            if (vkBindImageMemory(context_->device(), image, memory, 0) != VK_SUCCESS) {
                throw std::runtime_error("failed to bind scene render image memory!");
            }
        } catch (...) {
            if (memory != VK_NULL_HANDLE) {
                vkFreeMemory(context_->device(), memory, nullptr);
                memory = VK_NULL_HANDLE;
            }
            vkDestroyImage(context_->device(), image, nullptr);
            image = VK_NULL_HANDLE;
            throw;
        }
    };

    const auto createImageView = [&](VkImage image, VkFormat format, VkImageAspectFlags aspect) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspect;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView imageView = VK_NULL_HANDLE;
        if (vkCreateImageView(context_->device(), &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
            throw std::runtime_error("failed to create scene render image view!");
        }
        return imageView;
    };

    try {
        sceneRenderTargets_.resize(kMaxFramesInFlight);
        for (SceneRenderTarget& target : sceneRenderTargets_) {
            createImage(
                swapChain_->imageFormat(),
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT |
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                target.colorImage,
                target.colorMemory
            );
            target.colorImageView = createImageView(
                target.colorImage,
                swapChain_->imageFormat(),
                VK_IMAGE_ASPECT_COLOR_BIT
            );

            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.maxAnisotropy = 1.0f;
            samplerInfo.maxLod = 0.0f;
            if (vkCreateSampler(context_->device(), &samplerInfo, nullptr, &target.colorSampler) != VK_SUCCESS) {
                throw std::runtime_error("failed to create scene render sampler!");
            }

            createImage(
                swapChain_->depthFormat(),
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                target.depthImage,
                target.depthMemory
            );
            target.depthImageView = createImageView(
                target.depthImage,
                swapChain_->depthFormat(),
                VK_IMAGE_ASPECT_DEPTH_BIT
            );

            const std::array<VkImageView, 2> framebufferAttachments = {
                target.colorImageView,
                target.depthImageView
            };
            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = sceneRenderPass_;
            framebufferInfo.attachmentCount = static_cast<uint32_t>(framebufferAttachments.size());
            framebufferInfo.pAttachments = framebufferAttachments.data();
            framebufferInfo.width = extent.width;
            framebufferInfo.height = extent.height;
            framebufferInfo.layers = 1;
            if (vkCreateFramebuffer(
                    context_->device(),
                    &framebufferInfo,
                    nullptr,
                    &target.framebuffer
                ) != VK_SUCCESS) {
                throw std::runtime_error("failed to create scene render framebuffer!");
            }
        }
    } catch (...) {
        destroySceneRenderResources();
        throw;
    }
}

void Renderer::destroySceneRenderResources() {
    if (!context_) {
        return;
    }
    for (SceneRenderTarget& target : sceneRenderTargets_) {
        if (target.framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(context_->device(), target.framebuffer, nullptr);
        }
        if (target.colorSampler != VK_NULL_HANDLE) {
            vkDestroySampler(context_->device(), target.colorSampler, nullptr);
        }
        if (target.colorImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(context_->device(), target.colorImageView, nullptr);
        }
        if (target.depthImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(context_->device(), target.depthImageView, nullptr);
        }
        if (target.colorImage != VK_NULL_HANDLE) {
            vkDestroyImage(context_->device(), target.colorImage, nullptr);
        }
        if (target.depthImage != VK_NULL_HANDLE) {
            vkDestroyImage(context_->device(), target.depthImage, nullptr);
        }
        if (target.colorMemory != VK_NULL_HANDLE) {
            vkFreeMemory(context_->device(), target.colorMemory, nullptr);
        }
        if (target.depthMemory != VK_NULL_HANDLE) {
            vkFreeMemory(context_->device(), target.depthMemory, nullptr);
        }
        target = SceneRenderTarget{};
    }
    sceneRenderTargets_.clear();
    if (sceneRenderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(context_->device(), sceneRenderPass_, nullptr);
        sceneRenderPass_ = VK_NULL_HANDLE;
    }
}

void Renderer::discoverShaderBinaries() {
    for (auto& shaders : availableShaders_) {
        shaders.clear();
    }

    const std::filesystem::path shaderDirectory(kShaderBinaryDirectory);
    if (!std::filesystem::exists(shaderDirectory)) {
        throw std::runtime_error("shader binary directory does not exist: " + shaderDirectory.string());
    }

    for (const auto& entry : std::filesystem::directory_iterator(shaderDirectory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".spv") {
            continue;
        }

        std::string shaderName = entry.path().filename().string();
        constexpr std::string_view kSpirvSuffix = ".spv";
        if (!shaderName.ends_with(kSpirvSuffix)) {
            continue;
        }
        shaderName.erase(shaderName.size() - kSpirvSuffix.size());

        ShaderStage stage = ShaderStage::Vertex;
        if (!shaderStageForExtension(std::filesystem::path(shaderName).extension().string(), stage)) {
            continue;
        }
        availableShaders_[static_cast<std::size_t>(stage)].push_back(std::move(shaderName));
    }

    for (auto& shaders : availableShaders_) {
        std::sort(shaders.begin(), shaders.end());
    }
}

VkShaderModule Renderer::shaderModule(const std::string& shaderName) {
    const auto existing = shaderModules_.find(shaderName);
    if (existing != shaderModules_.end()) {
        return existing->second;
    }

    const std::filesystem::path shaderPath =
        std::filesystem::path(kShaderBinaryDirectory) / (shaderName + ".spv");
    const VkShaderModule module = createShaderModule(readBinaryFile(shaderPath));
    shaderModules_.emplace(shaderName, module);
    return module;
}

std::string Renderer::graphicsPipelineKey(
    const ShaderConfiguration& configuration,
    const MaterialData& material
) const {
    std::string key;
    for (const ShaderStage stage : {
             ShaderStage::Vertex,
             ShaderStage::TessellationControl,
             ShaderStage::TessellationEvaluation,
             ShaderStage::Geometry,
             ShaderStage::Fragment
         }) {
        key += configuration.shader(stage);
        key += '\n';
    }
    key += "blend=" + std::to_string(static_cast<int>(material.blendMode));
    key += ";depthWrite=" + std::to_string(material.depthWrite ? 1 : 0);
    key += ";doubleSided=" + std::to_string(material.doubleSided ? 1 : 0);
    return key;
}

VkPipeline Renderer::createMaterialGraphicsPipeline(
    const ShaderConfiguration& configuration,
    const MaterialData& material
) {
    const bool hasTessellationControl =
        !configuration.shader(ShaderStage::TessellationControl).empty();
    const bool hasTessellationEvaluation =
        !configuration.shader(ShaderStage::TessellationEvaluation).empty();
    if (hasTessellationControl != hasTessellationEvaluation) {
        throw std::runtime_error(
            "tessellation control and evaluation shaders must be configured together"
        );
    }
    const bool isWaterShader = isWaterShaderConfiguration(configuration);

    const std::array<ShaderStage, 5> graphicsStages = {
        ShaderStage::Vertex,
        ShaderStage::TessellationControl,
        ShaderStage::TessellationEvaluation,
        ShaderStage::Geometry,
        ShaderStage::Fragment
    };
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    shaderStages.reserve(graphicsStages.size());
    for (const ShaderStage stage : graphicsStages) {
        std::string shaderName = configuration.shader(stage);
        if (shaderName.empty()) {
            shaderName = defaultShaderForStage(stage);
        }
        if (shaderName.empty()) {
            continue;
        }

        const auto& stageShaders = availableShaders_[static_cast<std::size_t>(stage)];
        if (std::find(stageShaders.begin(), stageShaders.end(), shaderName) == stageShaders.end()) {
            throw std::runtime_error(
                std::string("shader is not available for ") + shaderStageLabel(stage) + ": " + shaderName
            );
        }

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = vkShaderStage(stage);
        stageInfo.module = shaderModule(shaderName);
        stageInfo.pName = "main";
        shaderStages.push_back(stageInfo);
    }

    const auto bindingDescription = Vertex::bindingDescription();
    const auto attributeDescriptions = Vertex::attributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = hasTessellationControl
        ? VK_PRIMITIVE_TOPOLOGY_PATCH_LIST
        : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = material.doubleSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = isWaterShader
        ? VK_FALSE
        : (material.depthWrite ? VK_TRUE : VK_FALSE);
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    const bool enableBlend = isWaterShader || isTransparentMaterial(material);
    colorBlendAttachment.blendEnable = enableBlend ? VK_TRUE : VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    if (material.blendMode == MaterialData::BlendMode::Additive) {
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    } else if (material.blendMode == MaterialData::BlendMode::Multiply) {
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    }
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    const std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineTessellationStateCreateInfo tessellationState{};
    tessellationState.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
    tessellationState.patchControlPoints = 3;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pTessellationState = hasTessellationControl ? &tessellationState : nullptr;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(
            context_->device(),
            VK_NULL_HANDLE,
            1,
            &pipelineInfo,
            nullptr,
            &pipeline
        ) != VK_SUCCESS) {
        throw std::runtime_error("failed to create node graphics pipeline");
    }
    return pipeline;
}

VkPipeline Renderer::graphicsPipelineForMaterial(const MaterialData& material) {
    const bool isDefaultMaterialState =
        material.blendMode == MaterialData::BlendMode::Opaque &&
        material.depthWrite &&
        !material.doubleSided;
    if (!material.shaderConfiguration.hasGraphicsOverride() && isDefaultMaterialState) {
        return graphicsPipeline_;
    }

    const std::string key = graphicsPipelineKey(material.shaderConfiguration, material);
    const auto existing = nodeGraphicsPipelines_.find(key);
    if (existing != nodeGraphicsPipelines_.end()) {
        return existing->second;
    }
    if (failedNodeGraphicsPipelineKeys_.contains(key)) {
        return graphicsPipeline_;
    }

    try {
        const VkPipeline pipeline = createMaterialGraphicsPipeline(material.shaderConfiguration, material);
        nodeGraphicsPipelines_.emplace(key, pipeline);
        return pipeline;
    } catch (const std::exception& error) {
        failedNodeGraphicsPipelineKeys_.insert(key);
        std::cerr << "warning: material '" << material.name
                  << "' shader pipeline failed, using default shaders: " << error.what() << '\n';
        return graphicsPipeline_;
    }
}

std::uint32_t Renderer::materialIndexForPart(const MeshPart& part) const {
    const std::int32_t overrideIndex = inheritedMaterialOverrideForSceneNode(part.nodeId);
    if (overrideIndex >= 0 &&
        static_cast<std::size_t>(overrideIndex) < meshData_.materials.size()) {
        return static_cast<std::uint32_t>(overrideIndex);
    }
    return part.materialIndex;
}

VkPipeline Renderer::computePipelineForMaterial(const MaterialData& material) {
    const std::string& computeShaderName = material.shaderConfiguration.shader(ShaderStage::Compute);
    if (computeShaderName.empty()) {
        return VK_NULL_HANDLE;
    }
    const auto existing = nodeComputePipelines_.find(computeShaderName);
    if (existing != nodeComputePipelines_.end()) {
        return existing->second;
    }
    if (failedNodeComputePipelineKeys_.contains(computeShaderName)) {
        return VK_NULL_HANDLE;
    }

    try {
        const auto& computeShaders = availableShaders_[static_cast<std::size_t>(ShaderStage::Compute)];
        if (std::find(computeShaders.begin(), computeShaders.end(), computeShaderName) == computeShaders.end()) {
            throw std::runtime_error("compute shader is not available: " + computeShaderName);
        }

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule(computeShaderName);
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = pipelineLayout_;

        VkPipeline pipeline = VK_NULL_HANDLE;
        if (vkCreateComputePipelines(
                context_->device(),
                VK_NULL_HANDLE,
                1,
                &pipelineInfo,
                nullptr,
                &pipeline
            ) != VK_SUCCESS) {
            throw std::runtime_error("failed to create node compute pipeline");
        }
        nodeComputePipelines_.emplace(computeShaderName, pipeline);
        return pipeline;
    } catch (const std::exception& error) {
        failedNodeComputePipelineKeys_.insert(computeShaderName);
        std::cerr << "warning: material '" << material.name
                  << "' compute shader pipeline failed: " << error.what() << '\n';
        return VK_NULL_HANDLE;
    }
}

void Renderer::createGraphicsPipeline() {
    discoverShaderBinaries();

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout_;
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT |
        VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
        VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT |
        VK_SHADER_STAGE_GEOMETRY_BIT |
        VK_SHADER_STAGE_FRAGMENT_BIT |
        VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(MaterialPushConstants);
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    if (vkCreatePipelineLayout(context_->device(), &pipelineLayoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout!");
    }

    graphicsPipeline_ = createMaterialGraphicsPipeline(ShaderConfiguration{}, MaterialData{});
}

void Renderer::createParticlePipelines() {
    particleAlphaPipeline_ = createParticlePipeline(false);
    particleAdditivePipeline_ = createParticlePipeline(true);
}

VkPipeline Renderer::createParticlePipeline(bool additiveBlending) {
    VkPipelineShaderStageCreateInfo vertexStage{};
    vertexStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexStage.module = shaderModule("shader_particle.vert");
    vertexStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragmentStage{};
    fragmentStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentStage.module = shaderModule("shader_particle.frag");
    fragmentStage.pName = "main";
    const std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {
        vertexStage,
        fragmentStage
    };

    const VkVertexInputBindingDescription bindingDescription =
        ParticleVertex::bindingDescription();
    const auto attributeDescriptions = ParticleVertex::attributeDescriptions();
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount =
        static_cast<std::uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = additiveBlending
        ? VK_BLEND_FACTOR_ONE
        : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    const std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<std::uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(
            context_->device(),
            VK_NULL_HANDLE,
            1,
            &pipelineInfo,
            nullptr,
            &pipeline
        ) != VK_SUCCESS) {
        throw std::runtime_error("failed to create particle graphics pipeline");
    }
    return pipeline;
}

void Renderer::createBoundingBoxPipeline() {
    const std::filesystem::path shaderDirectory(kShaderBinaryDirectory);
    const auto vertShaderCode = readBinaryFile(shaderDirectory / "shader_bounding_box.vert.spv");
    const auto fragShaderCode = readBinaryFile(shaderDirectory / "shader_bounding_box.frag.spv");

    const VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    const VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    const VkPipelineShaderStageCreateInfo shaderStages[] = {
        vertShaderStageInfo,
        fragShaderStageInfo
    };

    const auto bindingDescription = Vertex::bindingDescription();
    const auto attributeDescriptions = Vertex::attributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    const std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0;

    const VkResult result = vkCreateGraphicsPipelines(
        context_->device(),
        VK_NULL_HANDLE,
        1,
        &pipelineInfo,
        nullptr,
        &boundingBoxPipeline_
    );

    vkDestroyShaderModule(context_->device(), fragShaderModule, nullptr);
    vkDestroyShaderModule(context_->device(), vertShaderModule, nullptr);

    if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to create bounding box pipeline!");
    }
}

void Renderer::createBoundingBoxBuffers() {
    boundingBoxVertexBuffers_.resize(kMaxFramesInFlight);
    const VkDeviceSize bufferSize = sizeof(boundingBoxVertices_);
    for (auto& boundingBoxVertexBuffer : boundingBoxVertexBuffers_) {
        boundingBoxVertexBuffer = std::make_unique<Buffer>(
            *context_,
            bufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
    }
}

void Renderer::createMeshData() {
    if (std::string_view(kModelPath).empty()) {
        meshData_.vertices.assign(kRectangleVertices.begin(), kRectangleVertices.end());
        meshData_.indices.assign(kRectangleIndices.begin(), kRectangleIndices.end());
        meshData_.materials.emplace_back();
        meshData_.materials.back().name = "DefaultMaterial";
        meshData_.parts.push_back({0, static_cast<uint32_t>(meshData_.indices.size()), 0});
        meshData_.hierarchy.name = "Rectangle";
        meshData_.hierarchy.meshCount = 1;
        meshData_.hierarchy.meshPartIndices.push_back(0);
        meshData_.hierarchy.hasBounds = true;
        meshData_.hierarchy.boundsMinimum = glm::vec3(-0.5f, -0.5f, 0.0f);
        meshData_.hierarchy.boundsMaximum = glm::vec3(0.5f, 0.5f, 0.0f);
        modelCenter_ = glm::vec3(0.0f);
        modelScale_ = 1.0f;
        return;
    }

    meshData_ = loadModelMesh(std::filesystem::path(kModelPath));

    glm::vec3 minimum(std::numeric_limits<float>::max());
    glm::vec3 maximum(std::numeric_limits<float>::lowest());
    for (const Vertex& vertex : meshData_.vertices) {
        minimum = glm::min(minimum, vertex.position);
        maximum = glm::max(maximum, vertex.position);
    }

    const glm::vec3 extent = maximum - minimum;
    const float largestExtent = std::max(extent.x, std::max(extent.y, extent.z));
    if (largestExtent <= std::numeric_limits<float>::epsilon()) {
        throw std::runtime_error("model has no measurable bounds");
    }

    modelCenter_ = (minimum + maximum) * 0.5f;
    modelScale_ = 1.8f / largestExtent;
}

void Renderer::createSunMesh() {
    constexpr std::uint32_t kLatitudeSegments = 24;
    constexpr std::uint32_t kLongitudeSegments = 36;
    constexpr float kSunRadiusInViewport = 0.15f;
    const glm::vec3 sunPositionInViewport(0.82f, 0.78f, 1.08f);
    const float safeModelScale = std::max(modelScale_, std::numeric_limits<float>::epsilon());
    const glm::vec3 center = modelCenter_ + sunPositionInViewport / safeModelScale;
    const float radius = kSunRadiusInViewport / safeModelScale;

    const auto sunMaterial = std::find_if(
        meshData_.materials.begin(),
        meshData_.materials.end(),
        [](const MaterialData& material) {
            return material.name == "Sun";
        }
    );
    if (sunMaterial == meshData_.materials.end()) {
        MaterialData material;
        material.name = "Sun";
        material.baseColor = glm::vec4(1.0f, 0.55f, 0.06f, 1.0f);
        material.roughness = 0.24f;
        material.doubleSided = true;
        material.userCreated = true;
        material.shaderConfiguration.shader(ShaderStage::Fragment) = "shader_sun.frag";
        material.emissionColor = glm::vec3(1.0f, 0.22f, 0.015f);
        material.emissionStrength = 3.5f;
        meshData_.materials.push_back(std::move(material));
        sunMaterialIndex_ = static_cast<std::uint32_t>(meshData_.materials.size() - 1);
    } else {
        sunMaterialIndex_ = static_cast<std::uint32_t>(
            std::distance(meshData_.materials.begin(), sunMaterial)
        );
        if (sunMaterial->shaderConfiguration.shader(ShaderStage::Fragment).empty()) {
            sunMaterial->shaderConfiguration.shader(ShaderStage::Fragment) = "shader_sun.frag";
        }
    }

    const std::uint32_t sunNodeId = maximumModelNodeId(meshData_.hierarchy) + 1;
    const std::uint32_t firstIndex = static_cast<std::uint32_t>(meshData_.indices.size());
    const std::uint32_t vertexStart = static_cast<std::uint32_t>(meshData_.vertices.size());
    const float pi = std::acos(-1.0f);

    meshData_.vertices.reserve(
        meshData_.vertices.size() +
        static_cast<std::size_t>(kLatitudeSegments + 1) * (kLongitudeSegments + 1)
    );
    for (std::uint32_t latitude = 0; latitude <= kLatitudeSegments; ++latitude) {
        const float phi = pi * static_cast<float>(latitude) /
            static_cast<float>(kLatitudeSegments);
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);
        for (std::uint32_t longitude = 0; longitude <= kLongitudeSegments; ++longitude) {
            const float theta = 2.0f * pi * static_cast<float>(longitude) /
                static_cast<float>(kLongitudeSegments);
            const glm::vec3 normal(
                sinPhi * std::cos(theta),
                sinPhi * std::sin(theta),
                cosPhi
            );
            meshData_.vertices.push_back({
                center + normal * radius,
                normal,
                glm::vec2(
                    static_cast<float>(longitude) / static_cast<float>(kLongitudeSegments),
                    static_cast<float>(latitude) / static_cast<float>(kLatitudeSegments)
                ),
                glm::vec4(1.0f)
            });
        }
    }

    const std::uint32_t rowSize = kLongitudeSegments + 1;
    for (std::uint32_t latitude = 0; latitude < kLatitudeSegments; ++latitude) {
        for (std::uint32_t longitude = 0; longitude < kLongitudeSegments; ++longitude) {
            const std::uint32_t first = vertexStart + latitude * rowSize + longitude;
            const std::uint32_t second = first + rowSize;
            meshData_.indices.insert(
                meshData_.indices.end(),
                {first, second, first + 1, second, second + 1, first + 1}
            );
        }
    }

    const std::uint32_t indexCount = static_cast<std::uint32_t>(
        meshData_.indices.size() - firstIndex
    );
    const std::uint32_t partIndex = static_cast<std::uint32_t>(meshData_.parts.size());
    meshData_.parts.push_back({firstIndex, indexCount, sunMaterialIndex_, sunNodeId});

    ModelNode sunNode;
    sunNode.name = "Sun";
    sunNode.nodeId = sunNodeId;
    sunNode.meshCount = 1;
    sunNode.globalTransform = glm::translate(glm::mat4(1.0f), center);
    sunNode.meshPartIndices.push_back(partIndex);
    sunNode.hasBounds = true;
    sunNode.boundsMinimum = center - glm::vec3(radius);
    sunNode.boundsMaximum = center + glm::vec3(radius);
    sunNode.materialOverride = static_cast<std::int32_t>(sunMaterialIndex_);
    meshData_.additionalNodes.push_back(std::move(sunNode));
    sunNodeId_ = sunNodeId;
}

void Renderer::createVertexBuffer() {
    if (meshData_.vertices.empty()) {
        throw std::runtime_error("mesh has no vertices");
    }

    const VkDeviceSize bufferSize = sizeof(meshData_.vertices[0]) * meshData_.vertices.size();

    Buffer stagingBuffer(
        *context_,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    stagingBuffer.upload(meshData_.vertices.data(), bufferSize);

    vertexBuffer_ = std::make_unique<Buffer>(
        *context_,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    context_->copyBuffer(stagingBuffer.handle(), vertexBuffer_->handle(), bufferSize);
}

void Renderer::createIndexBuffer() {
    if (meshData_.indices.empty()) {
        throw std::runtime_error("mesh has no indices");
    }

    const VkDeviceSize bufferSize = sizeof(meshData_.indices[0]) * meshData_.indices.size();
    Buffer indexStagingBuffer(
        *context_,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    indexStagingBuffer.upload(meshData_.indices.data(), bufferSize);

    indexBuffer_ = std::make_unique<Buffer>(
        *context_,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    context_->copyBuffer(indexStagingBuffer.handle(), indexBuffer_->handle(), bufferSize);
}

void Renderer::createParticleBuffer() {
    const VkDeviceSize bufferSize = sizeof(ParticleVertex) *
        ParticleSystem::kMaximumSupportedParticles * ParticleSystem::kVerticesPerParticle *
        kMaximumParticleSystems;
    particleVertexBuffers_.resize(kMaxFramesInFlight);
    for (auto& particleVertexBuffer : particleVertexBuffers_) {
        particleVertexBuffer = std::make_unique<Buffer>(
            *context_,
            bufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
    }
}

void Renderer::updateParticleBuffer() {
    if (currentFrame_ >= particleVertexBuffers_.size() || particleVertices_.empty()) {
        return;
    }
    particleVertexBuffers_[currentFrame_]->upload(
        particleVertices_.data(),
        sizeof(ParticleVertex) * particleVertices_.size()
    );
}

void Renderer::updateParticleSystems(
    float deltaSeconds,
    const glm::vec3& cameraRight,
    const glm::vec3& cameraUp
) {
    particleVertices_.clear();
    particleDrawRanges_.clear();
    const std::size_t maximumVertices =
        static_cast<std::size_t>(ParticleSystem::kMaximumSupportedParticles) *
        ParticleSystem::kVerticesPerParticle * kMaximumParticleSystems;
    particleVertices_.reserve(maximumVertices);

    const auto updateNode = [this, deltaSeconds, &cameraRight, &cameraUp](
                                const auto& self,
                                const ModelNode& node
                            ) -> void {
        if (node.objectType == SceneObjectType::ParticleSystem) {
            const auto systemIt = particleSystems_.find(node.nodeId);
            if (systemIt != particleSystems_.end()) {
                ParticleSystem& system = systemIt->second;
                system.update(deltaSeconds);
                system.buildVertices(cameraRight, cameraUp);

                const std::vector<ParticleVertex>& sourceVertices = system.vertices();
                const std::size_t availableVertices = maximumVertices - particleVertices_.size();
                std::size_t copiedVertexCount = std::min(
                    sourceVertices.size(),
                    availableVertices
                );
                copiedVertexCount -= copiedVertexCount % ParticleSystem::kVerticesPerParticle;
                if (copiedVertexCount > 0) {
                    ParticleDrawRange range;
                    range.nodeId = node.nodeId;
                    range.firstVertex = static_cast<std::uint32_t>(particleVertices_.size());
                    range.vertexCount = static_cast<std::uint32_t>(copiedVertexCount);
                    range.blendMode = system.settings().blendMode;
                    const glm::mat4 objectTransform = particleObjectTransform(node);
                    particleDrawRanges_.push_back(range);
                    particleVertices_.reserve(
                        particleVertices_.size() + copiedVertexCount
                    );
                    for (std::size_t vertexIndex = 0; vertexIndex < copiedVertexCount; ++vertexIndex) {
                        ParticleVertex vertex = sourceVertices[vertexIndex];
                        vertex.position = glm::vec3(
                            objectTransform * glm::vec4(vertex.position, 1.0f)
                        );
                        particleVertices_.push_back(vertex);
                    }
                }
            }
        }
        for (const ModelNode& child : node.children) {
            self(self, child);
        }
    };

    updateNode(updateNode, meshData_.hierarchy);
    for (const ModelNode& node : meshData_.additionalNodes) {
        updateNode(updateNode, node);
    }
}

void Renderer::createMaterialTextures() {
    materialTextures_.resize(meshData_.materials.size());
    normalTextures_.resize(meshData_.materials.size());
    specularTextures_.resize(meshData_.materials.size());
    bool needsWhiteFallbackTexture = false;
    bool needsNormalFallbackTexture = false;

    const auto loadTexture = [this](
        const EmbeddedTextureData& embeddedTexture,
        const std::filesystem::path& texturePath,
        const char* textureLabel,
        std::size_t materialIndex
    ) -> std::unique_ptr<Texture> {
        try {
            if (!embeddedTexture.empty()) {
                if (embeddedTexture.compressed) {
                    return std::make_unique<Texture>(*context_, embeddedTexture.data);
                }
                return std::make_unique<Texture>(
                    *context_,
                    embeddedTexture.data,
                    embeddedTexture.width,
                    embeddedTexture.height
                );
            }
            if (!texturePath.empty()) {
                return std::make_unique<Texture>(*context_, texturePath);
            }
        } catch (const std::exception& error) {
            std::cerr << "warning: failed to load " << textureLabel
                      << " texture for material " << materialIndex << ": "
                      << error.what() << '\n';
        }
        return nullptr;
    };

    for (std::size_t materialIndex = 0; materialIndex < meshData_.materials.size(); ++materialIndex) {
        const MaterialData& material = meshData_.materials[materialIndex];
        materialTextures_[materialIndex] = loadTexture(
            material.embeddedTexture,
            material.texturePath,
            "base color",
            materialIndex
        );
        normalTextures_[materialIndex] = loadTexture(
            material.embeddedNormalTexture,
            material.normalTexturePath,
            "normal",
            materialIndex
        );
        specularTextures_[materialIndex] = loadTexture(
            material.embeddedSpecularTexture,
            material.specularTexturePath,
            "specular",
            materialIndex
        );

        needsWhiteFallbackTexture = needsWhiteFallbackTexture ||
            materialTextures_[materialIndex] == nullptr ||
            specularTextures_[materialIndex] == nullptr;
        needsNormalFallbackTexture = needsNormalFallbackTexture ||
            normalTextures_[materialIndex] == nullptr;
    }

    if (needsWhiteFallbackTexture || materialTextures_.empty() || fallbackTexture_ == nullptr) {
        const std::vector<std::uint8_t> whitePixel = {255, 255, 255, 255};
        fallbackTexture_ = std::make_unique<Texture>(*context_, whitePixel, 1, 1);
    }
    if (needsNormalFallbackTexture || normalTextures_.empty()) {
        const std::vector<std::uint8_t> flatNormalPixel = {128, 128, 255, 255};
        fallbackNormalTexture_ = std::make_unique<Texture>(
            *context_,
            flatNormalPixel,
            1,
            1
        );
    }
}

void Renderer::createParticleTextures() {
    particleTextures_.clear();
    for (const auto& [nodeId, system] : particleSystems_) {
        const std::filesystem::path& texturePath = system.settings().texturePath;
        if (texturePath.empty()) {
            continue;
        }
        try {
            particleTextures_.emplace(
                nodeId,
                std::make_unique<Texture>(*context_, texturePath)
            );
        } catch (const std::exception& error) {
            std::cerr << "warning: failed to load particle texture for node "
                      << nodeId << ": " << error.what() << '\n';
        }
    }
}

void Renderer::loadParticleTexture(std::uint32_t nodeId) {
    const auto systemIt = particleSystems_.find(nodeId);
    if (systemIt == particleSystems_.end()) {
        return;
    }

    context_->waitIdle();
    particleTextures_.erase(nodeId);
    const std::filesystem::path& texturePath = systemIt->second.settings().texturePath;
    if (!texturePath.empty()) {
        try {
            particleTextures_.emplace(
                nodeId,
                std::make_unique<Texture>(*context_, texturePath)
            );
        } catch (const std::exception& error) {
            std::cerr << "warning: failed to load particle texture for node "
                      << nodeId << ": " << error.what() << '\n';
        }
    }
    updateParticleTextureDescriptors(nodeId);
}

void Renderer::rebuildMaterialResources() {
    context_->waitIdle();
    materialTextures_.clear();
    normalTextures_.clear();
    specularTextures_.clear();
    fallbackTexture_.reset();
    fallbackNormalTexture_.reset();
    descriptorSets_.clear();
    particleDescriptorSets_.clear();
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(context_->device(), descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }
    createMaterialTextures();
    createDescriptorPool();
    createDescriptorSets();
}

void Renderer::createUniformBuffers() {
    const VkDeviceSize bufferSize = sizeof(UniformBufferObject);
    uniformBuffers_.resize(kMaxFramesInFlight);

    for (auto& uniformBuffer : uniformBuffers_) {
        uniformBuffer = std::make_unique<Buffer>(
            *context_,
            bufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
    }
}

void Renderer::createDescriptorPool() {
    const uint32_t materialCount = static_cast<uint32_t>(meshData_.materials.size());
    const uint32_t materialDescriptorCount = kMaxFramesInFlight * materialCount;
    const uint32_t particleDescriptorCapacity =
        kMaxFramesInFlight * kMaximumParticleSystems;
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = materialDescriptorCount + particleDescriptorCapacity;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = materialDescriptorCount * 4 + particleDescriptorCapacity;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = materialDescriptorCount + particleDescriptorCapacity;

    if (vkCreateDescriptorPool(context_->device(), &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool!");
    }
}

void Renderer::createDescriptorSets() {
    const uint32_t materialCount = static_cast<uint32_t>(meshData_.materials.size());
    const uint32_t descriptorSetCount = kMaxFramesInFlight * materialCount;
    std::vector<VkDescriptorSetLayout> layouts(descriptorSetCount, descriptorSetLayout_);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = descriptorSetCount;
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets_.resize(descriptorSetCount);
    if (vkAllocateDescriptorSets(context_->device(), &allocInfo, descriptorSets_.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor sets!");
    }

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        for (uint32_t materialIndex = 0; materialIndex < materialCount; ++materialIndex) {
            const std::size_t descriptorSetIndex =
                static_cast<std::size_t>(i) * materialCount + materialIndex;
            const Texture* texture = materialTextures_[materialIndex].get();
            if (texture == nullptr) {
                texture = fallbackTexture_.get();
            }
            const Texture* normalTexture = normalTextures_[materialIndex].get();
            if (normalTexture == nullptr) {
                normalTexture = fallbackNormalTexture_.get();
            }
            const Texture* specularTexture = specularTextures_[materialIndex].get();
            if (specularTexture == nullptr) {
                specularTexture = fallbackTexture_.get();
            }
            if (texture == nullptr || normalTexture == nullptr || specularTexture == nullptr) {
                throw std::runtime_error("material has no usable texture fallback");
            }

            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = uniformBuffers_[i]->handle();
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(UniformBufferObject);

            VkDescriptorImageInfo imageInfos[3]{};
            for (VkDescriptorImageInfo& imageInfo : imageInfos) {
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
            imageInfos[0].imageView = texture->imageView();
            imageInfos[0].sampler = texture->sampler();
            imageInfos[1].imageView = normalTexture->imageView();
            imageInfos[1].sampler = normalTexture->sampler();
            imageInfos[2].imageView = specularTexture->imageView();
            imageInfos[2].sampler = specularTexture->sampler();

            std::array<VkWriteDescriptorSet, 4> descriptorWrites{};
            descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[0].dstSet = descriptorSets_[descriptorSetIndex];
            descriptorWrites[0].dstBinding = 0;
            descriptorWrites[0].dstArrayElement = 0;
            descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrites[0].descriptorCount = 1;
            descriptorWrites[0].pBufferInfo = &bufferInfo;

            descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[1].dstSet = descriptorSets_[descriptorSetIndex];
            descriptorWrites[1].dstBinding = 1;
            descriptorWrites[1].dstArrayElement = 0;
            descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[1].descriptorCount = 1;
            descriptorWrites[1].pImageInfo = &imageInfos[0];

            descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[2].dstSet = descriptorSets_[descriptorSetIndex];
            descriptorWrites[2].dstBinding = 3;
            descriptorWrites[2].dstArrayElement = 0;
            descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[2].descriptorCount = 1;
            descriptorWrites[2].pImageInfo = &imageInfos[1];

            descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[3].dstSet = descriptorSets_[descriptorSetIndex];
            descriptorWrites[3].dstBinding = 4;
            descriptorWrites[3].dstArrayElement = 0;
            descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[3].descriptorCount = 1;
            descriptorWrites[3].pImageInfo = &imageInfos[2];

            vkUpdateDescriptorSets(
                context_->device(),
                static_cast<uint32_t>(descriptorWrites.size()),
                descriptorWrites.data(),
                0,
                nullptr
            );
        }
    }
    updateSceneTextureDescriptors();
    particleDescriptorSets_.clear();
    for (const auto& [nodeId, system] : particleSystems_) {
        (void)system;
        createParticleDescriptorSets(nodeId);
    }
}

void Renderer::createParticleDescriptorSets(std::uint32_t nodeId) {
    if (particleDescriptorSets_.contains(nodeId)) {
        updateParticleTextureDescriptors(nodeId);
        return;
    }
    if (particleDescriptorSets_.size() >= kMaximumParticleSystems) {
        std::cerr << "warning: particle descriptor capacity reached; node "
                  << nodeId << " will not be rendered\n";
        return;
    }

    std::array<VkDescriptorSetLayout, kMaxFramesInFlight> layouts{};
    layouts.fill(descriptorSetLayout_);
    std::array<VkDescriptorSet, kMaxFramesInFlight> descriptorSets{};
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = descriptorPool_;
    allocateInfo.descriptorSetCount = kMaxFramesInFlight;
    allocateInfo.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(
            context_->device(),
            &allocateInfo,
            descriptorSets.data()
        ) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate particle descriptor sets");
    }
    particleDescriptorSets_.emplace(nodeId, descriptorSets);
    updateParticleTextureDescriptors(nodeId);
}

void Renderer::updateParticleTextureDescriptors(std::uint32_t nodeId) {
    const auto descriptorIt = particleDescriptorSets_.find(nodeId);
    if (descriptorIt == particleDescriptorSets_.end()) {
        return;
    }

    const Texture* texture = fallbackTexture_.get();
    const auto textureIt = particleTextures_.find(nodeId);
    if (textureIt != particleTextures_.end() && textureIt->second != nullptr) {
        texture = textureIt->second.get();
    }
    if (texture == nullptr) {
        throw std::runtime_error("particle system has no usable texture fallback");
    }

    for (std::uint32_t frameIndex = 0; frameIndex < kMaxFramesInFlight; ++frameIndex) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers_[frameIndex]->handle();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = texture->imageView();
        imageInfo.sampler = texture->sampler();

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptorIt->second[frameIndex];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &bufferInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descriptorIt->second[frameIndex];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(
            context_->device(),
            static_cast<std::uint32_t>(writes.size()),
            writes.data(),
            0,
            nullptr
        );
    }
}

void Renderer::updateSceneTextureDescriptors() {
    if (sceneRenderTargets_.size() < kMaxFramesInFlight) {
        throw std::runtime_error("scene render targets are not initialized");
    }

    const uint32_t materialCount = static_cast<uint32_t>(meshData_.materials.size());
    for (uint32_t frameIndex = 0; frameIndex < kMaxFramesInFlight; ++frameIndex) {
        const SceneRenderTarget& target = sceneRenderTargets_[frameIndex];
        for (uint32_t materialIndex = 0; materialIndex < materialCount; ++materialIndex) {
            const std::size_t descriptorSetIndex =
                static_cast<std::size_t>(frameIndex) * materialCount + materialIndex;
            VkDescriptorImageInfo sceneImageInfo{};
            sceneImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            sceneImageInfo.imageView = target.colorImageView;
            sceneImageInfo.sampler = target.colorSampler;

            VkWriteDescriptorSet sceneWrite{};
            sceneWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            sceneWrite.dstSet = descriptorSets_[descriptorSetIndex];
            sceneWrite.dstBinding = 2;
            sceneWrite.descriptorCount = 1;
            sceneWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            sceneWrite.pImageInfo = &sceneImageInfo;
            vkUpdateDescriptorSets(context_->device(), 1, &sceneWrite, 0, nullptr);
        }
    }
}

void Renderer::updateUniformBuffer() {
    const auto now = std::chrono::steady_clock::now();
    const float elapsedSeconds = std::chrono::duration<float>(now - startTime_).count();

    UniformBufferObject ubo{};
    // Keep the imported scene centered and normalized. Per-node transforms
    // are supplied through the draw push constants, after an object has been
    // selected in the hierarchy.
    const glm::mat4 modelNormalization =
        glm::scale(
            glm::mat4(1.0f),
            glm::vec3(modelScale_)
        ) *
        glm::translate(glm::mat4(1.0f), -modelCenter_);

    ubo.model = modelNormalization;
    ubo.time = glm::vec4(elapsedSeconds, 0.0f, 0.0f, 0.0f);
    ubo.view = glm::lookAt(
        glm::vec3(3.5f, 3.5f, 2.8f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f)
    );

    const VkExtent2D extent = swapChain_->extent();
    if (extent.height == 0) {
        return;
    }
    ubo.viewport = glm::vec4(
        static_cast<float>(extent.width),
        static_cast<float>(extent.height),
        1.0f / static_cast<float>(extent.width),
        1.0f / static_cast<float>(extent.height)
    );
    ubo.projection = glm::perspective(
        glm::radians(45.0f),
        static_cast<float>(extent.width) / static_cast<float>(extent.height),
        0.01f,
        100.0f
    );
    // GLM follows OpenGL's Y direction; Vulkan's viewport uses the opposite one.
    ubo.projection[1][1] *= -1.0f;

    const glm::vec3 normalizedSunDirection = glm::length(sunLightDirection_) < 0.001f
        ? glm::vec3(0.45f, 0.55f, 0.75f)
        : glm::normalize(sunLightDirection_);
    ubo.ambientLight = glm::vec4(
        glm::clamp(ambientLightColor_, glm::vec3(0.0f), glm::vec3(1.0f)),
        std::clamp(ambientLightIntensity_, 0.0f, 4.0f)
    );
    ubo.sunLightDirection = glm::vec4(
        normalizedSunDirection,
        std::clamp(sunLightIntensity_, 0.0f, 4.0f)
    );
    ubo.sunLightColor = glm::vec4(
        glm::clamp(sunLightColor_, glm::vec3(0.0f), glm::vec3(1.0f)),
        1.0f
    );

    uniformBuffers_[currentFrame_]->upload(&ubo, sizeof(ubo));
}

void Renderer::updateBoundingBoxBuffer() {
    if (currentFrame_ >= boundingBoxVertexBuffers_.size()) {
        return;
    }

    boundingBoxVertexBuffers_[currentFrame_]->upload(
        boundingBoxVertices_.data(),
        sizeof(boundingBoxVertices_)
    );
}

void Renderer::createCommandBuffers() {
    commandBuffers_.resize(kMaxFramesInFlight);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = context_->commandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());

    if (vkAllocateCommandBuffers(context_->device(), &allocInfo, commandBuffers_.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers!");
    }
}

void Renderer::createSyncObjects() {
    imageAvailableSemaphores_.resize(kMaxFramesInFlight);
    renderFinishedSemaphores_.resize(kMaxFramesInFlight);
    inFlightFences_.resize(kMaxFramesInFlight);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (vkCreateSemaphore(context_->device(), &semaphoreInfo, nullptr, &imageAvailableSemaphores_[i]) != VK_SUCCESS ||
            vkCreateSemaphore(context_->device(), &semaphoreInfo, nullptr, &renderFinishedSemaphores_[i]) != VK_SUCCESS ||
            vkCreateFence(context_->device(), &fenceInfo, nullptr, &inFlightFences_[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create synchronization objects for a frame!");
        }
    }
}

void Renderer::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    const VkExtent2D extent = swapChain_->extent();
    const uint32_t materialCount = static_cast<uint32_t>(meshData_.materials.size());

    VkViewport viewport{};
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.extent = extent;

    const VkBuffer buffers[] = {vertexBuffer_->handle()};
    const VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer_->handle(), 0, VK_INDEX_TYPE_UINT32);

    const auto bindViewport = [&]() {
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    };

    const auto drawPart = [&](const MeshPart& part) {
        const std::uint32_t materialIndex = materialIndexForPart(part);
        if (materialIndex >= materialCount || materialIndex >= materialTextures_.size()) {
            return;
        }
        const MaterialData& material = meshData_.materials[materialIndex];
        const VkPipeline materialPipeline = graphicsPipelineForMaterial(material);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, materialPipeline);

        const std::size_t descriptorSetIndex =
            static_cast<std::size_t>(currentFrame_) * materialCount + materialIndex;
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout_,
            0,
            1,
            &descriptorSets_[descriptorSetIndex],
            0,
            nullptr
        );

        MaterialPushConstants materialPushConstants{};
        materialPushConstants.nodeTransform = transformForSceneNode(part.nodeId);
        materialPushConstants.baseColor = material.baseColor;
        materialPushConstants.textureFlags =
            (materialTextures_[materialIndex] != nullptr ? 1u : 0u) |
            (normalTextures_[materialIndex] != nullptr ? 2u : 0u) |
            (specularTextures_[materialIndex] != nullptr ? 4u : 0u);
        materialPushConstants.metallic = material.metallic;
        materialPushConstants.roughness = material.roughness;
        materialPushConstants.opacity = material.opacity;
        const WaterShaderSettings& waterSettings = material.waterShaderSettings;
        materialPushConstants.waterFlow = glm::vec4(
            waterSettings.flowDirection,
            waterSettings.flowSpeed,
            waterSettings.flowStrength
        );
        materialPushConstants.waterSurface = glm::vec4(
            waterSettings.flowTurbulence,
            waterSettings.waveScale,
            waterSettings.waveHeight,
            0.0f
        );
        if (!isWaterShaderConfiguration(material.shaderConfiguration)) {
            // The default shader uses this slot for emission. Water shaders
            // keep the same 128-byte push-constant layout for compatibility
            // with Vulkan devices that only expose the guaranteed minimum.
            materialPushConstants.waterSurface = glm::vec4(
                material.emissionColor,
                material.emissionStrength
            );
        }
        vkCmdPushConstants(
            commandBuffer,
            pipelineLayout_,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(MaterialPushConstants),
            &materialPushConstants
        );
        vkCmdDrawIndexed(commandBuffer, part.indexCount, 1, part.firstIndex, 0, 0);
    };

    const SceneRenderTarget& sceneTarget = sceneRenderTargets_[currentFrame_];
    VkRenderPassBeginInfo sceneRenderPassInfo{};
    sceneRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    sceneRenderPassInfo.renderPass = sceneRenderPass_;
    sceneRenderPassInfo.framebuffer = sceneTarget.framebuffer;
    sceneRenderPassInfo.renderArea.offset = {0, 0};
    sceneRenderPassInfo.renderArea.extent = extent;
    const std::array<VkClearValue, 2> sceneClearValues = {{
        {{{0.015f, 0.02f, 0.03f, 1.0f}}},
        {{{1.0f, 0}}}
    }};
    sceneRenderPassInfo.clearValueCount = static_cast<uint32_t>(sceneClearValues.size());
    sceneRenderPassInfo.pClearValues = sceneClearValues.data();
    vkCmdBeginRenderPass(commandBuffer, &sceneRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    bindViewport();

    // Render the opaque scene into a sampled color/depth pair first. Water
    // parts are intentionally excluded: they need this color/depth as their
    // reflection and refraction source in the second pass.
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_);
    for (const MeshPart& part : meshData_.parts) {
        const std::uint32_t materialIndex = materialIndexForPart(part);
        if (materialIndex >= materialCount || part.indexCount == 0) {
            continue;
        }
        const MaterialData& material = meshData_.materials[materialIndex];
        const ModelNode* node = findSceneNodeById(meshData_, part.nodeId);
        const bool isWater = node != nullptr &&
            isWaterShaderConfiguration(meshData_.materials[materialIndex].shaderConfiguration);
        if (!isWater && !isTransparentMaterial(material)) {
            drawPart(part);
        }
    }
    vkCmdEndRenderPass(commandBuffer);

    VkImageMemoryBarrier sceneColorToTransfer{};
    sceneColorToTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    sceneColorToTransfer.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    sceneColorToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    sceneColorToTransfer.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    sceneColorToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    sceneColorToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    sceneColorToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    sceneColorToTransfer.image = sceneTarget.colorImage;
    sceneColorToTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    sceneColorToTransfer.subresourceRange.levelCount = 1;
    sceneColorToTransfer.subresourceRange.layerCount = 1;

    VkImageMemoryBarrier sceneDepthToTransfer{};
    sceneDepthToTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    sceneDepthToTransfer.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    sceneDepthToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    sceneDepthToTransfer.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    sceneDepthToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    sceneDepthToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    sceneDepthToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    sceneDepthToTransfer.image = sceneTarget.depthImage;
    sceneDepthToTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    sceneDepthToTransfer.subresourceRange.levelCount = 1;
    sceneDepthToTransfer.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        2,
        std::array<VkImageMemoryBarrier, 2>{sceneColorToTransfer, sceneDepthToTransfer}.data()
    );

    VkImageMemoryBarrier swapColorToTransfer{};
    swapColorToTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    swapColorToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    swapColorToTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    swapColorToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapColorToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapColorToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapColorToTransfer.image = swapChain_->image(imageIndex);
    swapColorToTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    swapColorToTransfer.subresourceRange.levelCount = 1;
    swapColorToTransfer.subresourceRange.layerCount = 1;

    VkImageMemoryBarrier swapDepthToTransfer{};
    swapDepthToTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    swapDepthToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    swapDepthToTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    swapDepthToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapDepthToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapDepthToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapDepthToTransfer.image = swapChain_->depthImage();
    swapDepthToTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    swapDepthToTransfer.subresourceRange.levelCount = 1;
    swapDepthToTransfer.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        2,
        std::array<VkImageMemoryBarrier, 2>{swapColorToTransfer, swapDepthToTransfer}.data()
    );

    VkImageCopy colorCopy{};
    colorCopy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorCopy.srcSubresource.layerCount = 1;
    colorCopy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorCopy.dstSubresource.layerCount = 1;
    colorCopy.extent = {extent.width, extent.height, 1};
    vkCmdCopyImage(
        commandBuffer,
        sceneTarget.colorImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        swapChain_->image(imageIndex),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &colorCopy
    );

    VkImageCopy depthCopy{};
    depthCopy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthCopy.srcSubresource.layerCount = 1;
    depthCopy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthCopy.dstSubresource.layerCount = 1;
    depthCopy.extent = {extent.width, extent.height, 1};
    vkCmdCopyImage(
        commandBuffer,
        sceneTarget.depthImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        swapChain_->depthImage(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &depthCopy
    );

    VkImageMemoryBarrier sceneColorToShader{};
    sceneColorToShader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    sceneColorToShader.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    sceneColorToShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    sceneColorToShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    sceneColorToShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    sceneColorToShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    sceneColorToShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    sceneColorToShader.image = sceneTarget.colorImage;
    sceneColorToShader.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    sceneColorToShader.subresourceRange.levelCount = 1;
    sceneColorToShader.subresourceRange.layerCount = 1;

    VkImageMemoryBarrier swapColorToAttachment{};
    swapColorToAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    swapColorToAttachment.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    swapColorToAttachment.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    swapColorToAttachment.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapColorToAttachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    swapColorToAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapColorToAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapColorToAttachment.image = swapChain_->image(imageIndex);
    swapColorToAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    swapColorToAttachment.subresourceRange.levelCount = 1;
    swapColorToAttachment.subresourceRange.layerCount = 1;

    VkImageMemoryBarrier swapDepthToAttachment{};
    swapDepthToAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    swapDepthToAttachment.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    swapDepthToAttachment.dstAccessMask =
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    swapDepthToAttachment.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapDepthToAttachment.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    swapDepthToAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapDepthToAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapDepthToAttachment.image = swapChain_->depthImage();
    swapDepthToAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    swapDepthToAttachment.subresourceRange.levelCount = 1;
    swapDepthToAttachment.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        3,
        std::array<VkImageMemoryBarrier, 3>{
            sceneColorToShader,
            swapColorToAttachment,
            swapDepthToAttachment
        }.data()
    );

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass_;
    renderPassInfo.framebuffer = swapChain_->framebuffers()[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = extent;
    const std::array<VkClearValue, 2> clearValues = {{
        {{{0.015f, 0.02f, 0.03f, 1.0f}}},
        {{{1.0f, 0}}}
    }};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    bindViewport();

    // The opaque scene is already in the swap-chain color attachment. Only
    // water is rendered here so it can blend over and sample that scene.
    for (const MeshPart& part : meshData_.parts) {
        const std::uint32_t materialIndex = materialIndexForPart(part);
        if (materialIndex >= materialCount || part.indexCount == 0) {
            continue;
        }
        const MaterialData& material = meshData_.materials[materialIndex];
        const ModelNode* node = findSceneNodeById(meshData_, part.nodeId);
        const bool isWater = node != nullptr &&
            isWaterShaderConfiguration(meshData_.materials[materialIndex].shaderConfiguration);
        if (isWater || isTransparentMaterial(material)) {
            drawPart(part);
        }
    }

    if (!particleDrawRanges_.empty()) {
        const VkBuffer particleBuffer = particleVertexBuffers_[currentFrame_]->handle();
        const VkDeviceSize particleOffset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &particleBuffer, &particleOffset);
        for (const ParticleDrawRange& range : particleDrawRanges_) {
            const auto descriptorIt = particleDescriptorSets_.find(range.nodeId);
            if (descriptorIt == particleDescriptorSets_.end()) {
                continue;
            }
            const VkPipeline particlePipeline =
                range.blendMode == ParticleSystemSettings::BlendMode::Additive
                    ? particleAdditivePipeline_
                    : particleAlphaPipeline_;
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, particlePipeline);
            const VkDescriptorSet& particleDescriptorSet =
                descriptorIt->second[currentFrame_];
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelineLayout_,
                0,
                1,
                &particleDescriptorSet,
                0,
                nullptr
            );
            MaterialPushConstants particlePushConstants{};
            particlePushConstants.textureFlags = particleTextures_.contains(range.nodeId)
                ? 1u
                : 0u;
            vkCmdPushConstants(
                commandBuffer,
                pipelineLayout_,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(MaterialPushConstants),
                &particlePushConstants
            );
            vkCmdDraw(commandBuffer, range.vertexCount, 1, range.firstVertex, 0);
        }
    }

    if (selectedModelNode_ != nullptr && selectedModelNode_->hasBounds) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, boundingBoxPipeline_);

        const VkBuffer boundingBoxBuffer = boundingBoxVertexBuffers_[currentFrame_]->handle();
        const VkDeviceSize boundingBoxOffset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &boundingBoxBuffer, &boundingBoxOffset);

        const VkDescriptorSet& uniformDescriptorSet = descriptorSets_[currentFrame_ * materialCount];
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout_,
            0,
            1,
            &uniformDescriptorSet,
            0,
            nullptr
        );
        MaterialPushConstants boundingBoxPushConstants{};
        vkCmdPushConstants(
            commandBuffer,
            pipelineLayout_,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(MaterialPushConstants),
            &boundingBoxPushConstants
        );
        vkCmdDraw(commandBuffer, static_cast<uint32_t>(boundingBoxVertices_.size()), 1, 0, 0);
    }

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

    vkCmdEndRenderPass(commandBuffer);
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }
}

void Renderer::drawFrame() {
    if (vkWaitForFences(
            context_->device(),
            1,
            &inFlightFences_[currentFrame_],
            VK_TRUE,
            UINT64_MAX
        ) != VK_SUCCESS) {
        throw std::runtime_error("failed to wait for in-flight frame!");
    }

    uint32_t imageIndex = 0;
    const VkResult acquireResult = vkAcquireNextImageKHR(
        context_->device(),
        swapChain_->handle(),
        UINT64_MAX,
        imageAvailableSemaphores_[currentFrame_],
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
        return;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    buildImGuiFrame();
    const auto now = std::chrono::steady_clock::now();
    const float elapsedSeconds = std::chrono::duration<float>(now - startTime_).count();
    updateSceneNodeTransforms(elapsedSeconds);
    const float particleDeltaSeconds = std::clamp(
        elapsedSeconds - lastFrameElapsedSeconds_,
        0.0f,
        0.1f
    );
    lastFrameElapsedSeconds_ = elapsedSeconds;
    const glm::vec3 cameraPosition(3.5f, 3.5f, 2.8f);
    const glm::vec3 cameraTarget(0.0f);
    const glm::vec3 cameraForward = glm::normalize(cameraTarget - cameraPosition);
    const glm::vec3 worldUp(0.0f, 0.0f, 1.0f);
    const glm::vec3 cameraRight = glm::normalize(glm::cross(cameraForward, worldUp));
    const glm::vec3 cameraUp = glm::normalize(glm::cross(cameraRight, cameraForward));
    updateParticleSystems(particleDeltaSeconds, cameraRight, cameraUp);
    updateBoundingBoxVertices();

    if (vkResetFences(context_->device(), 1, &inFlightFences_[currentFrame_]) != VK_SUCCESS) {
        throw std::runtime_error("failed to reset in-flight frame fence!");
    }
    updateUniformBuffer();
    updateParticleBuffer();
    updateBoundingBoxBuffer();
    if (vkResetCommandBuffer(commandBuffers_[currentFrame_], 0) != VK_SUCCESS) {
        throw std::runtime_error("failed to reset command buffer!");
    }
    recordCommandBuffer(commandBuffers_[currentFrame_], imageIndex);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    const VkSemaphore waitSemaphores[] = {imageAvailableSemaphores_[currentFrame_]};
    const VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers_[currentFrame_];
    const VkSemaphore signalSemaphores[] = {renderFinishedSemaphores_[currentFrame_]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(context_->graphicsQueue(), 1, &submitInfo, inFlightFences_[currentFrame_]) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit draw command buffer!");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    const VkSwapchainKHR swapChains[] = {swapChain_->handle()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    const VkResult presentResult = vkQueuePresentKHR(context_->presentQueue(), &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
        presentResult == VK_SUBOPTIMAL_KHR ||
        framebufferResized_) {
        framebufferResized_ = false;
        recreateSwapChain();
    } else if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("failed to present swap chain image!");
    }

    currentFrame_ = (currentFrame_ + 1) % kMaxFramesInFlight;
}

void Renderer::recreateSwapChain() {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(context_->window(), &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(context_->window(), &width, &height);
        glfwWaitEvents();
    }

    context_->waitIdle();
    destroySceneRenderResources();
    swapChain_->recreate(renderPass_);
    createSceneRenderResources();
    updateSceneTextureDescriptors();
    ImGui_ImplVulkan_SetMinImageCount(
        std::max(2u, static_cast<uint32_t>(swapChain_->framebuffers().size()))
    );
}

VkShaderModule Renderer::createShaderModule(const std::vector<char>& code) const {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(context_->device(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module!");
    }
    return shaderModule;
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
