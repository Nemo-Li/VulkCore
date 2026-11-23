#include "RendererImplementation.hpp"

#include "vulkcore/Physics/PhysicsWorld.hpp"
#include "vulkcore/Terrain/Grass.hpp"
#include "vulkcore/Terrain/Terrain.hpp"

#include <cerrno>
#include <cstdio>

namespace VulkCore {

namespace {


} // namespace

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
    if (physicsWorld_) {
        physicsWorld_->clear();
    }
    modelGravity_.clear();
    enemyPatrols_.clear();
    terrainScene_.reset();
    grassCoverage_ = {};
    terrainResourcePath_.clear();
    waterResourcePath_.clear();
    playerController_.clear();
    playerSpawnPosition_.reset();
    playerModelResourcePath_.clear();
    playerModelAutoScale_ = 1.0f;
    playerModelGroundOffset_ = 0.0f;
    playerRunAnimationClipIndex_.reset();
    playerAttackAnimationClipIndex_.reset();
    playerLightningAnimationClipIndex_.reset();
    playerFlameAnimationClipIndex_.reset();
    MaterialData defaultMaterial;
    defaultMaterial.name = "DefaultMaterial";
    meshData_.materials.push_back(std::move(defaultMaterial));
    modelCenter_ = glm::vec3(0.0f);
    modelScale_ = 1.0f;
    cameraTarget_ = glm::vec3(0.0f);
    cameraBasisInitialized_ = false;
    postProcessSplitView_ = false;
    sunNodeId_ = 0;
    sunMaterialIndex_ = 0;
    sunPartIndex_ = 0;
    sunVertexStart_ = 0;
    sunIndexStart_ = 0;
    sunPresent_ = false;
    tornadoMeshMaterialIndex_ = 0;
    tornadoMeshPartIndex_ = 0;
    tornadoMeshPresent_ = false;
    tornadoMeshVisible_ = false;
    tornadoMeshAlwaysVisible_ = false;
    tornadoMeshTransform_ = glm::mat4(1.0f);
}

void Renderer::appendTerrainToScene(
    const TerrainScene& terrain,
    const std::filesystem::path& terrainRoot
) {
    TerrainMeshOptions meshOptions;
    const TerrainMeshData terrainMesh = terrain.buildMesh(meshOptions);
    if (terrainMesh.vertices.empty() || terrainMesh.indices.empty() ||
        terrainMesh.chunks.empty()) {
        throw std::runtime_error("BOTW terrain contains no renderable heightmap data");
    }
    if (meshData_.vertices.size() > std::numeric_limits<std::uint32_t>::max() -
            terrainMesh.vertices.size() ||
        meshData_.indices.size() > std::numeric_limits<std::uint32_t>::max() -
            terrainMesh.indices.size()) {
        throw std::runtime_error("BOTW terrain exceeds the renderer's 32-bit mesh limits");
    }
    if (!physicsWorld_) {
        throw std::runtime_error("physics world is not initialized");
    }
    physicsWorld_->setTerrain(terrainMesh);

    MaterialData terrainMaterial;
    terrainMaterial.name = "BOTW Terrain (TSCB landform atlas)";
    terrainMaterial.baseColor = glm::vec4(1.0f);
    terrainMaterial.roughness = 0.95f;
    terrainMaterial.texturePath = terrainRoot / "textures" / "terrain_atlas.png";
    terrainMaterial.shaderConfiguration.shader(ShaderStage::Vertex) =
        "shader_terrain.vert";
    terrainMaterial.shaderConfiguration.shader(ShaderStage::Fragment) =
        "shader_terrain.frag";
    const std::uint32_t terrainMaterialIndex = static_cast<std::uint32_t>(
        meshData_.materials.size()
    );
    meshData_.materials.push_back(std::move(terrainMaterial));

    const std::uint32_t vertexOffset = static_cast<std::uint32_t>(meshData_.vertices.size());
    const std::uint32_t indexOffset = static_cast<std::uint32_t>(meshData_.indices.size());
    meshData_.vertices.insert(
        meshData_.vertices.end(),
        terrainMesh.vertices.begin(),
        terrainMesh.vertices.end()
    );
    meshData_.bindPoseVertices.insert(
        meshData_.bindPoseVertices.end(),
        terrainMesh.vertices.begin(),
        terrainMesh.vertices.end()
    );
    meshData_.skinSourceVertices.insert(
        meshData_.skinSourceVertices.end(),
        terrainMesh.vertices.begin(),
        terrainMesh.vertices.end()
    );
    for (const std::uint32_t index : terrainMesh.indices) {
        meshData_.indices.push_back(index + vertexOffset);
    }

    ModelNode terrainNode;
    terrainNode.name = "BOTW Terrain";
    terrainNode.nodeId = kTerrainSceneNodeId;
    terrainNode.meshCount = 0;
    terrainNode.globalTransform = glm::mat4(1.0f);
    terrainNode.hasBounds = false;

    for (std::size_t chunkIndex = 0; chunkIndex < terrainMesh.chunks.size(); ++chunkIndex) {
        const TerrainChunkMesh& chunkMesh = terrainMesh.chunks[chunkIndex];
        if (chunkMesh.indexCount == 0) {
            --terrainNode.meshCount;
            continue;
        }
        if (chunkIndex > std::numeric_limits<std::uint32_t>::max() - kTerrainChunkNodeBase) {
            throw std::runtime_error("BOTW terrain chunk node IDs overflow 32-bit storage");
        }
        const std::uint32_t chunkNodeId = kTerrainChunkNodeBase +
            static_cast<std::uint32_t>(chunkIndex);
        const std::uint32_t partIndex = static_cast<std::uint32_t>(meshData_.parts.size());
        meshData_.parts.push_back({
            indexOffset + static_cast<std::uint32_t>(chunkMesh.indexOffset),
            static_cast<std::uint32_t>(chunkMesh.indexCount),
            terrainMaterialIndex,
            chunkNodeId,
            TerrainChunkBounds{chunkMesh.boundsMinimum, chunkMesh.boundsMaximum}
        });
        ++terrainNode.meshCount;

        ModelNode chunkNode;
        chunkNode.name = "Chunk LOD" + std::to_string(chunkMesh.lod) + " (" +
            std::to_string(chunkMesh.coordinate.x) + ", " +
            std::to_string(chunkMesh.coordinate.y) + ")";
        chunkNode.nodeId = chunkNodeId;
        chunkNode.meshCount = chunkMesh.tileCount;
        chunkNode.globalTransform = glm::mat4(1.0f);
        chunkNode.meshPartIndices.push_back(partIndex);
        chunkNode.hasBounds = true;
        chunkNode.boundsMinimum = chunkMesh.boundsMinimum;
        chunkNode.boundsMaximum = chunkMesh.boundsMaximum;
        terrainNode.children.push_back(std::move(chunkNode));

        if (!terrainNode.hasBounds) {
            terrainNode.boundsMinimum = chunkMesh.boundsMinimum;
            terrainNode.boundsMaximum = chunkMesh.boundsMaximum;
            terrainNode.hasBounds = true;
        } else {
            terrainNode.boundsMinimum = glm::min(
                terrainNode.boundsMinimum,
                chunkMesh.boundsMinimum
            );
            terrainNode.boundsMaximum = glm::max(
                terrainNode.boundsMaximum,
                chunkMesh.boundsMaximum
            );
        }
    }

    if (terrainNode.children.empty()) {
        throw std::runtime_error("BOTW terrain chunks contain no triangles");
    }
    meshData_.additionalNodes.push_back(std::move(terrainNode));
    recalculateSceneBounds();
}

void Renderer::appendWaterToScene(const TerrainScene& terrain) {
    const TerrainMeshData waterMesh = terrain.buildWaterMesh();
    if (waterMesh.vertices.empty() || waterMesh.indices.empty() ||
        waterMesh.chunks.empty()) {
        return;
    }
    if (meshData_.vertices.size() > std::numeric_limits<std::uint32_t>::max() -
            waterMesh.vertices.size() ||
        meshData_.indices.size() > std::numeric_limits<std::uint32_t>::max() -
            waterMesh.indices.size()) {
        throw std::runtime_error("BOTW water exceeds the renderer's 32-bit mesh limits");
    }

    MaterialData waterMaterial;
    waterMaterial.name = "BOTW Water (terrain_water)";
    waterMaterial.baseColor = glm::vec4(0.84f, 0.96f, 1.0f, 1.0f);
    waterMaterial.metallic = 0.05f;
    waterMaterial.roughness = 0.18f;
    waterMaterial.opacity = 0.9f;
    waterMaterial.blendMode = MaterialData::BlendMode::AlphaBlend;
    waterMaterial.depthWrite = false;
    waterMaterial.doubleSided = true;
    waterMaterial.shaderConfiguration.shader(ShaderStage::Vertex) =
        "shader_water.vert";
    waterMaterial.shaderConfiguration.shader(ShaderStage::Fragment) =
        "shader_water.frag";
    waterMaterial.waterShaderSettings.flowDirection = glm::vec2(0.0f, 1.0f);
    waterMaterial.waterShaderSettings.flowSpeed = 0.18f;
    waterMaterial.waterShaderSettings.flowStrength = 0.85f;
    waterMaterial.waterShaderSettings.flowTurbulence = 0.3f;
    waterMaterial.waterShaderSettings.waveScale = 1.0f;
    waterMaterial.waterShaderSettings.waveHeight = 0.035f;
    const std::uint32_t waterMaterialIndex = static_cast<std::uint32_t>(
        meshData_.materials.size()
    );
    meshData_.materials.push_back(std::move(waterMaterial));

    const std::uint32_t vertexOffset = static_cast<std::uint32_t>(meshData_.vertices.size());
    const std::uint32_t indexOffset = static_cast<std::uint32_t>(meshData_.indices.size());
    meshData_.vertices.insert(
        meshData_.vertices.end(),
        waterMesh.vertices.begin(),
        waterMesh.vertices.end()
    );
    meshData_.bindPoseVertices.insert(
        meshData_.bindPoseVertices.end(),
        waterMesh.vertices.begin(),
        waterMesh.vertices.end()
    );
    meshData_.skinSourceVertices.insert(
        meshData_.skinSourceVertices.end(),
        waterMesh.vertices.begin(),
        waterMesh.vertices.end()
    );
    for (const std::uint32_t index : waterMesh.indices) {
        meshData_.indices.push_back(index + vertexOffset);
    }

    ModelNode waterNode;
    waterNode.name = "BOTW Water";
    waterNode.nodeId = kWaterSceneNodeId;
    waterNode.globalTransform = glm::mat4(1.0f);
    waterNode.hasBounds = false;
    for (std::size_t chunkIndex = 0; chunkIndex < waterMesh.chunks.size(); ++chunkIndex) {
        const TerrainChunkMesh& chunkMesh = waterMesh.chunks[chunkIndex];
        if (chunkMesh.indexCount == 0) {
            continue;
        }
        if (chunkIndex > std::numeric_limits<std::uint32_t>::max() -
                kWaterChunkNodeBase) {
            throw std::runtime_error("BOTW water tile node IDs overflow 32-bit storage");
        }
        const std::uint32_t chunkNodeId = kWaterChunkNodeBase +
            static_cast<std::uint32_t>(chunkIndex);
        const std::uint32_t partIndex = static_cast<std::uint32_t>(meshData_.parts.size());
        meshData_.parts.push_back({
            indexOffset + static_cast<std::uint32_t>(chunkMesh.indexOffset),
            static_cast<std::uint32_t>(chunkMesh.indexCount),
            waterMaterialIndex,
            chunkNodeId
        });

        ModelNode chunkNode;
        chunkNode.name = "Water tile (" +
            std::to_string(chunkMesh.coordinate.x) + ", " +
            std::to_string(chunkMesh.coordinate.y) + ")";
        chunkNode.nodeId = chunkNodeId;
        chunkNode.meshCount = chunkMesh.tileCount;
        chunkNode.globalTransform = glm::mat4(1.0f);
        chunkNode.meshPartIndices.push_back(partIndex);
        chunkNode.hasBounds = true;
        chunkNode.boundsMinimum = chunkMesh.boundsMinimum;
        chunkNode.boundsMaximum = chunkMesh.boundsMaximum;
        waterNode.children.push_back(std::move(chunkNode));

        if (!waterNode.hasBounds) {
            waterNode.boundsMinimum = chunkMesh.boundsMinimum;
            waterNode.boundsMaximum = chunkMesh.boundsMaximum;
            waterNode.hasBounds = true;
        } else {
            waterNode.boundsMinimum = glm::min(
                waterNode.boundsMinimum,
                chunkMesh.boundsMinimum
            );
            waterNode.boundsMaximum = glm::max(
                waterNode.boundsMaximum,
                chunkMesh.boundsMaximum
            );
        }
    }

    if (!waterNode.children.empty()) {
        meshData_.additionalNodes.push_back(std::move(waterNode));
        recalculateSceneBounds();
    }
}

void Renderer::appendPlayerToScene() {
    if (!terrainScene_ || findSceneNodeById(meshData_, kPlayerSceneNodeId) != nullptr) {
        return;
    }

    const CapsuleMeshData capsule = createCapsuleMesh(
        PlayerController::kBotwLinkHeight,
        PlayerController::kBotwLinkRadius
    );
    if (capsule.vertices.empty() || capsule.indices.empty()) {
        throw std::runtime_error("player capsule mesh contains no renderable triangles");
    }
    if (meshData_.vertices.size() > std::numeric_limits<std::uint32_t>::max() -
            capsule.vertices.size() ||
        meshData_.indices.size() > std::numeric_limits<std::uint32_t>::max() -
            capsule.indices.size()) {
        throw std::runtime_error("player capsule exceeds the renderer's 32-bit mesh limits");
    }

    MaterialData playerMaterial;
    playerMaterial.name = "BOTW Link Capsule";
    playerMaterial.baseColor = glm::vec4(0.18f, 0.38f, 0.82f, 1.0f);
    playerMaterial.roughness = 0.7f;
    playerMaterial.doubleSided = true;
    const std::uint32_t materialIndex = static_cast<std::uint32_t>(
        meshData_.materials.size()
    );
    meshData_.materials.push_back(std::move(playerMaterial));

    const std::uint32_t vertexOffset = static_cast<std::uint32_t>(meshData_.vertices.size());
    const std::uint32_t indexOffset = static_cast<std::uint32_t>(meshData_.indices.size());
    for (const Vertex& vertex : capsule.vertices) {
        meshData_.vertices.push_back(vertex);
        meshData_.bindPoseVertices.push_back(vertex);
        meshData_.skinSourceVertices.push_back(vertex);
    }
    for (const std::uint32_t index : capsule.indices) {
        meshData_.indices.push_back(index + vertexOffset);
    }

    ModelNode playerNode;
    playerNode.name = "Player (Link Capsule)";
    playerNode.nodeId = kPlayerSceneNodeId;
    playerNode.objectType = SceneObjectType::Player;
    playerNode.meshCount = 1;
    playerNode.globalTransform = glm::mat4(1.0f);
    playerNode.meshPartIndices.push_back(static_cast<std::uint32_t>(meshData_.parts.size()));
    playerNode.hasBounds = true;
    playerNode.boundsMinimum = glm::vec3(
        -PlayerController::kBotwLinkRadius,
        0.0f,
        -PlayerController::kBotwLinkRadius
    );
    playerNode.boundsMaximum = glm::vec3(
        PlayerController::kBotwLinkRadius,
        PlayerController::kBotwLinkHeight,
        PlayerController::kBotwLinkRadius
    );
    meshData_.parts.push_back({
        indexOffset,
        static_cast<std::uint32_t>(capsule.indices.size()),
        materialIndex,
        kPlayerSceneNodeId
    });
    meshData_.additionalNodes.push_back(std::move(playerNode));
    recalculateSceneBounds();
}

void Renderer::appendGrassToScene(const TerrainScene& terrain) {
    const auto makeMaterial = [&](const char* name, const char* texture) {
        MaterialData material;
        material.name = name;
        material.roughness = 0.92f;
        material.doubleSided = true;
        if (texture) material.texturePath = assetsDirectory_ / "textures" / texture;
        material.shaderConfiguration.shader(ShaderStage::Vertex) = kGrassVertexShader;
        material.shaderConfiguration.shader(ShaderStage::Fragment) = "shader_grass.frag";

        const auto index = static_cast<std::uint32_t>(meshData_.materials.size());
        meshData_.materials.push_back(std::move(material));
        return index;
    };
    const auto grassMaterial = makeMaterial("World Grass", nullptr);
    const auto flowerMaterial = makeMaterial("Meadow Wildflowers", "botw_wildflowers.png");
    ModelNode grassNode;
    grassNode.name = "World Grass";
    grassNode.nodeId = kGrassSceneNodeId;
    grassNode.globalTransform = glm::mat4(1.0f);
    grassNode.hasBounds = true;
    grassNode.boundsMinimum = terrain.boundsMinimum();
    grassNode.boundsMaximum = terrain.boundsMaximum();

    constexpr std::array<const char*, kGrassLods.size()> lodNames{
        "LOD0 Near Curved Blades", "Wildflower Cross Cards", "Small Yellow Flowers"
    };
    for (std::size_t layer = 0; layer < lodNames.size(); ++layer) {
        const auto lod = kGrassLods[layer];
        ModelNode node;
        node.name = lodNames[layer];
        node.nodeId = kGrassLodNodeBase + static_cast<std::uint32_t>(lod);
        node.globalTransform = glm::mat4(1.0f);
        node.meshCount = 1;
        node.meshPartIndices.push_back(static_cast<std::uint32_t>(meshData_.parts.size()));
        // Both flower layers use the existing material slot. Only the original
        // layer samples its texture, and saved material indices stay stable.
        const auto material = lod == GrassLod::NearMesh ? grassMaterial : flowerMaterial;
        // Each layer has a regular scene part for material/visibility controls.
        // Its instance ranges live outside the shared scene mesh.
        meshData_.parts.push_back({0, 0, material, node.nodeId});
        grassNode.children.push_back(std::move(node));
        ++grassNode.meshCount;
    }
    meshData_.additionalNodes.push_back(std::move(grassNode));
}

void Renderer::updatePlayerController(float deltaSeconds, bool viewportHovered) {
    if (!terrainScene_ || !playerController_.active()) {
        if (terrainScene_ && !modelGravity_.empty()) physicsWorld_->simulate(deltaSeconds);
        return;
    }
    postProcessSplitView_ = false;
    const ImGuiIO& io = ImGui::GetIO();
#ifdef VULKCORE_OHOS
    // Touch look is routed to the player separately from ImGui's pointer.
    viewportHovered = true;
#endif
    // A mouse drag can make an ImGui item active without requiring text input.
    playerController_.update(
        window_,
        deltaSeconds,
        *physicsWorld_,
        sceneEditMode_ || io.WantTextInput,
        !viewportHovered || roadEditorActive()
    );
    updatePlayerAttackEffect();
    if (!roadEditorActive()) updatePlayerCamera();
}

void Renderer::updatePlayerCamera() {
    if (!terrainScene_ || !playerController_.active()) {
        cameraTarget_ = glm::vec3(0.0f);
        return;
    }
    const float safeScale = std::max(modelScale_, std::numeric_limits<float>::epsilon());
    const PlayerCameraView view = playerController_.cameraView(*physicsWorld_);
    cameraTarget_ = safeScale * (view.target - modelCenter_);
    cameraPosition_ = safeScale * (view.position - modelCenter_);
    cameraBasisInitialized_ = false;
}

const Renderer::SceneModelResource* Renderer::sceneModelResourceForNode(
    const ModelNode& node
) const {
    for (const SceneModelResource& model : sceneModelResources_) {
        if (node.nodeId >= model.rootNodeId &&
            node.nodeId - model.rootNodeId < kSceneModelNodeStride) {
            return &model;
        }
    }
    return nullptr;
}

void Renderer::clearSceneData() {
    context_->waitIdle();
    selectedModelNode_ = nullptr;
    editorSelection_ = EditorSelection::None;
    editorPart_.reset();
    currentScenePath_.clear();
    roadPaint_.clear();
    roadIndexRevision_ = 0;
    for (auto& frame : roadFrameBuffers_) frame.revision = 0;
#ifndef VULKCORE_OHOS
    roadEditorCamera_.reset();
    roadBrushEnabled_ = true;
    roadTopDown_ = false;
    roadBrushDragging_ = false;
    roadStrokeHasPoint_ = false;
#endif
    skyboxEnabled_ = false;
    skyboxTexturePath_.clear();
    grassGroundTexturePath_.clear();
    grassGroundTexture_.reset();
    grassHeightScale_ = 1.0f;
    grassGrowth_ = {};
    cameraPosition_ = {0.0f, 0.0f, 4.0f};
    ambientLightColor_ = {0.18f, 0.21f, 0.24f};
    ambientLightIntensity_ = 1.0f;
    sunLightDirection_ = {0.45f, 0.55f, 0.75f};
    sunLightColor_ = {0.82f, 0.86f, 0.9f};
    sunLightIntensity_ = 1.0f;
    postProcessEffect_ = 0;
    postProcessStrength_ = 1.0f;
    postProcessParameter_ = 0.5f;
    postProcessSplitPosition_ = 0.5f;
    removedSceneNodeIds_.clear();
    sceneNodeTransforms_.clear();
    sceneNodeRenderEnabled_.clear();
    particleSystems_.clear();
    particleDrawRanges_.clear();
    particleVertices_.clear();
    hdrSettings_ = {};
    distanceFogSettings_ = {};
    authoredView_ = {};
    cameraOrbitDragging_ = false;
    transitionSettings_ = {};
    transitionPlayer_ = {};
    configureHdrComposition();
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

    vertexBuffers_.clear();
    indexBuffer_.reset();
    createVertexBuffer();
    createIndexBuffer();
    createParticleTextures();
    rebuildMaterialResources();
}

void Renderer::recalculateSceneBounds() {
    if (authoredView_.enabled || meshData_.vertices.empty() || meshData_.parts.empty()) {
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
    const std::filesystem::path& path,
    std::optional<std::uint32_t> preferredRootNodeId,
    bool playerModel
) {
    float playerAutoScale = 1.0f;
    float playerGroundOffset = 0.0f;
    if (playerModel) {
        if (!importedMesh.hierarchy.hasBounds) {
            throw std::runtime_error("player model has no measurable bounds");
        }
        const float modelHeight = importedMesh.hierarchy.boundsMaximum.y -
            importedMesh.hierarchy.boundsMinimum.y;
        if (!std::isfinite(modelHeight) ||
            modelHeight <= std::numeric_limits<float>::epsilon()) {
            throw std::runtime_error("player model has an invalid height");
        }
        playerAutoScale = PlayerController::kBotwLinkHeight / modelHeight;
        playerGroundOffset = -importedMesh.hierarchy.boundsMinimum.y;
    }

    std::uint32_t modelIndex = nextSceneModelInstanceIndex_;
    std::uint32_t nodeOffset = 0;
    if (playerModel) {
        if (preferredRootNodeId.has_value()) {
            throw std::runtime_error("a player model cannot use a scene model root node ID");
        }
        const bool duplicatePlayerModel = std::any_of(
            sceneModelResources_.begin(),
            sceneModelResources_.end(),
            [](const SceneModelResource& model) {
                return model.playerModel;
            }
        );
        if (duplicatePlayerModel ||
            findSceneNodeById(meshData_, kPlayerSceneNodeId) != nullptr) {
            throw std::runtime_error("scene already contains a player model");
        }
        nodeOffset = kPlayerSceneNodeId;
    } else if (preferredRootNodeId.has_value()) {
        const std::uint32_t rootNodeId = *preferredRootNodeId;
        if (rootNodeId == 0 ||
            (rootNodeId - 1u) % kSceneModelNodeStride != 0) {
            throw std::runtime_error("scene contains an invalid model root node ID");
        }
        modelIndex = (rootNodeId - 1u) / kSceneModelNodeStride;
        nodeOffset = rootNodeId;
        const bool duplicateRoot = std::any_of(
            sceneModelResources_.begin(),
            sceneModelResources_.end(),
            [rootNodeId](const SceneModelResource& model) {
                return model.rootNodeId == rootNodeId;
            }
        );
        if (duplicateRoot) {
            throw std::runtime_error("scene contains duplicate model root node IDs");
        }
    } else {
        if (modelIndex >= (std::numeric_limits<std::uint32_t>::max() - 1u) /
                kSceneModelNodeStride) {
            throw std::runtime_error("scene contains too many model instances");
        }
        nodeOffset = 1u + modelIndex * kSceneModelNodeStride;
        ++nextSceneModelInstanceIndex_;
    }

    const std::size_t importedNodeCount = countModelNodes(importedMesh.hierarchy);
    if (importedNodeCount > 0 &&
        nodeOffset > std::numeric_limits<std::uint32_t>::max() -
            static_cast<std::uint32_t>(importedNodeCount - 1)) {
        throw std::runtime_error("scene model node IDs overflow 32-bit storage");
    }
    if (!playerModel && preferredRootNodeId.has_value()) {
        nextSceneModelInstanceIndex_ = std::max(
            nextSceneModelInstanceIndex_,
            modelIndex + 1u
        );
    }

    const std::uint32_t vertexOffset = static_cast<std::uint32_t>(meshData_.vertices.size());
    const std::uint32_t indexOffset = static_cast<std::uint32_t>(meshData_.indices.size());
    const std::uint32_t materialOffset = static_cast<std::uint32_t>(meshData_.materials.size());
    const std::uint32_t importedMaterialCount =
        static_cast<std::uint32_t>(importedMesh.materials.size());
    const std::uint32_t partOffset = static_cast<std::uint32_t>(meshData_.parts.size());
    const std::uint32_t boneOffset = static_cast<std::uint32_t>(meshData_.skeletonBones.size());
    const std::uint32_t animationOffset = static_cast<std::uint32_t>(
        meshData_.skeletalAnimations.size()
    );
    const auto appendVertex = [boneOffset](Vertex vertex) {
        for (int influenceIndex = 0; influenceIndex < 4; ++influenceIndex) {
            if (vertex.boneWeights[influenceIndex] > 0.0f) {
                vertex.boneIndices[influenceIndex] += static_cast<int>(boneOffset);
            }
        }
        return vertex;
    };
    for (const Vertex& sourceVertex : importedMesh.vertices) {
        meshData_.vertices.push_back(appendVertex(sourceVertex));
    }
    if (importedMesh.bindPoseVertices.size() == importedMesh.vertices.size()) {
        for (const Vertex& sourceVertex : importedMesh.bindPoseVertices) {
            meshData_.bindPoseVertices.push_back(appendVertex(sourceVertex));
        }
    } else {
        for (const Vertex& sourceVertex : importedMesh.vertices) {
            meshData_.bindPoseVertices.push_back(appendVertex(sourceVertex));
        }
    }
    if (importedMesh.skinSourceVertices.size() == importedMesh.vertices.size()) {
        for (const Vertex& sourceVertex : importedMesh.skinSourceVertices) {
            meshData_.skinSourceVertices.push_back(appendVertex(sourceVertex));
        }
    } else {
        for (const Vertex& sourceVertex : importedMesh.vertices) {
            meshData_.skinSourceVertices.push_back(appendVertex(sourceVertex));
        }
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
    for (SkeletonBone bone : importedMesh.skeletonBones) {
        if (bone.nodeId != std::numeric_limits<std::uint32_t>::max()) {
            bone.nodeId += nodeOffset;
        }
        meshData_.skeletonBones.push_back(std::move(bone));
    }
    meshData_.skinnedMeshCount += importedMesh.skinnedMeshCount;
    meshData_.skinWeightCount += importedMesh.skinWeightCount;
    for (SkeletalAnimationClip animation : importedMesh.skeletalAnimations) {
        for (SkeletalAnimationChannel& channel : animation.channels) {
            if (channel.nodeId != std::numeric_limits<std::uint32_t>::max()) {
                channel.nodeId += nodeOffset;
            }
            if (channel.boneIndex >= 0) {
                channel.boneIndex += static_cast<std::int32_t>(boneOffset);
            }
        }
        meshData_.skeletalAnimations.push_back(std::move(animation));
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

    const std::uint32_t rootNodeId = importedMesh.hierarchy.nodeId;
    for (SkeletalAnimationInstance instance : importedMesh.skeletalAnimationInstances) {
        instance.rootNodeId += nodeOffset;
        instance.ownerNodeId = rootNodeId;
        instance.vertexStart += vertexOffset;
        instance.boneStart += boneOffset;
        instance.animationStart += animationOffset;
        meshData_.skeletalAnimationInstances.push_back(std::move(instance));
    }

    if (meshData_.hierarchy.name.empty()) {
        meshData_.hierarchy.name = "Scene";
    }
    if (playerModel) {
        playerModelResourcePath_ = normalizedPath(path);
        playerModelAutoScale_ = playerAutoScale;
        playerModelGroundOffset_ = playerGroundOffset;
        importedMesh.hierarchy.name = "Player (" + path.filename().string() + ")";
        importedMesh.hierarchy.objectType = SceneObjectType::Player;
        meshData_.additionalNodes.push_back(std::move(importedMesh.hierarchy));
    } else {
        meshData_.hierarchy.children.push_back(std::move(importedMesh.hierarchy));
    }
    sceneModelResources_.push_back({
        rootNodeId,
        normalizedPath(path),
        materialOffset,
        importedMaterialCount,
        playerModel,
    });
    if (playerModel) {
        configurePlayerRunAnimation();
    }
    recalculateSceneBounds();
}

bool Renderer::createNewEmptyScene() {
    const auto path = createSceneFile();
    if (path.empty()) return false;
    if (!loadSceneResource(path)) {
        std::error_code error;
        std::filesystem::remove(path, error);
        return false;
    }
    sceneStatus_ = "Created empty scene " + currentScenePath_.filename().string() + ".";
    return true;
}

std::filesystem::path Renderer::createSceneFile() {
    std::error_code error;
    const auto directory = scenesDirectory_.empty()
        ? std::filesystem::current_path(error) / "scenes" : scenesDirectory_;
    if (!error) std::filesystem::create_directories(directory, error);
    if (error) {
        sceneStatusFailed_ = true;
        sceneStatus_ = "Failed to create scene directory: " + error.message();
        return {};
    }

    for (std::size_t suffix = 0; ; ++suffix) {
        const auto path = directory / (suffix == 0
            ? "Scene.scene" : "Scene_" + std::to_string(suffix) + ".scene");
        // Exclusive creation keeps both existing scenes and concurrent creations intact.
        std::FILE* file = nullptr;
#ifdef _WIN32
        const int openError = _wfopen_s(&file, path.c_str(), L"wbx");
#else
        file = std::fopen(path.c_str(), "wbx");
        const int openError = file ? 0 : errno;
#endif
        if (!file) {
            if (openError == EEXIST || std::filesystem::exists(std::filesystem::symlink_status(path, error))) {
                continue;
            }
            sceneStatusFailed_ = true;
            sceneStatus_ = "Failed to create scene file: " +
                std::error_code(openError, std::generic_category()).message();
            return {};
        }

        const bool written = std::fputs("# VulkCore scene\n", file) >= 0;
        const bool closed = std::fclose(file) == 0;
        if (written && closed) return path;

        std::filesystem::remove(path, error);
        sceneStatusFailed_ = true;
        sceneStatus_ = "Failed to write scene file: " + path.string();
        return {};
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
    const bool untitled = path.empty();
    std::filesystem::path resolvedPath = untitled ? createSceneFile() : path;
    if (resolvedPath.empty()) return false;
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
    if (!untitled && !resolvedPath.parent_path().empty()) {
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
        if (untitled) std::filesystem::remove(resolvedPath, error);
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
    const bool isTornadoScene = resolvedScenePath.filename() == "tornado.scene";

    struct ModelResourceReference {
        std::filesystem::path path;
        std::optional<std::uint32_t> rootNodeId;
    };
    std::vector<ModelResourceReference> modelReferences;
    std::filesystem::path terrainPath;
    std::filesystem::path waterPath;
    std::filesystem::path playerModelPath;
    std::optional<glm::vec3> playerSpawnPosition;
    bool sceneHasSun = false;
    std::optional<bool> sceneHasSkybox;
    std::string skyboxImage;
    std::string grassGroundImage;
    float grassHeightScale = 1.0f;
    GrassGrowthSettings grassGrowth;
    bool sceneHasPlayer = true;
    RoadPaint savedRoadPaint;
    std::ifstream sceneFile(resolvedScenePath);
    if (!sceneFile.is_open()) {
        sceneStatusFailed_ = true;
        sceneStatus_ = "Scene file could not be opened: " + resolvedScenePath.string();
        return false;
    }
    struct SavedAnimation {
        std::string clipName;
        std::uint32_t clipIndex = 0;
        int enabled = 0, looping = 0, playing = 0;
        float speed = 1.0f;
    };
    std::unordered_map<std::uint32_t, SavedAnimation> savedAnimations;
    std::unordered_map<std::uint32_t, EnemyPatrol> savedPatrols;
    std::vector<std::uint32_t> gravityNodeIds;
    std::string key;
    while (sceneFile >> key) {
        if (key == "node_patrol") {
            std::uint32_t id = 0;
            int enabled = 0;
            EnemyPatrol patrol;
            if (!(sceneFile >> id >> enabled >> patrol.offset.x >> patrol.offset.y >> patrol.speed >>
                  patrol.wait >> patrol.facingOffsetDegrees >> std::quoted(patrol.walkClip) >> std::quoted(patrol.idleClip)) ||
                !std::isfinite(patrol.offset.x) || !std::isfinite(patrol.offset.y) ||
                !std::isfinite(patrol.speed) || !std::isfinite(patrol.wait) || !std::isfinite(patrol.facingOffsetDegrees)) {
                sceneStatusFailed_ = true;
                sceneStatus_ = "Scene file has invalid patrol settings.";
                return false;
            }
            patrol.enabled = enabled != 0;
            patrol.offset = glm::clamp(patrol.offset, glm::vec2(-100.0f), glm::vec2(100.0f));
            patrol.speed = std::clamp(patrol.speed, 0.1f, 20.0f);
            patrol.wait = std::clamp(patrol.wait, 0.0f, 30.0f);
            patrol.facingOffsetDegrees = std::clamp(patrol.facingOffsetDegrees, -180.0f, 180.0f);
            savedPatrols.insert_or_assign(id, std::move(patrol));
        } else if (key == "node_skeletal") {
            std::uint32_t id = 0;
            SavedAnimation animation;
            if (!(sceneFile >> id >> animation.clipIndex >> std::quoted(animation.clipName) >>
                  animation.enabled >> animation.looping >> animation.playing >> animation.speed) ||
                !std::isfinite(animation.speed)) {
                sceneStatusFailed_ = true;
                sceneStatus_ = "Scene file has invalid skeletal playback settings.";
                return false;
            }
            savedAnimations.insert_or_assign(id, std::move(animation));
        } else if (key == "node_gravity") {
            std::uint32_t id = 0;
            if (!(sceneFile >> id)) {
                sceneStatusFailed_ = true;
                sceneStatus_ = "Scene file has an invalid gravity node ID.";
                return false;
            }
            gravityNodeIds.push_back(id);
        } else if (key == "model_resource_path") {
            std::string configuredPath;
            if (!(sceneFile >> std::quoted(configuredPath))) {
                sceneStatusFailed_ = true;
                sceneStatus_ = "Scene file has an invalid model path.";
                return false;
            }
            std::string modelReferenceSuffix;
            std::getline(sceneFile, modelReferenceSuffix);
            std::istringstream suffixStream(modelReferenceSuffix);
            std::uint64_t savedRootNodeId = 0;
            std::optional<std::uint32_t> rootNodeId;
            if (suffixStream >> savedRootNodeId) {
                if (savedRootNodeId == 0 ||
                    savedRootNodeId > std::numeric_limits<std::uint32_t>::max()) {
                    sceneStatusFailed_ = true;
                    sceneStatus_ = "Scene file has an invalid model root node ID.";
                    return false;
                }
                rootNodeId = static_cast<std::uint32_t>(savedRootNodeId);
            }
            modelReferences.push_back({std::move(configuredPath), rootNodeId});
        } else if (key == "terrain_resource") {
            std::string configuredPath;
            if (!(sceneFile >> std::quoted(configuredPath))) {
                sceneStatusFailed_ = true;
                sceneStatus_ = "Scene file has an invalid terrain path.";
                return false;
            }
            terrainPath = std::move(configuredPath);
        } else if (key == "terrain_road") {
            if (!savedRoadPaint.readStroke(sceneFile)) {
                sceneStatusFailed_ = true;
                sceneStatus_ = "Scene file has an invalid terrain road stroke.";
                return false;
            }
        } else if (key == "water_resource") {
            std::string configuredPath;
            if (!(sceneFile >> std::quoted(configuredPath))) {
                sceneStatusFailed_ = true;
                sceneStatus_ = "Scene file has an invalid water path.";
                return false;
            }
            waterPath = std::move(configuredPath);
        } else if (key == "player_model_resource") {
            std::string configuredPath;
            if (!(sceneFile >> std::quoted(configuredPath)) || configuredPath.empty()) {
                sceneStatusFailed_ = true;
                sceneStatus_ = "Scene file has an invalid player model path.";
                return false;
            }
            playerModelPath = std::move(configuredPath);
        } else if (key == "player_present") {
            int present = 1;
            if (!(sceneFile >> present) || (present != 0 && present != 1)) {
                sceneStatusFailed_ = true;
                sceneStatus_ = "Scene file has an invalid player state.";
                return false;
            }
            sceneHasPlayer = present != 0;
        } else if (key == "player_spawn_position") {
            glm::vec3 configuredPosition(0.0f);
            if (!(sceneFile >> configuredPosition.x >> configuredPosition.y >> configuredPosition.z) ||
                !std::isfinite(configuredPosition.x) ||
                !std::isfinite(configuredPosition.y) ||
                !std::isfinite(configuredPosition.z)) {
                sceneStatusFailed_ = true;
                sceneStatus_ = "Scene file has an invalid player spawn position.";
                return false;
            }
            playerSpawnPosition = configuredPosition;
        } else if (key == "skybox") {
            int enabled = 0;
            if (!(sceneFile >> enabled >> std::quoted(skyboxImage)) ||
                (enabled != 0 && enabled != 1)) {
                sceneStatusFailed_ = true;
                sceneStatus_ = "Scene file has invalid skybox settings.";
                return false;
            }
            sceneHasSkybox = enabled != 0;
        } else if (key == "grass_settings") {
            if (!(sceneFile >> grassHeightScale >> std::quoted(grassGroundImage)) ||
                !std::isfinite(grassHeightScale) || grassHeightScale < 0.25f ||
                grassHeightScale > 3.0f) {
                sceneStatusFailed_ = true;
                sceneStatus_ = "Scene file has invalid grass settings.";
                return false;
            }
        } else if (key == "grass_growth") {
            if (!(sceneFile >> grassGrowth.minimumTilt >> grassGrowth.maximumTilt >> grassGrowth.noiseSize) ||
                !std::isfinite(grassGrowth.minimumTilt) || !std::isfinite(grassGrowth.maximumTilt) ||
                !std::isfinite(grassGrowth.noiseSize) || grassGrowth.minimumTilt < 0.0f ||
                grassGrowth.maximumTilt > GrassGrowthSettings::kMaxTilt ||
                grassGrowth.minimumTilt > grassGrowth.maximumTilt ||
                grassGrowth.noiseSize < GrassGrowthSettings::kMinNoiseSize ||
                grassGrowth.noiseSize > GrassGrowthSettings::kMaxNoiseSize) {
                sceneStatusFailed_ = true;
                sceneStatus_ = "Scene file has invalid grass growth settings.";
                return false;
            }
        } else if (key == "grass_blade_width") {
            if (!(sceneFile >> grassGrowth.minimumBladeWidth >> grassGrowth.maximumBladeWidth) ||
                !std::isfinite(grassGrowth.minimumBladeWidth) || !std::isfinite(grassGrowth.maximumBladeWidth) ||
                grassGrowth.minimumBladeWidth < GrassGrowthSettings::kMinBladeWidth ||
                grassGrowth.maximumBladeWidth > GrassGrowthSettings::kMaxBladeWidth ||
                grassGrowth.minimumBladeWidth > grassGrowth.maximumBladeWidth) {
                sceneStatusFailed_ = true;
                sceneStatus_ = "Scene file has invalid grass blade widths.";
                return false;
            }
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

    // Ignore old unnumbered model references from the standalone VFX preview.
    // Saved editor instances have stable root IDs and still belong to the scene.
    if (isTornadoScene) {
        std::erase_if(modelReferences, [](const auto& model) { return !model.rootNodeId; });
        terrainPath.clear();
        waterPath.clear();
        playerModelPath.clear();
        playerSpawnPosition.reset();
        sceneHasPlayer = false;
        sceneHasSun = false;
    }

    if (!terrainPath.empty()) {
        if (terrainPath.is_relative()) {
            const std::filesystem::path sceneRelativePath =
                resolvedScenePath.parent_path() / terrainPath;
            if (std::filesystem::is_directory(sceneRelativePath, error)) {
                terrainPath = sceneRelativePath;
            } else {
                error.clear();
                terrainPath = std::filesystem::absolute(terrainPath, error);
            }
        }
        if (!std::filesystem::is_directory(terrainPath, error) ||
            !std::filesystem::is_regular_file(terrainPath / "MainField.tscb", error)) {
            sceneStatusFailed_ = true;
            sceneStatus_ = "Terrain referenced by scene was not found: " +
                terrainPath.string();
            return false;
        }
        terrainPath = normalizedPath(terrainPath);

        if (waterPath.empty()) {
            const std::filesystem::path defaultWaterPath = terrainPath / "terrain_water";
            if (std::filesystem::is_directory(defaultWaterPath, error)) {
                waterPath = defaultWaterPath;
            }
        }
        if (!waterPath.empty()) {
            if (waterPath.is_relative()) {
                const std::filesystem::path sceneRelativePath =
                    resolvedScenePath.parent_path() / waterPath;
                if (std::filesystem::is_directory(sceneRelativePath, error)) {
                    waterPath = sceneRelativePath;
                } else {
                    error.clear();
                    waterPath = std::filesystem::absolute(waterPath, error);
                }
            }
            if (!std::filesystem::is_directory(waterPath, error)) {
                sceneStatusFailed_ = true;
                sceneStatus_ = "Water referenced by scene was not found: " +
                    waterPath.string();
                return false;
            }
            waterPath = normalizedPath(waterPath);
        }
    }

    for (ModelResourceReference& modelReference : modelReferences) {
        std::filesystem::path& modelPath = modelReference.path;
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
    if (!playerModelPath.empty()) {
        if (playerModelPath.is_relative()) {
            const std::filesystem::path sceneRelativePath =
                resolvedScenePath.parent_path() / playerModelPath;
            if (std::filesystem::is_regular_file(sceneRelativePath, error)) {
                playerModelPath = sceneRelativePath;
            } else {
                error.clear();
                playerModelPath = std::filesystem::absolute(playerModelPath, error);
            }
        }
        if (!std::filesystem::is_regular_file(playerModelPath, error)) {
            sceneStatusFailed_ = true;
            sceneStatus_ = "Player model referenced by scene was not found: " +
                playerModelPath.string();
            return false;
        }
        playerModelPath = normalizedPath(playerModelPath);
    }

    std::unique_ptr<TerrainScene> terrain;
    if (!terrainPath.empty()) {
        try {
            terrain = TerrainScene::load(terrainPath / "MainField.tscb", terrainPath, waterPath);
            if (terrain->tileCount() == 0) throw std::runtime_error("terrain has no heightmap tiles");
        } catch (const std::exception& terrainError) {
            sceneStatusFailed_ = true;
            sceneStatus_ = "Failed to load scene terrain: " + std::string(terrainError.what());
            return false;
        }
    }

    clearSceneData();
    playerSpawnPosition_ = playerSpawnPosition;
    for (const ModelResourceReference& modelReference : modelReferences) {
        if (!importModelResource(modelReference.path, modelReference.rootNodeId)) {
            sceneStatusFailed_ = true;
            sceneStatus_ = "Failed to load scene model: " + resourceImportStatus_;
            return false;
        }
    }

    bool generatedGeometryChanged = false;
    if (terrain) {
        try {
            appendTerrainToScene(*terrain, terrainPath);
            appendWaterToScene(*terrain);
            terrainScene_ = std::move(terrain);
            roadPaint_ = std::move(savedRoadPaint);
            terrainResourcePath_ = terrainPath;
            waterResourcePath_ = waterPath;
            if (sceneHasPlayer) {
                playerController_.reset(
                    *terrainScene_,
                    *physicsWorld_,
                    playerSpawnPosition_
                );
                if (playerModelPath.empty()) {
                    appendPlayerToScene();
                } else if (!importModelResource(
                               playerModelPath,
                               std::nullopt,
                               true
                           )) {
                    sceneStatusFailed_ = true;
                    sceneStatus_ = "Failed to load player model: " +
                        resourceImportStatus_;
                    return false;
                }
                appendGrassToScene(*terrainScene_);
            }
            generatedGeometryChanged = true;
            std::cerr << "Loaded BOTW terrain: " << terrainScene_->tileCount()
                      << " tiles in " << terrainScene_->chunks().size()
                      << " chunks, " << terrainScene_->waterTileCount()
                      << " water tiles, " << terrainScene_->grassTileCount()
                      << " grass map tiles\n";
        } catch (const std::exception& terrainError) {
            sceneStatusFailed_ = true;
            sceneStatus_ = "Failed to load scene terrain: " +
                std::string(terrainError.what());
            return false;
        }
    }

    if (sceneHasSun) {
        createSunMesh();
        generatedGeometryChanged = true;
    }
    if (terrainScene_ && !sceneHasPlayer) {
        // Keep the authored material indices for terrain-only scenes.
        appendGrassToScene(*terrainScene_);
    }
    if (generatedGeometryChanged) {
        rebuildSceneGeometryResources();
    }

    currentScenePath_ = resolvedScenePath;
    skyboxEnabled_ = sceneHasSkybox.value_or(resolvedScenePath.filename() == "game.scene");
    grassHeightScale_ = grassHeightScale;
    grassGrowth_ = grassGrowth;
    const auto sceneImagePath = [&](const std::string& image) {
        return image.empty() ? std::filesystem::path{} :
            normalizedPath(resolvedScenePath.parent_path() / std::filesystem::path(std::u8string(image.begin(), image.end())));
    };
    skyboxTexturePath_ = sceneImagePath(skyboxImage);
    grassGroundTexturePath_ = sceneImagePath(grassGroundImage);
    createSkyboxTexture();
    if (!grassGroundTexturePath_.empty()) {
        try {
            grassGroundTexture_ = std::make_unique<Texture>(*context_, grassGroundTexturePath_,
                VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT, VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT);
        } catch (const std::exception& error) {
            std::cerr << "warning: failed to load grass ground texture: " << error.what() << '\n';
        }
    }
    loadTransformSettings();
    for (const auto& resource : sceneModelResources_) {
        if (!resource.playerModel && std::find(gravityNodeIds.begin(), gravityNodeIds.end(),
            resource.rootNodeId) != gravityNodeIds.end()) {
            modelGravity_.try_emplace(resource.rootNodeId);
        }
        if (!resource.playerModel) {
            if (auto patrol = savedPatrols.find(resource.rootNodeId); patrol != savedPatrols.end()) {
                enemyPatrols_.emplace(resource.rootNodeId, std::move(patrol->second));
            }
        }
        const auto saved = savedAnimations.find(resource.rootNodeId);
        if (saved == savedAnimations.end()) continue;
        if (auto* animation = sceneAnimation(resource.rootNodeId)) {
            const auto& settings = saved->second;
            const auto index = std::min(settings.clipIndex, animation->animationCount - 1);
            animation->activeClip = meshData_.skeletalAnimations[animation->animationStart + index].name == settings.clipName
                ? index : findAnimationClip(meshData_, *animation, settings.clipName).value_or(index);
            animation->enabled = settings.enabled != 0;
            animation->looping = settings.looping != 0;
            animation->playing = settings.playing != 0;
            animation->playbackSpeed = std::clamp(settings.speed, -4.0f, 4.0f);
            animation->settingsEdited = true;
        }
    }
    recalculateSceneBounds();
    configureHdrComposition();
    prepareTerrainAndGrassPipelines();
    lastFrameElapsedSeconds_ = std::chrono::duration<float>(
        std::chrono::steady_clock::now() - startTime_
    ).count();
    if (isTornadoScene) {
        createTornadoMeshEffect(true);
    }
    createPlayerAttackEffect();
    if (!terrainPath.empty()) {
        // A game scene starts with a single continuous camera view. The
        // editor's post-process split preview is not part of gameplay.
        postProcessSplitView_ = false;
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

    const auto resolveResourcePath = [&configPath](const std::string& value) {
        std::filesystem::path resourcePath(value);
        if (!resourcePath.empty() && resourcePath.is_relative()) {
            resourcePath = configPath.parent_path() / resourcePath;
        }
        return normalizedPath(resourcePath);
    };

    removedSceneNodeIds_.clear();

    glm::vec3 legacyTranslation(0.0f);
    glm::vec3 legacyRotationDegrees(0.0f);
    float legacyScaleFactor = 1.0f;
    bool hasLegacyTransformSettings = false;
    glm::vec3 cameraPosition = cameraPosition_;
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
        bool keepEmbeddedBaseColor = false;
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
    struct MaterialEffect {
        std::uint32_t index;
        MaterialData settings;
    };
    std::vector<MaterialEffect> materialEffects;
    HdrSettings hdr;
    DistanceFogSettings fog;
    AuthoredView authored;
    TransitionSettings transition;
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
            pending.keepEmbeddedBaseColor = texturePath == "@embedded";
            if (!pending.keepEmbeddedBaseColor) {
                pending.material.texturePath = resolveResourcePath(texturePath);
            }
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
        } else if (key == "hdr_composition") {
            int smaa = 0;
            if (!(file >> hdr.bloomLevels >> hdr.bloomThreshold >> hdr.bloomSmoothing >>
                         hdr.bloomIntensity >> hdr.bloomRadius >> hdr.exposure >> smaa >>
                         hdr.clearColor.r >> hdr.clearColor.g >> hdr.clearColor.b) ||
                hdr.bloomLevels < 0 || hdr.bloomLevels > 8 || hdr.bloomThreshold < 0 ||
                hdr.bloomSmoothing < 0 || hdr.bloomIntensity < 0 || hdr.bloomRadius < 0 ||
                hdr.bloomRadius > 1 || hdr.exposure <= 0 || (smaa != 0 && smaa != 1)) {
                valid = false;
                break;
            }
            hdr.enabled = true;
            hdr.smaa = smaa != 0;
        } else if (key == "distance_fog") {
            const auto validColor = [](const glm::vec3& color) {
                return glm::all(glm::greaterThanEqual(color, glm::vec3(0.0f))) &&
                    glm::all(glm::lessThanEqual(color, glm::vec3(1.0f)));
            };
            if (!(file >> fog.startDistance >> fog.halfDistance >> fog.maxOpacity >>
                         fog.horizonColor.r >> fog.horizonColor.g >> fog.horizonColor.b >>
                         fog.upperColor.r >> fog.upperColor.g >> fog.upperColor.b) ||
                !std::isfinite(fog.startDistance) || fog.startDistance < 0.0f ||
                !std::isfinite(fog.halfDistance) || fog.halfDistance <= 0.0f ||
                !std::isfinite(fog.maxOpacity) || fog.maxOpacity < 0.0f || fog.maxOpacity > 1.0f ||
                !validColor(fog.horizonColor) || !validColor(fog.upperColor)) {
                valid = false;
                break;
            }
            fog.enabled = true;
        } else if (key == "floating_mist") {
            if (!(file >> fog.floatingMistOpacity) || !std::isfinite(fog.floatingMistOpacity) ||
                fog.floatingMistOpacity < 0.0f || fog.floatingMistOpacity > 1.0f) {
                valid = false;
                break;
            }
        } else if (key == "authored_view") {
            if (!(file >> authored.target.x >> authored.target.y >> authored.target.z >>
                         authored.up.x >> authored.up.y >> authored.up.z >>
                         authored.fov >> authored.minimumAspect) ||
                glm::length(authored.up) < 0.001f || authored.fov <= 0 || authored.fov >= 179 ||
                authored.minimumAspect < 0) {
                valid = false;
                break;
            }
            authored.enabled = true;
            authored.up = glm::normalize(authored.up);
        } else if (key == "camera_orbit") {
            auto& limits = authored.orbitPitchLimits;
            if (!(file >> limits.x >> limits.y) || !std::isfinite(limits.x) ||
                !std::isfinite(limits.y) || limits.x <= -90.0f ||
                limits.y >= 90.0f || limits.x >= limits.y) {
                valid = false;
                break;
            }
            authored.orbitEnabled = true;
        } else if (key == "material_transition") {
            int autoplay = 0;
            if (!(file >> transition.delay >> transition.duration >> transition.dwell >>
                         autoplay >> transition.activeFov >> transition.distanceOffset) ||
                transition.delay < 0 || transition.duration <= 0 || transition.dwell < 0 ||
                (autoplay != 0 && autoplay != 1) || transition.activeFov < 0 ||
                transition.activeFov >= 179) {
                valid = false;
                break;
            }
            transition.enabled = true;
            transition.autoplay = autoplay != 0;
        } else if (key == "material_effect") {
            MaterialEffect effect{};
            int twoPass = 0, lessEqual = 0, samplesOpaque = 0, hasProgress = 0, custom = 0;
            auto& m = effect.settings;
            if (!(file >> effect.index >> twoPass >> lessEqual >> samplesOpaque >> hasProgress >>
                         m.transitionVisibleFrom >> m.transitionVisibleUntil >> custom >>
                         m.shaderParameters0.x >> m.shaderParameters0.y >> m.shaderParameters0.z >> m.shaderParameters0.w >>
                         m.shaderParameters1.x >> m.shaderParameters1.y >> m.shaderParameters1.z >> m.shaderParameters1.w) ||
                (twoPass & ~1) || (lessEqual & ~1) || (samplesOpaque & ~1) || (hasProgress & ~1) ||
                (custom & ~1) || m.transitionVisibleFrom < 0 || m.transitionVisibleUntil > 1 ||
                m.transitionVisibleFrom > m.transitionVisibleUntil) {
                valid = false;
                break;
            }
            m.drawBackFacesFirst = twoPass != 0;
            m.depthLessEqual = lessEqual != 0;
            m.samplesOpaqueScene = samplesOpaque != 0;
            m.transitionOpacity = hasProgress != 0;
            m.customParameters = custom != 0;
            materialEffects.push_back(std::move(effect));
        } else if (key == "material_textures") {
            PendingMaterialTextures pending;
            std::string normalTexturePath;
            std::string specularTexturePath;
            if (!(file >> pending.materialIndex >> std::quoted(normalTexturePath) >>
                  std::quoted(specularTexturePath))) {
                valid = false;
                break;
            }
            pending.normalTexturePath = resolveResourcePath(normalTexturePath);
            pending.specularTexturePath = resolveResourcePath(specularTexturePath);
            pendingMaterialTextures.push_back(std::move(pending));
        } else if (key == "node_material") {
            PendingNodeMaterial material;
            if (!(file >> material.nodeId >> material.materialIndex) ||
                material.materialIndex < -1) {
                valid = false;
                break;
            }
            pendingNodeMaterials.push_back(material);
        } else if (key == "camera_position") {
            if (!(file >> cameraPosition.x >> cameraPosition.y >> cameraPosition.z)) {
                valid = false;
                break;
            }
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
                shape < 0 || shape > static_cast<int>(ParticleSystemSettings::Shape::Ring) ||
                blendMode < 0 ||
                blendMode > static_cast<int>(ParticleSystemSettings::BlendMode::Additive)) {
                valid = false;
                break;
            }
            std::string texturePath;
            if (particleStream >> std::quoted(texturePath)) {
                settings.texturePath = resolveResourcePath(texturePath);
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
        std::isfinite(cameraPosition.x) && std::isfinite(cameraPosition.y) &&
        std::isfinite(cameraPosition.z) &&
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
        if (!removeSceneNodeById(removed.nodeId) && removed.nodeId >= kParticleSceneNodeBase) {
            // Generated objects can be created after this pass. Missing imported
            // instances are already absent and their IDs can be reused later.
            removedSceneNodeIds_.insert(removed.nodeId);
        }
    }

    ambientLightColor_ = glm::clamp(ambientLightColor, glm::vec3(0.0f), glm::vec3(1.0f));
    cameraPosition_ = glm::length(cameraPosition) < 0.05f
        ? glm::vec3(0.0f, 0.0f, 4.0f)
        : cameraPosition;
    hdrSettings_ = hdr;
    distanceFogSettings_ = fog;
    authoredView_ = authored;
    cameraOrbitDragging_ = false;
    if (authored.enabled) cameraTarget_ = authored.target;
    transitionSettings_ = transition;
    transitionPlayer_ = {};
    transitionPlayer_.loop = transition.autoplay;
    transitionCameraStart_ = cameraPosition_;
    transitionCameraBaseDistance_ = glm::distance(cameraTarget_, cameraPosition_);
    transitionCameraDistance_ = transitionCameraBaseDistance_;
    transitionCameraOffset_ = 0.0f;
    transitionCameraFov_ = authored.fov;
    transitionHeld_ = false;
    // A loaded scene may replace the camera while another scene was already
    // active. Reinitialize the roll once for the new saved camera position;
    // subsequent position edits remain continuous through updateCameraBasis().
    cameraBasisInitialized_ = false;
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
        const bool playerEffect = pending.nodeId == kPlayerLightningParticleNodeId ||
            pending.nodeId == kPlayerFlameParticleNodeId;
        if (playerEffect && !playerController_.active()) continue;
        ModelNode* node = findSceneNodeById(meshData_, pending.nodeId);
        if (!node && !playerEffect) {
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
        if (node && node->objectType != SceneObjectType::ParticleSystem) continue;
        if (!particleSystems_.contains(pending.nodeId) &&
            particleSystems_.size() >= kMaximumParticleSystems) {
            continue;
        }
        auto [systemIt, inserted] = particleSystems_.try_emplace(pending.nodeId);
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
            pending.material.alphaCutoff = importedMaterial.alphaCutoff;
            pending.material.embeddedAlphaMask = importedMaterial.embeddedAlphaMask;
            if (pending.keepEmbeddedBaseColor) {
                pending.material.embeddedTexture = importedMaterial.embeddedTexture;
            }
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
    for (const MaterialEffect& effect : materialEffects) {
        if (effect.index >= meshData_.materials.size()) continue;
        auto& material = meshData_.materials[effect.index];
        const auto& source = effect.settings;
        material.drawBackFacesFirst = source.drawBackFacesFirst;
        material.depthLessEqual = source.depthLessEqual;
        material.samplesOpaqueScene = source.samplesOpaqueScene;
        material.transitionOpacity = source.transitionOpacity;
        material.transitionVisibleFrom = source.transitionVisibleFrom;
        material.transitionVisibleUntil = source.transitionVisibleUntil;
        material.customParameters = source.customParameters;
        material.shaderParameters0 = source.shaderParameters0;
        material.shaderParameters1 = source.shaderParameters1;
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
    const auto sceneReference = [&](const std::filesystem::path& path) {
        if (path.empty()) return std::string{};
        std::error_code error;
        const auto relative = std::filesystem::relative(path, configPath.parent_path(), error);
        return (error || relative.empty() ? path : relative).generic_string();
    };
    file << "skybox " << skyboxEnabled_ << ' '
         << std::quoted(sceneReference(skyboxTexturePath_)) << '\n';
    if (terrainScene_) {
        file << "grass_settings " << grassHeightScale_ << ' '
             << std::quoted(sceneReference(grassGroundTexturePath_)) << '\n'
             << "grass_growth " << grassGrowth_.minimumTilt << ' '
             << grassGrowth_.maximumTilt << ' ' << grassGrowth_.noiseSize << '\n'
             << "grass_blade_width " << grassGrowth_.minimumBladeWidth << ' '
             << grassGrowth_.maximumBladeWidth << '\n';
        roadPaint_.writeStrokes(file);
    }
    if (!terrainResourcePath_.empty()) {
        std::error_code relativeError;
        std::filesystem::path terrainReference = std::filesystem::relative(
            terrainResourcePath_,
            configPath.parent_path(),
            relativeError
        );
        if (relativeError || terrainReference.empty()) {
            terrainReference = terrainResourcePath_;
        }
        file << "terrain_resource " << std::quoted(terrainReference.generic_string()) << "\n"
             << "player_present " << (playerController_.active() ? 1 : 0) << "\n";
        if (playerSpawnPosition_.has_value()) {
            file << "player_spawn_position " << playerSpawnPosition_->x << ' '
                 << playerSpawnPosition_->y << ' ' << playerSpawnPosition_->z << "\n";
        }
        if (playerController_.active() && !playerModelResourcePath_.empty()) {
            relativeError.clear();
            std::filesystem::path playerModelReference = std::filesystem::relative(
                playerModelResourcePath_,
                configPath.parent_path(),
                relativeError
            );
            if (relativeError || playerModelReference.empty()) {
                playerModelReference = playerModelResourcePath_;
            }
            file << "player_model_resource "
                 << std::quoted(playerModelReference.generic_string()) << "\n";
        }
        if (!waterResourcePath_.empty()) {
            relativeError.clear();
            std::filesystem::path waterReference = std::filesystem::relative(
                waterResourcePath_,
                configPath.parent_path(),
                relativeError
            );
            if (relativeError || waterReference.empty()) {
                waterReference = waterResourcePath_;
            }
            file << "water_resource " << std::quoted(waterReference.generic_string())
                 << "\n";
        }
    }
    for (const SceneModelResource& model : sceneModelResources_) {
        if (model.playerModel) {
            continue;
        }
        std::error_code relativeError;
        const auto relativePath = std::filesystem::relative(model.path, configPath.parent_path(), relativeError);
        file << "model_resource_path "
             << std::quoted((relativeError || relativePath.empty() ? model.path : relativePath).generic_string())
             << ' ' << model.rootNodeId << "\n";
        if (modelGravity_.contains(model.rootNodeId)) file << "node_gravity " << model.rootNodeId << '\n';
        if (const auto entry = enemyPatrols_.find(model.rootNodeId); entry != enemyPatrols_.end()) {
            const auto& patrol = entry->second;
            file << "node_patrol " << model.rootNodeId << ' ' << patrol.enabled << ' '
                 << patrol.offset.x << ' ' << patrol.offset.y << ' ' << patrol.speed << ' '
                 << patrol.wait << ' ' << patrol.facingOffsetDegrees << ' '
                 << std::quoted(patrol.walkClip) << ' ' << std::quoted(patrol.idleClip) << '\n';
        }
    }
    for (const auto& animation : meshData_.skeletalAnimationInstances) {
        if (!animation.settingsEdited || animation.activeClip >= animation.animationCount) continue;
        const auto& clip = meshData_.skeletalAnimations[animation.animationStart + animation.activeClip];
        file << "node_skeletal " << animation.ownerNodeId << ' ' << animation.activeClip << ' '
             << std::quoted(clip.name) << ' ' << animation.enabled << ' ' << animation.looping << ' '
             << animation.playing << ' ' << animation.playbackSpeed << '\n';
    }
    if (hdrSettings_.enabled) {
        const auto& h = hdrSettings_;
        file << "hdr_composition " << h.bloomLevels << ' ' << h.bloomThreshold << ' '
             << h.bloomSmoothing << ' ' << h.bloomIntensity << ' ' << h.bloomRadius << ' '
             << h.exposure << ' ' << h.smaa << ' ' << h.clearColor.r << ' '
             << h.clearColor.g << ' ' << h.clearColor.b << "\n";
    }
    if (distanceFogSettings_.enabled) {
        const auto& fog = distanceFogSettings_;
        file << "distance_fog " << fog.startDistance << ' ' << fog.halfDistance << ' '
             << fog.maxOpacity << ' ' << fog.horizonColor.r << ' ' << fog.horizonColor.g << ' '
             << fog.horizonColor.b << ' ' << fog.upperColor.r << ' ' << fog.upperColor.g << ' '
             << fog.upperColor.b << "\n";
    }
    if (distanceFogSettings_.floatingMistOpacity > 0.0f) {
        file << "floating_mist " << distanceFogSettings_.floatingMistOpacity << '\n';
    }
    const auto& view = authoredView_;
    if (view.enabled) {
        file << "authored_view " << view.target.x << ' ' << view.target.y << ' ' << view.target.z << ' '
             << view.up.x << ' ' << view.up.y << ' ' << view.up.z << ' ' << view.fov << ' ' << view.minimumAspect << "\n";
    }
    if (view.orbitEnabled) {
        file << "camera_orbit " << view.orbitPitchLimits.x << ' ' << view.orbitPitchLimits.y << "\n";
    }
    if (transitionSettings_.enabled) {
        const auto& t = transitionSettings_;
        file << "material_transition " << t.delay << ' ' << t.duration << ' ' << t.dwell << ' '
             << t.autoplay << ' ' << t.activeFov << ' ' << t.distanceOffset << "\n";
    }
    const bool saveInitialCamera = transitionSettings_.enabled || view.orbitEnabled;
    glm::vec3 savedCamera = saveInitialCamera ? transitionCameraStart_ : cameraPosition_;
#ifndef VULKCORE_OHOS
    if (!saveInitialCamera && roadEditorCamera_) savedCamera = roadEditorCamera_->originalPosition;
#endif
    file << "sun_present " << (sunPresent_ ? 1 : 0) << "\n"
         << "camera_position " << savedCamera.x << ' '
         << savedCamera.y << ' ' << savedCamera.z << "\n"
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

    for (const auto effectId : {kPlayerLightningParticleNodeId, kPlayerFlameParticleNodeId}) {
        if (const auto it = particleSystems_.find(effectId); it != particleSystems_.end()) {
            saveParticleSystemSettings(file, effectId, it->second.settings());
        }
    }

    std::unordered_set<std::size_t> activeMaterialIndices;
    if (!meshData_.materials.empty()) {
        activeMaterialIndices.insert(0);
    }
    for (const SceneModelResource& model : sceneModelResources_) {
        for (std::size_t materialIndex = 0;
             materialIndex < model.materialCount;
             ++materialIndex) {
            const std::size_t sceneMaterialIndex =
                static_cast<std::size_t>(model.materialStart) + materialIndex;
            if (sceneMaterialIndex < meshData_.materials.size()) {
                activeMaterialIndices.insert(sceneMaterialIndex);
            }
        }
    }
    for (const MeshPart& part : meshData_.parts) {
        // Water and grass are regenerated before node removals are loaded.
        // Keep their material slots so saved overrides still use the right indices.
        const bool regenerated = (part.nodeId >= kWaterSceneNodeId && part.nodeId < kGrassSceneNodeId) ||
            grassLodForLayer(part.nodeId - kGrassLodNodeBase).has_value();
        if (part.materialIndex < meshData_.materials.size() &&
            (regenerated || !removedSceneNodeIds_.contains(part.nodeId))) {
            activeMaterialIndices.insert(part.materialIndex);
        }
    }
    if (sunPresent_ && sunMaterialIndex_ < meshData_.materials.size()) {
        activeMaterialIndices.insert(sunMaterialIndex_);
    }
    const auto collectActiveNodeMaterials =
        [this, &activeMaterialIndices](const auto& self, const ModelNode& node) -> void {
            if (removedSceneNodeIds_.contains(node.nodeId)) {
                return;
            }
            if (node.materialOverride >= 0 &&
                static_cast<std::size_t>(node.materialOverride) < meshData_.materials.size()) {
                activeMaterialIndices.insert(
                    static_cast<std::size_t>(node.materialOverride)
                );
            }
            for (const ModelNode& child : node.children) {
                self(self, child);
            }
        };
    collectActiveNodeMaterials(collectActiveNodeMaterials, meshData_.hierarchy);
    for (const ModelNode& node : meshData_.additionalNodes) {
        collectActiveNodeMaterials(collectActiveNodeMaterials, node);
    }
    MaterialIndexRemap materialIndexRemap;
    const auto addMaterial = [&](std::size_t index) {
        if (activeMaterialIndices.contains(index)) materialIndexRemap.try_emplace(index, materialIndexRemap.size());
    };
    addMaterial(0);
    // The loader creates regular model materials before terrain and player materials,
    // including models added after those objects were created in the editor.
    for (const auto& model : sceneModelResources_) {
        if (model.playerModel) continue;
        for (std::size_t index = 0; index < model.materialCount; ++index) {
            addMaterial(model.materialStart + index);
        }
    }
    for (std::size_t index = 1; index < meshData_.materials.size(); ++index) {
        addMaterial(index);
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
    saveUserMaterials(file, meshData_.materials, materialIndexRemap);
    for (std::size_t index = 0; index < meshData_.materials.size(); ++index) {
        const auto remap = materialIndexRemap.find(index);
        if (remap == materialIndexRemap.end()) continue;
        const auto& m = meshData_.materials[index];
        if (!m.drawBackFacesFirst && !m.depthLessEqual && !m.samplesOpaqueScene &&
            !m.transitionOpacity && !m.customParameters) continue;
        const auto& a = m.shaderParameters0;
        const auto& b = m.shaderParameters1;
        file << "material_effect " << remap->second << ' ' << m.drawBackFacesFirst << ' '
             << m.depthLessEqual << ' ' << m.samplesOpaqueScene << ' ' << m.transitionOpacity << ' '
             << m.transitionVisibleFrom << ' ' << m.transitionVisibleUntil << ' ' << m.customParameters << ' '
             << a.x << ' ' << a.y << ' ' << a.z << ' ' << a.w << ' '
             << b.x << ' ' << b.y << ' ' << b.z << ' ' << b.w << "\n";
    }
    saveNodeMaterialSettings(file, meshData_.hierarchy, materialIndexRemap);
    for (const ModelNode& node : meshData_.additionalNodes) {
        saveNodeMaterialSettings(file, node, materialIndexRemap);
    }
    file.close();
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
        } else {
            for (auto& object : meshData_.additionalNodes) {
                if (removeModelNodeFromChildren(object, nodeId, removedNodeIds)) {
                    removed = true;
                    break;
                }
            }
        }
    }
    if (!removed) {
        return false;
    }
    selectedModelNode_ = nullptr;

    const bool removingPlayer = std::find(removedNodeIds.begin(), removedNodeIds.end(),
        kPlayerSceneNodeId) != removedNodeIds.end();
    if (removingPlayer) {
        if (physicsWorld_) physicsWorld_->removePlayer();
        playerController_.clear();
        removedNodeIds.insert(removedNodeIds.end(),
            {kPlayerAttackParticleNodeId, kPlayerLightningParticleNodeId, kPlayerFlameParticleNodeId});
        removeSceneNodeById(kPlayerTornadoMeshNodeId);
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
        if (modelGravity_.erase(removedNodeId)) physicsWorld_->removeGravityBox(removedNodeId);
        enemyPatrols_.erase(removedNodeId);
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

    std::erase_if(meshData_.skeletalAnimationInstances, [&removedNodeIds](const auto& instance) {
        return std::find(removedNodeIds.begin(), removedNodeIds.end(), instance.rootNodeId) != removedNodeIds.end();
    });
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
    if (removingSun) {
        removeSunMesh();
        rebuildSceneGeometryResources();
    }
    updateBoundingBoxVertices();
    return true;
}



} // namespace VulkCore
