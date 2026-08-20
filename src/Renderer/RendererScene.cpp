#include "RendererImplementation.hpp"

namespace VulkCore {

void Renderer::discoverModelResources() {
    assetsDirectory_ = locateAssetsDirectory();
    modelResourcePaths_.clear();

    const auto addModelPath = [this](const std::filesystem::path& path) {
        if (path.empty()) {
            return;
        }
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) {
            return;
        }
        const auto existing = std::find_if(
            modelResourcePaths_.begin(),
            modelResourcePaths_.end(),
            [&path](const std::filesystem::path& candidate) {
                return pathsEqual(candidate, path);
            }
        );
        if (existing == modelResourcePaths_.end()) {
            modelResourcePaths_.push_back(path);
        }
    };

    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(
        assetsDirectory_,
        std::filesystem::directory_options::skip_permission_denied,
        error
    );
    const std::filesystem::recursive_directory_iterator end;
    for (; iterator != end && !error; iterator.increment(error)) {
        const std::filesystem::path path = iterator->path();
        if (iterator->is_regular_file(error) && isModelAssetPath(path)) {
            addModelPath(path);
        }
        error.clear();
    }

    // Keep imported models visible even when they live outside the project's
    // assets directory. Multiple instances can be present in one scene.
    for (const SceneModelResource& model : sceneModelResources_) {
        addModelPath(model.path);
    }

    std::sort(
        modelResourcePaths_.begin(),
        modelResourcePaths_.end(),
        [](const std::filesystem::path& left, const std::filesystem::path& right) {
            return normalizedPath(left).string() < normalizedPath(right).string();
        }
    );
    selectedModelPreview_.reset();
    selectedModelPreviewError_.clear();
}

void Renderer::discoverModelTextureResources(
    const std::filesystem::path& modelPath,
    ModelResourceCacheEntry& cache
) {
    cache.texturePaths.clear();
    const std::filesystem::path textureDirectory = modelPath.parent_path() / "tex";
    std::error_code error;
    if (!std::filesystem::is_directory(textureDirectory, error)) {
        return;
    }

    std::filesystem::recursive_directory_iterator iterator(
        textureDirectory,
        std::filesystem::directory_options::skip_permission_denied,
        error
    );
    const std::filesystem::recursive_directory_iterator end;
    for (; iterator != end && !error; iterator.increment(error)) {
        if (!iterator->is_regular_file(error) || !isTextureAssetPath(iterator->path())) {
            error.clear();
            continue;
        }
        const std::filesystem::path candidate = normalizedPath(iterator->path());
        const auto existing = std::find_if(
            cache.texturePaths.begin(),
            cache.texturePaths.end(),
            [&candidate](const std::filesystem::path& current) {
                return pathsEqual(current, candidate);
            }
        );
        if (existing == cache.texturePaths.end()) {
            cache.texturePaths.push_back(candidate);
        }
        error.clear();
    }
    std::sort(
        cache.texturePaths.begin(),
        cache.texturePaths.end(),
        [](const std::filesystem::path& left, const std::filesystem::path& right) {
            return left.string() < right.string();
        }
    );
}

void Renderer::synchronizeModelResourceMaterial(
    const std::filesystem::path& path,
    std::size_t materialIndex,
    const MaterialData& material
) {
    const std::filesystem::path normalizedModelPath = normalizedPath(path);
    auto cacheIt = modelResourceCache_.find(normalizedModelPath.string());
    if (cacheIt != modelResourceCache_.end() && cacheIt->second.mesh != nullptr &&
        materialIndex < cacheIt->second.mesh->materials.size() &&
        &cacheIt->second.mesh->materials[materialIndex] != &material) {
        cacheIt->second.mesh->materials[materialIndex] = material;
    }

    for (const SceneModelResource& model : sceneModelResources_) {
        if (!pathsEqual(model.path, normalizedModelPath) ||
            materialIndex >= model.materialCount) {
            continue;
        }
        const std::size_t sceneMaterialIndex =
            static_cast<std::size_t>(model.materialStart) + materialIndex;
        if (sceneMaterialIndex >= meshData_.materials.size()) {
            continue;
        }
        if (&meshData_.materials[sceneMaterialIndex] != &material) {
            meshData_.materials[sceneMaterialIndex] = material;
        }
        meshData_.materials[sceneMaterialIndex].userModified = true;
    }
}

void Renderer::refreshModelResourceCacheFromScene(const std::filesystem::path& path) {
    const std::filesystem::path normalizedModelPath = normalizedPath(path);
    auto cacheIt = modelResourceCache_.find(normalizedModelPath.string());
    if (cacheIt == modelResourceCache_.end() || cacheIt->second.mesh == nullptr) {
        return;
    }
    const auto sceneModelIt = std::find_if(
        sceneModelResources_.begin(),
        sceneModelResources_.end(),
        [&normalizedModelPath](const SceneModelResource& model) {
            return pathsEqual(model.path, normalizedModelPath);
        }
    );
    if (sceneModelIt == sceneModelResources_.end()) {
        return;
    }
    for (std::size_t materialIndex = 0;
         materialIndex < sceneModelIt->materialCount;
         ++materialIndex) {
        const std::size_t sceneMaterialIndex =
            static_cast<std::size_t>(sceneModelIt->materialStart) + materialIndex;
        if (sceneMaterialIndex >= meshData_.materials.size() ||
            materialIndex >= cacheIt->second.mesh->materials.size()) {
            break;
        }
        cacheIt->second.mesh->materials[materialIndex] =
            meshData_.materials[sceneMaterialIndex];
    }
}

void Renderer::selectModelResource(const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }

    const std::filesystem::path normalizedModelPath = normalizedPath(path);
    selectedResourceType_ = ResourceSelectionType::Model;
    selectedResourceMaterialIndex_ = -1;
    selectedModelNode_ = nullptr;
    selectedModelResourcePath_ = normalizedModelPath;

    modelImportPathBuffer_.fill('\0');
    const std::string selectedPathString = normalizedModelPath.string();
    const std::size_t selectedPathCopyCount = std::min(
        selectedPathString.size(),
        modelImportPathBuffer_.size() - 1
    );
    std::copy_n(
        selectedPathString.data(),
        selectedPathCopyCount,
        modelImportPathBuffer_.data()
    );

    selectedModelPreview_.reset();
    selectedModelPreviewError_.clear();

    ModelResourceCacheEntry& cache = modelResourceCache_[normalizedModelPath.string()];
    if (cache.mesh == nullptr && cache.error.empty()) {
        try {
            auto preview = std::make_shared<MeshData>(loadModelMesh(normalizedModelPath));
            if (preview->vertices.empty() || preview->indices.empty()) {
                throw std::runtime_error("the model contains no renderable triangles");
            }
            cache.mesh = std::move(preview);
            discoverModelTextureResources(normalizedModelPath, cache);
        } catch (const std::exception& error) {
            cache.error = error.what();
        }
    }

    refreshModelResourceCacheFromScene(normalizedModelPath);
    selectedModelPreview_ = cache.mesh;
    selectedModelPreviewError_ = cache.error;
    updateBoundingBoxVertices();
}

void Renderer::initializeEmptyScene() {
    meshData_ = MeshData{};
    MaterialData defaultMaterial;
    defaultMaterial.name = "DefaultMaterial";
    meshData_.materials.push_back(std::move(defaultMaterial));
    modelCenter_ = glm::vec3(0.0f);
    modelScale_ = 1.0f;
    sunNodeId_ = 0;
    sunMaterialIndex_ = 0;
    sunPartIndex_ = 0;
    sunVertexStart_ = 0;
    sunIndexStart_ = 0;
    sunPresent_ = false;
}

void Renderer::clearSceneData() {
    context_->waitIdle();
    selectedModelNode_ = nullptr;
    removedSceneNodeIds_.clear();
    sceneNodeTransforms_.clear();
    sceneNodeRenderEnabled_.clear();
    particleSystems_.clear();
    particleDrawRanges_.clear();
    particleVertices_.clear();
    particleTextures_.clear();
    sceneModelResources_.clear();
    nextSceneModelInstanceIndex_ = 0;
    initializeEmptyScene();
    selectedModelResourcePath_.clear();
    selectedModelPreview_.reset();
    selectedModelPreviewError_.clear();
    selectedResourceType_ = ResourceSelectionType::Model;
    selectedResourceMaterialIndex_ = -1;
    modelImportPathBuffer_.fill('\0');
    boundingBoxVertices_.fill(Vertex{});

    vertexBuffer_.reset();
    indexBuffer_.reset();
    createVertexBuffer();
    createIndexBuffer();
    createParticleTextures();
    rebuildMaterialResources();
}

void Renderer::recalculateSceneBounds() {
    if (meshData_.vertices.empty() || meshData_.parts.empty()) {
        modelCenter_ = glm::vec3(0.0f);
        modelScale_ = 1.0f;
        return;
    }

    glm::vec3 minimum(std::numeric_limits<float>::max());
    glm::vec3 maximum(std::numeric_limits<float>::lowest());
    bool hasVisibleVertex = false;
    for (const MeshPart& part : meshData_.parts) {
        if (removedSceneNodeIds_.contains(part.nodeId)) {
            continue;
        }
        const std::size_t firstIndex = part.firstIndex;
        const std::size_t endIndex = std::min<std::size_t>(
            meshData_.indices.size(),
            firstIndex + part.indexCount
        );
        for (std::size_t index = firstIndex; index < endIndex; ++index) {
            const std::uint32_t vertexIndex = meshData_.indices[index];
            if (vertexIndex >= meshData_.vertices.size()) {
                continue;
            }
            minimum = glm::min(minimum, meshData_.vertices[vertexIndex].position);
            maximum = glm::max(maximum, meshData_.vertices[vertexIndex].position);
            hasVisibleVertex = true;
        }
    }
    if (!hasVisibleVertex) {
        modelCenter_ = glm::vec3(0.0f);
        modelScale_ = 1.0f;
        return;
    }
    const glm::vec3 extent = maximum - minimum;
    const float largestExtent = std::max(extent.x, std::max(extent.y, extent.z));
    if (largestExtent <= std::numeric_limits<float>::epsilon()) {
        throw std::runtime_error("scene has no measurable bounds");
    }
    modelCenter_ = (minimum + maximum) * 0.5f;
    modelScale_ = 1.8f / largestExtent;
}

void Renderer::appendImportedModel(
    MeshData importedMesh,
    const std::filesystem::path& path
) {
    const std::uint32_t modelIndex = nextSceneModelInstanceIndex_++;
    if (modelIndex >= (std::numeric_limits<std::uint32_t>::max() - 1u) /
            kSceneModelNodeStride) {
        throw std::runtime_error("scene contains too many model instances");
    }

    const std::uint32_t vertexOffset = static_cast<std::uint32_t>(meshData_.vertices.size());
    const std::uint32_t indexOffset = static_cast<std::uint32_t>(meshData_.indices.size());
    const std::uint32_t materialOffset = static_cast<std::uint32_t>(meshData_.materials.size());
    const std::uint32_t importedMaterialCount =
        static_cast<std::uint32_t>(importedMesh.materials.size());
    const std::uint32_t partOffset = static_cast<std::uint32_t>(meshData_.parts.size());
    const std::uint32_t nodeOffset = 1u + modelIndex *
        kSceneModelNodeStride;

    for (const Vertex& vertex : importedMesh.vertices) {
        meshData_.vertices.push_back(vertex);
    }
    for (const std::uint32_t index : importedMesh.indices) {
        meshData_.indices.push_back(index + vertexOffset);
    }
    for (MaterialData& material : importedMesh.materials) {
        meshData_.materials.push_back(std::move(material));
    }
    for (MeshPart part : importedMesh.parts) {
        part.firstIndex += indexOffset;
        part.materialIndex += materialOffset;
        part.nodeId += nodeOffset;
        meshData_.parts.push_back(part);
    }

    const auto remapNode = [nodeOffset, partOffset, materialOffset](
                               const auto& self,
                               ModelNode& node
                           ) -> void {
        node.nodeId += nodeOffset;
        for (std::uint32_t& partIndex : node.meshPartIndices) {
            partIndex += partOffset;
        }
        if (node.materialOverride >= 0) {
            node.materialOverride += static_cast<std::int32_t>(materialOffset);
        }
        for (ModelNode& child : node.children) {
            self(self, child);
        }
    };
    remapNode(remapNode, importedMesh.hierarchy);

    if (meshData_.hierarchy.name.empty()) {
        meshData_.hierarchy.name = "Scene";
    }
    const std::uint32_t rootNodeId = importedMesh.hierarchy.nodeId;
    meshData_.hierarchy.children.push_back(std::move(importedMesh.hierarchy));
    sceneModelResources_.push_back({
        rootNodeId,
        normalizedPath(path),
        materialOffset,
        importedMaterialCount,
    });
    recalculateSceneBounds();
}

void Renderer::createNewEmptyScene() {
    clearSceneData();
    sceneStatusFailed_ = false;

    if (scenesDirectory_.empty()) {
        scenesDirectory_ = std::filesystem::current_path() / "scenes";
    }
    std::filesystem::path newScenePath;
    std::error_code error;
    for (std::size_t suffix = 0; ; ++suffix) {
        const std::string fileName = suffix == 0
            ? "Scene.scene"
            : "Scene_" + std::to_string(suffix) + ".scene";
        const std::filesystem::path candidate = scenesDirectory_ / fileName;
        error.clear();
        if (!std::filesystem::exists(candidate, error)) {
            if (error) {
                sceneStatusFailed_ = true;
                sceneStatus_ = "Failed to choose a scene filename: " + error.message();
                return;
            }
            newScenePath = candidate;
            break;
        }
    }

    if (saveSceneResource(newScenePath)) {
        sceneStatus_ = "Created and saved empty scene " +
            currentScenePath_.filename().string() + ".";
    }
}

void Renderer::discoverSceneResources() {
    scenesDirectory_ = assetsDirectory_.empty()
        ? std::filesystem::current_path() / "scenes"
        : assetsDirectory_.parent_path() / "scenes";
    sceneResourcePaths_.clear();

    const auto addScenePath = [this](const std::filesystem::path& path) {
        if (path.empty()) {
            return;
        }
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || !isSceneResourcePath(path)) {
            return;
        }
        const auto existing = std::find_if(
            sceneResourcePaths_.begin(),
            sceneResourcePaths_.end(),
            [&path](const std::filesystem::path& candidate) {
                return pathsEqual(candidate, path);
            }
        );
        if (existing == sceneResourcePaths_.end()) {
            sceneResourcePaths_.push_back(path);
        }
    };

    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(
        scenesDirectory_,
        std::filesystem::directory_options::skip_permission_denied,
        error
    );
    const std::filesystem::recursive_directory_iterator end;
    for (; iterator != end && !error; iterator.increment(error)) {
        addScenePath(iterator->path());
        error.clear();
    }
    addScenePath(currentScenePath_);
    std::sort(
        sceneResourcePaths_.begin(),
        sceneResourcePaths_.end(),
        [](const std::filesystem::path& left, const std::filesystem::path& right) {
            return normalizedPath(left).string() < normalizedPath(right).string();
        }
    );
}

bool Renderer::saveSceneResource(const std::filesystem::path& path) {
    sceneStatusFailed_ = false;
    sceneStatus_.clear();
    if (path.empty()) {
        sceneStatusFailed_ = true;
        sceneStatus_ = "Enter a scene file path before saving.";
        return false;
    }

    std::filesystem::path resolvedPath = path;
    if (resolvedPath.is_relative()) {
        resolvedPath = resolvedPath.parent_path().empty()
            ? scenesDirectory_ / resolvedPath
            : std::filesystem::current_path() / resolvedPath;
    }
    if (resolvedPath.extension().empty()) {
        resolvedPath += ".scene";
    }
    resolvedPath = normalizedPath(resolvedPath);

    std::error_code error;
    if (!resolvedPath.parent_path().empty()) {
        std::filesystem::create_directories(resolvedPath.parent_path(), error);
    }
    if (error) {
        sceneStatusFailed_ = true;
        sceneStatus_ = "Failed to create scene directory: " + error.message();
        return false;
    }

    const std::filesystem::path previousScenePath = currentScenePath_;
    currentScenePath_ = resolvedPath;
    if (!saveTransformSettings()) {
        currentScenePath_ = previousScenePath;
        sceneStatusFailed_ = true;
        sceneStatus_ = "Failed to save scene: " + resolvedPath.string();
        return false;
    }

    discoverSceneResources();
    scenePathBuffer_.fill('\0');
    const std::string pathString = currentScenePath_.string();
    const std::size_t copyCount = std::min(pathString.size(), scenePathBuffer_.size() - 1);
    std::copy_n(pathString.data(), copyCount, scenePathBuffer_.data());
    sceneStatus_ = "Saved scene " + currentScenePath_.filename().string() + ".";
    return true;
}

bool Renderer::loadSceneResource(const std::filesystem::path& path) {
    sceneStatusFailed_ = false;
    sceneStatus_.clear();
    std::error_code error;
    const std::filesystem::path resolvedScenePath = normalizedPath(path);
    if (!std::filesystem::is_regular_file(resolvedScenePath, error)) {
        sceneStatusFailed_ = true;
        sceneStatus_ = "Scene file was not found: " + resolvedScenePath.string();
        return false;
    }

    std::vector<std::filesystem::path> modelPaths;
    bool sceneHasSun = false;
    std::ifstream sceneFile(resolvedScenePath);
    if (!sceneFile.is_open()) {
        sceneStatusFailed_ = true;
        sceneStatus_ = "Scene file could not be opened: " + resolvedScenePath.string();
        return false;
    }
    std::string key;
    while (sceneFile >> key) {
        if (key == "model_resource_path") {
            std::string configuredPath;
            if (!(sceneFile >> std::quoted(configuredPath))) {
                sceneStatusFailed_ = true;
                sceneStatus_ = "Scene file has an invalid model path.";
                return false;
            }
            modelPaths.emplace_back(configuredPath);
        } else if (key == "sun_present") {
            int present = 0;
            if (!(sceneFile >> present) || (present != 0 && present != 1)) {
                sceneStatusFailed_ = true;
                sceneStatus_ = "Scene file has an invalid sun state.";
                return false;
            }
            sceneHasSun = present != 0;
        } else {
            std::string ignoredLine;
            std::getline(sceneFile, ignoredLine);
        }
    }

    for (std::filesystem::path& modelPath : modelPaths) {
        if (modelPath.is_relative()) {
            const std::filesystem::path sceneRelativePath =
                resolvedScenePath.parent_path() / modelPath;
            if (std::filesystem::is_regular_file(sceneRelativePath, error)) {
                modelPath = sceneRelativePath;
            } else {
                error.clear();
                modelPath = std::filesystem::absolute(modelPath, error);
            }
        }
        if (!std::filesystem::is_regular_file(modelPath, error)) {
            sceneStatusFailed_ = true;
            sceneStatus_ = "Model referenced by scene was not found: " + modelPath.string();
            return false;
        }
    }

    clearSceneData();
    for (const std::filesystem::path& modelPath : modelPaths) {
        if (!importModelResource(modelPath)) {
            sceneStatusFailed_ = true;
            sceneStatus_ = "Failed to load scene model: " + resourceImportStatus_;
            return false;
        }
    }

    if (sceneHasSun) {
        createSunMesh();
        rebuildSceneGeometryResources();
    }

    currentScenePath_ = resolvedScenePath;
    loadTransformSettings();
    for (const SceneModelResource& model : sceneModelResources_) {
        refreshModelResourceCacheFromScene(model.path);
    }
    createParticleTextures();
    rebuildMaterialResources();
    discoverSceneResources();
    scenePathBuffer_.fill('\0');
    const std::string pathString = currentScenePath_.string();
    const std::size_t copyCount = std::min(pathString.size(), scenePathBuffer_.size() - 1);
    std::copy_n(pathString.data(), copyCount, scenePathBuffer_.data());
    selectedModelNode_ = nullptr;
    selectedResourceType_ = ResourceSelectionType::Model;
    selectedResourceMaterialIndex_ = -1;
    updateBoundingBoxVertices();
    sceneStatus_ = "Loaded scene " + currentScenePath_.filename().string() + ".";
    return true;
}

void Renderer::loadResourceSettings() {
    // Models are scene content now. Start with a blank scene instead of
    // implicitly importing the compile-time demo model or legacy ini entry.
    sceneModelResources_.clear();
    currentScenePath_.clear();
    sceneStatus_.clear();
    sceneStatusFailed_ = false;

    modelImportPathBuffer_.fill('\0');
    scenePathBuffer_.fill('\0');
}

void Renderer::loadTransformSettings() {
    if (currentScenePath_.empty()) {
        return;
    }

    const std::filesystem::path configPath(currentScenePath_);
    std::ifstream file(configPath);
    if (!file.is_open()) {
        return;
    }

    removedSceneNodeIds_.clear();

    glm::vec3 legacyTranslation(0.0f);
    glm::vec3 legacyRotationDegrees(0.0f);
    float legacyScaleFactor = 1.0f;
    bool hasLegacyTransformSettings = false;
    glm::vec3 ambientLightColor = ambientLightColor_;
    float ambientLightIntensity = ambientLightIntensity_;
    glm::vec3 sunLightDirection = sunLightDirection_;
    glm::vec3 sunLightColor = sunLightColor_;
    float sunLightIntensity = sunLightIntensity_;
    std::uint32_t postProcessEffect = postProcessEffect_;
    float postProcessStrength = postProcessStrength_;
    float postProcessParameter = postProcessParameter_;
    float postProcessSplitPosition = postProcessSplitPosition_;
    int postProcessSplitView = postProcessSplitView_ ? 1 : 0;
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
    struct PendingNodeRender {
        std::uint32_t nodeId = 0;
        bool enabled = true;
    };
    struct PendingRemovedNode {
        std::uint32_t nodeId = 0;
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
    std::vector<PendingNodeRender> pendingNodeRenderSettings;
    std::vector<PendingRemovedNode> pendingRemovedNodes;
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
        } else if (key == "node_render") {
            PendingNodeRender render;
            int enabled = 1;
            if (!(file >> render.nodeId >> enabled) || (enabled != 0 && enabled != 1)) {
                valid = false;
                break;
            }
            render.enabled = enabled != 0;
            pendingNodeRenderSettings.push_back(render);
        } else if (key == "node_removed") {
            PendingRemovedNode removed;
            if (!(file >> removed.nodeId)) {
                valid = false;
                break;
            }
            pendingRemovedNodes.push_back(removed);
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
        } else if (key == "shader_demo") {
            if (!(file >> postProcessEffect >> postProcessStrength >> postProcessParameter >>
                  postProcessSplitPosition >> postProcessSplitView) ||
                (postProcessSplitView != 0 && postProcessSplitView != 1)) {
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
        std::isfinite(sunLightIntensity) && std::isfinite(postProcessStrength) &&
        std::isfinite(postProcessParameter) && std::isfinite(postProcessSplitPosition);
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

    for (const PendingRemovedNode& removed : pendingRemovedNodes) {
        if (!removeSceneNodeById(removed.nodeId)) {
            // This also covers generated objects (such as particle systems)
            // that are restored from their own settings later in this method.
            removedSceneNodeIds_.insert(removed.nodeId);
        }
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
    postProcessEffect_ = std::min<std::uint32_t>(
        postProcessEffect_, static_cast<std::uint32_t>(kPostProcessDemos.size() - 1)
    );
    postProcessStrength_ = std::clamp(postProcessStrength, 0.0f, 2.0f);
    postProcessParameter_ = std::clamp(postProcessParameter, 0.0f, 1.0f);
    postProcessSplitPosition_ = std::clamp(postProcessSplitPosition, 0.1f, 0.9f);
    postProcessSplitView_ = postProcessSplitView != 0;
    for (PendingParticleSystem& pending : pendingParticleSystems) {
        if (removedSceneNodeIds_.contains(pending.nodeId)) {
            continue;
        }
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
    for (const PendingNodeRender& render : pendingNodeRenderSettings) {
        ModelNode* node = findSceneNodeById(meshData_, render.nodeId);
        if (node == nullptr) {
            continue;
        }
        node->renderEnabled = render.enabled;
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

bool Renderer::saveTransformSettings() const {
    if (currentScenePath_.empty()) {
        return false;
    }

    const std::filesystem::path configPath(currentScenePath_);
    std::ofstream file(configPath, std::ios::trunc);
    if (!file.is_open()) {
        std::cerr << "warning: failed to save VulkCore scene settings to '"
                  << configPath.string() << "'\n";
        return false;
    }

    file << "# VulkCore scene\n"
         << std::setprecision(9);
    for (const SceneModelResource& model : sceneModelResources_) {
        file << "model_resource_path " << std::quoted(model.path.string()) << ' '
             << model.rootNodeId << "\n";
    }
    file << "sun_present " << (sunPresent_ ? 1 : 0) << "\n"
         << "ambient_color " << ambientLightColor_.r << ' '
         << ambientLightColor_.g << ' ' << ambientLightColor_.b << "\n"
         << "ambient_intensity " << ambientLightIntensity_ << "\n"
         << "sun_light_direction " << sunLightDirection_.x << ' '
         << sunLightDirection_.y << ' ' << sunLightDirection_.z << "\n"
         << "sun_light_color " << sunLightColor_.r << ' '
         << sunLightColor_.g << ' ' << sunLightColor_.b << "\n"
         << "sun_light_intensity " << sunLightIntensity_ << "\n"
         << "shader_demo " << postProcessEffect_ << ' '
         << postProcessStrength_ << ' ' << postProcessParameter_ << ' '
         << postProcessSplitPosition_ << ' ' << (postProcessSplitView_ ? 1 : 0) << "\n";
    for (const std::uint32_t nodeId : removedSceneNodeIds_) {
        file << "node_removed " << nodeId << "\n";
    }
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
    saveNodeRenderSettings(file, meshData_.hierarchy);
    for (const ModelNode& node : meshData_.additionalNodes) {
        saveNodeRenderSettings(file, node);
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
    return static_cast<bool>(file);
}

bool Renderer::removeSceneNodeById(std::uint32_t nodeId) {
    std::vector<std::uint32_t> removedNodeIds;
    bool removed = false;
    const bool removingSun = sunPresent_ && nodeId == sunNodeId_;
    if (meshData_.hierarchy.nodeId == nodeId) {
        if (meshData_.hierarchy.name == "Scene") {
            clearSceneData();
            selectedModelNode_ = nullptr;
            updateBoundingBoxVertices();
            return true;
        }
        collectModelNodeIds(meshData_.hierarchy, removedNodeIds);
        ModelNode replacement;
        replacement.nodeId = nodeId;
        replacement.renderEnabled = false;
        meshData_.hierarchy = std::move(replacement);
        removed = true;
    } else if (removeModelNodeFromChildren(meshData_.hierarchy, nodeId, removedNodeIds)) {
        removed = true;
    } else {
        const auto nodeIt = std::find_if(
            meshData_.additionalNodes.begin(),
            meshData_.additionalNodes.end(),
            [nodeId](const ModelNode& node) {
                return node.nodeId == nodeId;
            }
        );
        if (nodeIt != meshData_.additionalNodes.end()) {
            collectModelNodeIds(*nodeIt, removedNodeIds);
            meshData_.additionalNodes.erase(nodeIt);
            removed = true;
        }
    }
    if (!removed) {
        return false;
    }

    bool hasParticleResources = false;
    for (const std::uint32_t removedNodeId : removedNodeIds) {
        hasParticleResources = hasParticleResources ||
            particleSystems_.contains(removedNodeId) ||
            particleTextures_.contains(removedNodeId) ||
            particleDescriptorSets_.contains(removedNodeId);
    }
    if (hasParticleResources && context_ != nullptr) {
        context_->waitIdle();
    }

    for (const std::uint32_t removedNodeId : removedNodeIds) {
        removedSceneNodeIds_.insert(removedNodeId);
        particleSystems_.erase(removedNodeId);
        particleTextures_.erase(removedNodeId);
        const auto descriptorIt = particleDescriptorSets_.find(removedNodeId);
        if (descriptorIt != particleDescriptorSets_.end()) {
            if (descriptorPool_ != VK_NULL_HANDLE) {
                vkFreeDescriptorSets(
                    context_->device(),
                    descriptorPool_,
                    kMaxFramesInFlight,
                    descriptorIt->second.data()
                );
            }
            particleDescriptorSets_.erase(descriptorIt);
        }
        sceneNodeTransforms_.erase(removedNodeId);
        sceneNodeRenderEnabled_.erase(removedNodeId);
    }
    if (std::find(
            removedNodeIds.begin(),
            removedNodeIds.end(),
            particleTexturePathNodeId_
        ) != removedNodeIds.end()) {
        particleTexturePathNodeId_ = std::numeric_limits<std::uint32_t>::max();
        particleTexturePathBuffer_.fill('\0');
    }

    sceneModelResources_.erase(
        std::remove_if(
            sceneModelResources_.begin(),
            sceneModelResources_.end(),
            [&removedNodeIds](const SceneModelResource& model) {
                return std::find(
                    removedNodeIds.begin(),
                    removedNodeIds.end(),
                    model.rootNodeId
                ) != removedNodeIds.end();
            }
        ),
        sceneModelResources_.end()
    );

    recalculateSceneBounds();
    selectedModelNode_ = nullptr;
    if (removingSun) {
        removeSunMesh();
        rebuildSceneGeometryResources();
    }
    updateBoundingBoxVertices();
    return true;
}



} // namespace VulkCore

