#include "RendererPrivate.hpp"
#include "vulkcore/Utils/SbfresLoader.hpp"

namespace VulkCore::renderer_detail {

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

bool isModelAssetPath(const std::filesystem::path& path) {
    if (isBfresPath(path)) return !isBfresTextureSidecarPath(path);
    static constexpr std::array<std::string_view, 17> kModelExtensions = {
        ".fbx", ".obj", ".gltf", ".glb", ".dae", ".3ds", ".ply", ".stl",
        ".blend", ".x", ".md5mesh", ".ase", ".lwo", ".lws", ".off", ".x3d",
        ".abc",
    };
    std::string extension = path.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );
    return std::find(kModelExtensions.begin(), kModelExtensions.end(), extension) !=
        kModelExtensions.end();
}

bool isSceneResourcePath(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );
    return extension == ".scene";
}

bool isTextureAssetPath(const std::filesystem::path& path) {
    static constexpr std::array<std::string_view, 10> kTextureExtensions = {
        ".png", ".jpg", ".jpeg", ".bmp", ".tga",
        ".gif", ".hdr", ".exr", ".ktx", ".dds",
    };
    std::string extension = path.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );
    return std::find(kTextureExtensions.begin(), kTextureExtensions.end(), extension) !=
        kTextureExtensions.end();
}

std::filesystem::path normalizedPath(const std::filesystem::path& path) {
    if (path.empty()) {
        return {};
    }
    std::error_code error;
    const std::filesystem::path absolutePath = path.is_absolute()
        ? path
        : std::filesystem::absolute(path, error);
    return (error ? path : absolutePath).lexically_normal();
}

bool pathsEqual(const std::filesystem::path& left, const std::filesystem::path& right) {
    return normalizedPath(left) == normalizedPath(right);
}

std::filesystem::path locateAssetsDirectory() {
    const std::filesystem::path candidates[] = {
        std::filesystem::current_path() / "assets",
        std::filesystem::current_path().parent_path() / "assets",
        std::filesystem::path(kConfigPath).parent_path() / "assets",
    };
    for (const std::filesystem::path& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_directory(candidate, error)) {
            return candidate;
        }
    }
    return std::filesystem::current_path() / "assets";
}

bool isWaterShaderConfiguration(const ShaderConfiguration& configuration) {
    const std::string& fragmentShader = configuration.shader(ShaderStage::Fragment);
    return fragmentShader == "shader_water.frag" || fragmentShader.starts_with("shader_water_");
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

bool findInheritedRenderEnabled(
    const ModelNode& node,
    std::uint32_t targetNodeId,
    bool inherited,
    bool& result
) {
    const bool effectiveEnabled = inherited && node.renderEnabled;
    if (node.nodeId == targetNodeId) {
        result = effectiveEnabled;
        return true;
    }
    for (const ModelNode& child : node.children) {
        if (findInheritedRenderEnabled(child, targetNodeId, effectiveEnabled, result)) {
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

void collectModelNodeIds(const ModelNode& node, std::vector<std::uint32_t>& nodeIds) {
    nodeIds.push_back(node.nodeId);
    for (const ModelNode& child : node.children) {
        collectModelNodeIds(child, nodeIds);
    }
}

bool removeModelNodeFromChildren(
    ModelNode& parent,
    std::uint32_t nodeId,
    std::vector<std::uint32_t>& removedNodeIds
) {
    const auto childIt = std::find_if(
        parent.children.begin(),
        parent.children.end(),
        [nodeId](const ModelNode& child) {
            return child.nodeId == nodeId;
        }
    );
    if (childIt != parent.children.end()) {
        collectModelNodeIds(*childIt, removedNodeIds);
        parent.children.erase(childIt);
        return true;
    }

    for (ModelNode& child : parent.children) {
        if (removeModelNodeFromChildren(child, nodeId, removedNodeIds)) {
            return true;
        }
    }
    return false;
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

void saveNodeRenderSettings(std::ostream& file, const ModelNode& node) {
    if (!node.renderEnabled) {
        file << "node_render " << node.nodeId << " 0\n";
    }
    for (const ModelNode& child : node.children) {
        saveNodeRenderSettings(file, child);
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

void saveNodeMaterialSettings(
    std::ostream& file,
    const ModelNode& node,
    const MaterialIndexRemap& materialIndexRemap
) {
    if (node.materialOverride >= 0) {
        const auto materialIt = materialIndexRemap.find(
            static_cast<std::size_t>(node.materialOverride)
        );
        if (materialIt != materialIndexRemap.end()) {
            file << "node_material " << node.nodeId << ' '
                 << materialIt->second << "\n";
        }
    }
    for (const ModelNode& child : node.children) {
        saveNodeMaterialSettings(file, child, materialIndexRemap);
    }
}

void saveUserMaterials(
    std::ostream& file,
    const std::vector<MaterialData>& materials,
    const MaterialIndexRemap& materialIndexRemap
) {
    for (std::size_t materialIndex = 0; materialIndex < materials.size(); ++materialIndex) {
        const MaterialData& material = materials[materialIndex];
        if (!material.userCreated && !material.userModified) {
            continue;
        }
        const auto remappedIndexIt = materialIndexRemap.find(materialIndex);
        if (remappedIndexIt == materialIndexRemap.end()) {
            continue;
        }
        const std::size_t serializedMaterialIndex = remappedIndexIt->second;
        file << "material " << serializedMaterialIndex << ' '
             << std::quoted(material.name) << ' '
             << material.baseColor.r << ' ' << material.baseColor.g << ' '
             << material.baseColor.b << ' ' << material.baseColor.a << ' '
             << material.metallic << ' ' << material.roughness << ' '
             << material.opacity << ' '
             << static_cast<int>(material.blendMode) << ' '
             << (material.depthWrite ? 1 : 0) << ' '
             << (material.doubleSided ? 1 : 0) << ' '
             << std::quoted(material.embeddedTexture.empty() ? material.texturePath.string() : "@embedded");
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
        file << "material_textures " << serializedMaterialIndex << ' '
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
         << std::quoted(settings.texturePath.string());
    const auto& book = settings.flipbook;
    if (book.frames) {
        file << " flipbook " << book.columns << ' ' << book.rows << ' '
             << book.frames << ' ' << book.cellPixels << ' ' << book.gutterPixels << ' ' << book.width;
    }
    file << '\n';
}

std::string shaderConfigToken(const std::string& token) {
    return token == "-" ? std::string{} : token;
}

std::size_t countModelNodes(const ModelNode& node) {
    std::size_t count = 1;
    for (const ModelNode& child : node.children) {
        count += countModelNodes(child);
    }
    return count;
}

std::size_t countMeshNodes(const ModelNode& node) {
    std::size_t count = node.meshCount > 0 ? 1 : 0;
    for (const ModelNode& child : node.children) {
        count += countMeshNodes(child);
    }
    return count;
}



} // namespace VulkCore::renderer_detail
