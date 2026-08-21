#include "RendererImplementation.hpp"

namespace VulkCore {

bool Renderer::importModelResource(
    const std::filesystem::path& path,
    std::optional<std::uint32_t> preferredRootNodeId
) {
    resourceImportFailed_ = false;
    resourceImportStatus_.clear();

    if (path.empty()) {
        resourceImportFailed_ = true;
        resourceImportStatus_ = "Enter a model file path before importing.";
        return false;
    }

    std::error_code error;
    std::filesystem::path resolvedPath = path;
    if (resolvedPath.is_relative()) {
        const std::filesystem::path absolutePath = std::filesystem::absolute(
            resolvedPath,
            error
        );
        if (!error) {
            resolvedPath = absolutePath;
        }
        error.clear();
    }
    if (!std::filesystem::is_regular_file(resolvedPath, error)) {
        resourceImportFailed_ = true;
        resourceImportStatus_ = "Model file was not found: " + resolvedPath.string();
        return false;
    }
    resolvedPath = normalizedPath(resolvedPath);

    MeshData importedMesh;
    try {
        ModelResourceCacheEntry& cache = modelResourceCache_[resolvedPath.string()];
        if (cache.mesh != nullptr) {
            importedMesh = *cache.mesh;
        } else {
            if (!cache.error.empty()) {
                throw std::runtime_error(cache.error);
            }
            auto loadedMesh = std::make_shared<MeshData>(loadModelMesh(resolvedPath));
            if (loadedMesh->vertices.empty() || loadedMesh->indices.empty()) {
                throw std::runtime_error("the imported model contains no renderable triangles");
            }
            cache.mesh = std::move(loadedMesh);
            cache.error.clear();
            discoverModelTextureResources(resolvedPath, cache);
            importedMesh = *cache.mesh;
        }
        if (importedMesh.vertices.empty() || importedMesh.indices.empty()) {
            throw std::runtime_error("the imported model contains no renderable triangles");
        }
    } catch (const std::exception& importError) {
        resourceImportFailed_ = true;
        resourceImportStatus_ = "Import failed: " + std::string(importError.what());
        return false;
    }

    ModelNode preservedSunNode;
    MaterialData preservedSunMaterial;
    const bool preserveSun = sunPresent_;
    if (preserveSun) {
        const auto sunNodeIt = std::find_if(
            meshData_.additionalNodes.begin(),
            meshData_.additionalNodes.end(),
            [this](const ModelNode& node) {
                return node.nodeId == sunNodeId_;
            }
        );
        if (sunNodeIt != meshData_.additionalNodes.end()) {
            preservedSunNode = *sunNodeIt;
            if (sunMaterialIndex_ < meshData_.materials.size()) {
                preservedSunMaterial = meshData_.materials[sunMaterialIndex_];
            }
        }
    }

    // The CPU import is complete before changing scene data. Existing models,
    // particles, and the optional Sun remain in the scene.
    context_->waitIdle();
    selectedModelNode_ = nullptr;
    sceneNodeTransforms_.clear();
    sceneNodeRenderEnabled_.clear();
    particleDrawRanges_.clear();
    particleVertices_.clear();
    const std::size_t importedVertexCount = importedMesh.vertices.size();
    const std::size_t importedTriangleCount = importedMesh.indices.size() / 3;
    if (preserveSun) {
        removeSunMesh();
    }
    appendImportedModel(
        std::move(importedMesh),
        resolvedPath,
        preferredRootNodeId
    );
    discoverModelResources();
    if (preserveSun) {
        createSunMesh(&preservedSunNode, &preservedSunMaterial);
    }
    rebuildSceneGeometryResources();

    boundingBoxVertices_.fill(Vertex{});
    materialNameMaterialIndex_ = -1;
    materialTexturePathMaterialIndex_ = -1;
    materialNormalTexturePathMaterialIndex_ = -1;
    materialSpecularTexturePathMaterialIndex_ = -1;
    selectedResourceType_ = ResourceSelectionType::Model;
    selectedResourceMaterialIndex_ = -1;
    resourceImportStatus_ = "Imported " + resolvedPath.filename().string() +
        " into the scene (" + std::to_string(importedVertexCount) + " vertices, " +
        std::to_string(importedTriangleCount) + " triangles).";
    selectModelResource(resolvedPath);
    return true;
}

void Renderer::createSunMesh(
    const ModelNode* preservedNode,
    const MaterialData* preservedMaterial
) {
    if (sunPresent_) {
        return;
    }

    constexpr std::uint32_t kLatitudeSegments = 24;
    constexpr std::uint32_t kLongitudeSegments = 36;
    constexpr float kSunRadiusInViewport = 0.15f;
    const glm::vec3 sunPositionInViewport(0.82f, 0.78f, 1.08f);
    const float safeModelScale = std::max(modelScale_, std::numeric_limits<float>::epsilon());
    const glm::vec3 center = modelCenter_ + sunPositionInViewport / safeModelScale;
    const float radius = kSunRadiusInViewport / safeModelScale;

    MaterialData material = preservedMaterial != nullptr
        ? *preservedMaterial
        : MaterialData{};
    material.name = "Sun";
    material.baseColor = preservedMaterial != nullptr
        ? material.baseColor
        : glm::vec4(1.0f, 0.55f, 0.06f, 1.0f);
    material.roughness = preservedMaterial != nullptr ? material.roughness : 0.24f;
    material.doubleSided = true;
    material.userCreated = true;
    if (material.shaderConfiguration.shader(ShaderStage::Fragment).empty()) {
        material.shaderConfiguration.shader(ShaderStage::Fragment) = "shader_sun.frag";
    }
    if (preservedMaterial == nullptr) {
        material.emissionColor = glm::vec3(1.0f, 0.22f, 0.015f);
        material.emissionStrength = 3.5f;
    }
    meshData_.materials.push_back(std::move(material));
    sunMaterialIndex_ = static_cast<std::uint32_t>(meshData_.materials.size() - 1);

    const std::uint32_t sunNodeId = kSunSceneNodeId;
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
            const Vertex sunVertex{
                center + normal * radius,
                normal,
                glm::vec2(
                    static_cast<float>(longitude) / static_cast<float>(kLongitudeSegments),
                    static_cast<float>(latitude) / static_cast<float>(kLatitudeSegments)
                ),
                glm::vec4(1.0f)
            };
            meshData_.vertices.push_back(sunVertex);
            meshData_.bindPoseVertices.push_back(sunVertex);
            meshData_.skinSourceVertices.push_back(sunVertex);
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

    ModelNode sunNode = preservedNode != nullptr ? *preservedNode : ModelNode{};
    sunNode.name = "Sun";
    sunNode.nodeId = sunNodeId;
    sunNode.objectType = SceneObjectType::ImportedMesh;
    sunNode.meshCount = 1;
    sunNode.globalTransform = glm::translate(glm::mat4(1.0f), center);
    sunNode.meshPartIndices.clear();
    sunNode.meshPartIndices.push_back(partIndex);
    sunNode.hasBounds = true;
    sunNode.boundsMinimum = center - glm::vec3(radius);
    sunNode.boundsMaximum = center + glm::vec3(radius);
    sunNode.materialOverride = static_cast<std::int32_t>(sunMaterialIndex_);
    meshData_.additionalNodes.push_back(std::move(sunNode));
    sunNodeId_ = sunNodeId;
    sunPartIndex_ = partIndex;
    sunVertexStart_ = vertexStart;
    sunIndexStart_ = firstIndex;
    sunPresent_ = true;
    removedSceneNodeIds_.erase(sunNodeId_);
}

void Renderer::removeSunMesh() {
    if (!sunPresent_) {
        return;
    }

    const std::uint32_t removedNodeId = sunNodeId_;
    const std::uint32_t removedMaterialIndex = sunMaterialIndex_;
    if (sunPartIndex_ < meshData_.parts.size() &&
        meshData_.parts[sunPartIndex_].nodeId == removedNodeId) {
        const MeshPart& sunPart = meshData_.parts[sunPartIndex_];
        if (sunVertexStart_ <= meshData_.vertices.size() &&
            sunIndexStart_ <= meshData_.indices.size() &&
            sunPart.firstIndex + sunPart.indexCount == meshData_.indices.size()) {
            meshData_.vertices.resize(sunVertexStart_);
            meshData_.bindPoseVertices.resize(sunVertexStart_);
            meshData_.skinSourceVertices.resize(sunVertexStart_);
            meshData_.indices.resize(sunIndexStart_);
        }
        meshData_.parts.erase(meshData_.parts.begin() + sunPartIndex_);
        for (ModelNode& node : meshData_.additionalNodes) {
            std::vector<std::uint32_t> adjustedParts;
            adjustedParts.reserve(node.meshPartIndices.size());
            for (const std::uint32_t partIndex : node.meshPartIndices) {
                if (partIndex == sunPartIndex_) {
                    continue;
                }
                adjustedParts.push_back(
                    partIndex > sunPartIndex_ ? partIndex - 1 : partIndex
                );
            }
            node.meshPartIndices = std::move(adjustedParts);
        }
    }

    const auto materialIsUsed = [this, removedMaterialIndex]() {
        if (std::any_of(
                meshData_.parts.begin(),
                meshData_.parts.end(),
                [removedMaterialIndex](const MeshPart& part) {
                    return part.materialIndex == removedMaterialIndex;
                })) {
            return true;
        }
        const auto nodeUsesMaterial = [removedMaterialIndex](const auto& self, const ModelNode& node) {
            if (node.materialOverride == static_cast<std::int32_t>(removedMaterialIndex)) {
                return true;
            }
            return std::any_of(
                node.children.begin(),
                node.children.end(),
                [&self](const ModelNode& child) {
                    return self(self, child);
                }
            );
        };
        if (nodeUsesMaterial(nodeUsesMaterial, meshData_.hierarchy)) {
            return true;
        }
        return std::any_of(
            meshData_.additionalNodes.begin(),
            meshData_.additionalNodes.end(),
            [&nodeUsesMaterial](const ModelNode& node) {
                return nodeUsesMaterial(nodeUsesMaterial, node);
            }
        );
    };
    if (removedMaterialIndex < meshData_.materials.size() && !materialIsUsed()) {
        meshData_.materials.erase(meshData_.materials.begin() + removedMaterialIndex);
        const auto adjustMaterialOverrides = [removedMaterialIndex](
            const auto& self,
            ModelNode& node
        ) -> void {
            if (node.materialOverride > static_cast<std::int32_t>(removedMaterialIndex)) {
                --node.materialOverride;
            }
            for (ModelNode& child : node.children) {
                self(self, child);
            }
        };
        for (MeshPart& part : meshData_.parts) {
            if (part.materialIndex > removedMaterialIndex) {
                --part.materialIndex;
            }
        }
        adjustMaterialOverrides(adjustMaterialOverrides, meshData_.hierarchy);
        for (ModelNode& node : meshData_.additionalNodes) {
            adjustMaterialOverrides(adjustMaterialOverrides, node);
        }
    }

    sunNodeId_ = 0;
    sunMaterialIndex_ = 0;
    sunPartIndex_ = 0;
    sunVertexStart_ = 0;
    sunIndexStart_ = 0;
    sunPresent_ = false;
    selectedResourceMaterialIndex_ = -1;
}

void Renderer::rebuildSceneGeometryResources() {
    context_->waitIdle();
    vertexBuffers_.clear();
    indexBuffer_.reset();
    createVertexBuffer();
    createIndexBuffer();
    rebuildMaterialResources();
}

void Renderer::createVertexBuffer() {
    const Vertex emptyVertex{};
    const Vertex* vertexData = meshData_.vertices.empty()
        ? &emptyVertex
        : meshData_.vertices.data();
    const std::size_t vertexCount = std::max<std::size_t>(1, meshData_.vertices.size());
    const VkDeviceSize bufferSize = sizeof(Vertex) * vertexCount;

    vertexBuffers_.resize(kMaxFramesInFlight);
    for (auto& vertexBuffer : vertexBuffers_) {
        vertexBuffer = std::make_unique<Buffer>(
            *context_,
            bufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        vertexBuffer->upload(vertexData, bufferSize);
    }
}

void Renderer::createIndexBuffer() {
    const std::uint32_t emptyIndex = 0;
    const std::uint32_t* indexData = meshData_.indices.empty()
        ? &emptyIndex
        : meshData_.indices.data();
    const std::size_t indexCount = std::max<std::size_t>(1, meshData_.indices.size());
    const VkDeviceSize bufferSize = sizeof(std::uint32_t) * indexCount;
    Buffer indexStagingBuffer(
        *context_,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    indexStagingBuffer.upload(indexData, bufferSize);

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
                if (copiedVertexCount > 0 && isSceneNodeRenderEnabled(node.nodeId)) {
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
    terrainDetailTextures_ = {};
    terrainDetailTexturesReady_ = false;
    bool needsWhiteFallbackTexture = false;
    bool needsNormalFallbackTexture = false;
    std::filesystem::path terrainAtlasDirectory;

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
        if (material.texturePath.filename() == "terrain_atlas.png" &&
            materialTextures_[materialIndex] != nullptr) {
            terrainAtlasDirectory = material.texturePath.parent_path();
        }

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

    if (!terrainAtlasDirectory.empty()) {
        static constexpr std::array<const char*, kTerrainDetailTextureCount> kTerrainDetailNames = {
            "terrain_grass.png",
            "terrain_dirt.png",
            "terrain_rock.png",
            "terrain_sand.png",
            "terrain_snow.png",
            "terrain_mud.png",
            "terrain_gravel.png",
            "terrain_ash.png"
        };
        terrainDetailTexturesReady_ = true;
        for (std::size_t index = 0; index < kTerrainDetailTextureCount; ++index) {
            try {
                terrainDetailTextures_[index] = std::make_unique<Texture>(
                    *context_,
                    terrainAtlasDirectory / kTerrainDetailNames[index]
                );
            } catch (const std::exception& error) {
                terrainDetailTexturesReady_ = false;
                std::cerr << "warning: failed to load terrain detail texture '"
                          << kTerrainDetailNames[index] << "': " << error.what() << '\n';
                break;
            }
        }
        if (!terrainDetailTexturesReady_) {
            terrainDetailTextures_ = {};
        }
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
    terrainDetailTextures_ = {};
    terrainDetailTexturesReady_ = false;
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
    poolSizes[1].descriptorCount = (materialDescriptorCount + particleDescriptorCapacity) *
        (5 + static_cast<uint32_t>(kTerrainDetailTextureCount));

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
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

            std::array<VkDescriptorImageInfo, kTerrainDetailTextureCount>
                terrainDetailImageInfos{};
            for (std::size_t detailIndex = 0;
                 detailIndex < kTerrainDetailTextureCount;
                 ++detailIndex) {
                const Texture* detailTexture = terrainDetailTextures_[detailIndex].get();
                if (detailTexture == nullptr) {
                    detailTexture = fallbackTexture_.get();
                }
                terrainDetailImageInfos[detailIndex].imageLayout =
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                terrainDetailImageInfos[detailIndex].imageView = detailTexture->imageView();
                terrainDetailImageInfos[detailIndex].sampler = detailTexture->sampler();
            }

            VkDescriptorImageInfo depthImageInfo{};
            depthImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            depthImageInfo.imageView = sceneRenderTargets_[i].depthImageView;
            depthImageInfo.sampler = sceneRenderTargets_[i].depthSampler;

            std::array<VkWriteDescriptorSet, 6> descriptorWrites{};
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

            descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[4].dstSet = descriptorSets_[descriptorSetIndex];
            descriptorWrites[4].dstBinding = 5;
            descriptorWrites[4].dstArrayElement = 0;
            descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[4].descriptorCount = 1;
            descriptorWrites[4].pImageInfo = &depthImageInfo;

            descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[5].dstSet = descriptorSets_[descriptorSetIndex];
            descriptorWrites[5].dstBinding = 6;
            descriptorWrites[5].dstArrayElement = 0;
            descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[5].descriptorCount = static_cast<uint32_t>(
                kTerrainDetailTextureCount
            );
            descriptorWrites[5].pImageInfo = terrainDetailImageInfos.data();

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

            VkDescriptorImageInfo depthImageInfo{};
            depthImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            depthImageInfo.imageView = target.depthImageView;
            depthImageInfo.sampler = target.depthSampler;
            VkWriteDescriptorSet depthWrite{};
            depthWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            depthWrite.dstSet = descriptorSets_[descriptorSetIndex];
            depthWrite.dstBinding = 5;
            depthWrite.descriptorCount = 1;
            depthWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            depthWrite.pImageInfo = &depthImageInfo;
            vkUpdateDescriptorSets(context_->device(), 1, &depthWrite, 0, nullptr);
        }
    }
}

void Renderer::updateCameraBasis() {
    constexpr float kMinimumBasisLength = 0.0001f;
    const float kMinimumCameraDistance = playerController_.active()
        ? std::max(0.00001f, modelScale_ * 0.02f)
        : 0.05f;
    glm::vec3 targetDelta = cameraTarget_ - cameraPosition_;
    if (glm::length(targetDelta) < kMinimumCameraDistance) {
        cameraPosition_ = cameraTarget_ + glm::vec3(3.5f, 2.8f, 3.5f);
        targetDelta = cameraTarget_ - cameraPosition_;
        cameraBasisInitialized_ = false;
    }

    cameraForward_ = glm::normalize(targetDelta);

    if (!cameraBasisInitialized_) {
        const glm::vec3 worldUp = playerController_.active()
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::vec3(0.0f, 0.0f, 1.0f);
        // At the exact pole the position alone does not define camera roll,
        // so retain the historical front-view orientation as the initial one.
        glm::vec3 initialUp = worldUp -
            cameraForward_ * glm::dot(worldUp, cameraForward_);
        if (glm::length(initialUp) < kMinimumBasisLength) {
            initialUp = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        cameraUp_ = glm::normalize(initialUp);
        cameraBasisInitialized_ = true;
    } else {
        // Keep the previous roll and project the previous up direction onto
        // the new view plane. This makes camera position edits continuous,
        // including when the view crosses the old 0.99 threshold.
        glm::vec3 continuousUp = cameraUp_ -
            cameraForward_ * glm::dot(cameraUp_, cameraForward_);
        if (glm::length(continuousUp) < kMinimumBasisLength) {
            // If the previous up direction is parallel to the new forward
            // direction, the previous right direction is still a valid
            // continuous reference for the new camera roll.
            continuousUp = cameraRight_ -
                cameraForward_ * glm::dot(cameraRight_, cameraForward_);
        }
        if (glm::length(continuousUp) < kMinimumBasisLength) {
            // Defensive fallback for a discontinuous external camera jump.
            const glm::vec3 worldUp = playerController_.active()
                ? glm::vec3(0.0f, 1.0f, 0.0f)
                : glm::vec3(0.0f, 0.0f, 1.0f);
            continuousUp = worldUp -
                cameraForward_ * glm::dot(worldUp, cameraForward_);
        }
        cameraUp_ = glm::normalize(continuousUp);
    }

    cameraRight_ = glm::normalize(glm::cross(cameraForward_, cameraUp_));
    cameraUp_ = glm::normalize(glm::cross(cameraRight_, cameraForward_));
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
    ubo.view = glm::lookAt(cameraPosition_, cameraTarget_, cameraUp_);

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
    const float nearPlane = playerController_.active()
        ? std::max(0.00001f, modelScale_ * 0.02f)
        : 0.01f;
    ubo.projection = glm::perspective(
        glm::radians(45.0f),
        static_cast<float>(extent.width) / static_cast<float>(extent.height),
        nearPlane,
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
    ubo.cameraPosition = glm::vec4(cameraPosition_, modelScale_);

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



} // namespace VulkCore
