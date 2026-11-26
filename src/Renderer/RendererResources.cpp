#include "RendererImplementation.hpp"
#include "SceneLoadTask.hpp"
#include "vulkcore/Renderer/Frustum.hpp"
#include "vulkcore/Renderer/SceneProjection.hpp"
#include "vulkcore/Terrain/Terrain.hpp"
#include "vulkcore/Utils/SbfresLoader.hpp"

#include <charconv>
#include <future>
#include <map>

namespace VulkCore {
namespace {

struct MistPuff {
    glm::vec4 centerAndDensity;
    glm::vec4 inverseRadiiAndSeed;
    float distance;
};

auto mistCellRandom(int x, int z) {
    std::uint32_t seed = static_cast<std::uint32_t>(x) * 747796405u +
        static_cast<std::uint32_t>(z) * 2891336453u + 277803737u;
    return [seed]() mutable {
        seed = seed * 747796405u + 2891336453u;
        const std::uint32_t word = ((seed >> ((seed >> 28u) + 4u)) ^ seed) * 277803737u;
        return static_cast<float>((word ^ (word >> 22u)) >> 8u) * (1.0f / 16777216.0f);
    };
}

void sortNearestPuffs(std::vector<MistPuff>& puffs, std::size_t count) {
    std::partial_sort(puffs.begin(), puffs.begin() + count, puffs.end(),
        [](const MistPuff& a, const MistPuff& b) { return a.distance < b.distance; });
}

void fillFloatingMist(UniformBufferObject& ubo, const TerrainScene& terrain,
                      float opacity, float time, const glm::mat4& clipFromWorld) {
    constexpr float cellSize = 185.0f;
    constexpr float maximumDistance = 1150.0f;
    constexpr float driftAmplitude = 17.0f;
    const glm::vec3 camera = (glm::vec3(ubo.cameraPosition) - glm::vec3(ubo.model[3])) / ubo.cameraPosition.w;
    const glm::vec2 cameraXZ(camera.x, camera.z);
    const float searchRadius = maximumDistance + driftAmplitude;
    const glm::ivec2 first(glm::floor((cameraXZ - searchRadius) / cellSize));
    const glm::ivec2 last(glm::floor((cameraXZ + searchRadius) / cellSize));

    std::vector<MistPuff> visible;
    for (int z = first.y; z <= last.y; ++z) {
        for (int x = first.x; x <= last.x; ++x) {
            // Seed each world cell so translating or rotating the camera keeps the same wisps.
            auto random = mistCellRandom(x, z);
            if (random() < 0.37f) continue;
            const float jitterX = random();
            const float jitterZ = random();
            const glm::vec2 anchor = (glm::vec2(x, z) + 0.15f + glm::vec2(jitterX, jitterZ) * 0.70f) * cellSize;
            const float phase = random() * 6.2831853f;
            const glm::vec2 drift = glm::vec2(1.0f, 0.44f) * (driftAmplitude * std::sin(time * 0.075f + phase));
            const glm::vec2 position = anchor + drift;
            const float distance = glm::distance(cameraXZ, position);
            const float fade = glm::smoothstep(110.0f, 210.0f, distance) *
                (1.0f - glm::smoothstep(850.0f, maximumDistance, distance));
            if (fade <= 0.0f) continue;

            const auto height = terrain.renderedHeightAtWorld(anchor);
            if (!height) continue;
            const float radiusX = std::lerp(48.0f, 98.0f, random());
            const float radiusY = std::lerp(10.0f, 25.0f, random());
            const float radiusZ = std::lerp(24.0f, 58.0f, random());
            const glm::vec3 radii(radiusX, radiusY, radiusZ);
            const float clearance = std::lerp(9.0f, 32.0f, random());
            const glm::vec3 center(position.x,
                *height + radiusY * 0.63f + clearance + 2.0f * std::sin(time * 0.11f + phase), position.y);
            if (isOutsideVulkanFrustum(clipFromWorld, center - radii, center + radii)) continue;
            visible.push_back({glm::vec4(center, fade * std::lerp(0.42f, 1.0f, random())),
                               glm::vec4(1.0f / radii, random() * 51.0f), distance});
        }
    }
    const std::size_t count = std::min(visible.size(), ubo.mistCenters.size());
    sortNearestPuffs(visible, count);
    ubo.floatingMist = glm::vec4(static_cast<float>(count), opacity, time, 0.0f);
    for (std::size_t i = 0; i < count; ++i) {
        ubo.mistCenters[i] = visible[i].centerAndDensity;
        ubo.mistInverseRadii[i] = visible[i].inverseRadiiAndSeed;
    }
}

void fillCloudMist(UniformBufferObject& ubo, float time, glm::mat4 cloudClip) {
    struct Layer {
        float height;
        float cellSize;
        float windSpeed;
        float opacity;
    };
    constexpr std::array layers = {Layer{940.0f, 2200.0f, 13.0f, 0.95f},
                                   Layer{1530.0f, 2800.0f, 19.0f, 0.58f}};
    constexpr float maximumDistance = 15000.0f;
    const glm::vec3 camera = (glm::vec3(ubo.cameraPosition) - glm::vec3(ubo.model[3])) / ubo.cameraPosition.w;
    const glm::vec2 cameraXZ(camera.x, camera.z);

    // Sky rays extend beyond the geometry clip planes. Keep the side planes
    // and reject puffs entirely behind the camera.
    for (int i = 0; i < 4; ++i) cloudClip[i][2] = 0.5f * cloudClip[i][3];

    std::size_t count = 0;
    float lowest = std::numeric_limits<float>::max();
    float highest = std::numeric_limits<float>::lowest();
    for (std::size_t layerIndex = 0; layerIndex < layers.size(); ++layerIndex) {
        const Layer& layer = layers[layerIndex];
        const glm::vec2 drift = glm::vec2(0.821f, 0.571f) * (time * layer.windSpeed);
        const glm::vec2 localCamera = cameraXZ - drift;
        const glm::ivec2 first(glm::floor((localCamera - maximumDistance) / layer.cellSize));
        const glm::ivec2 last(glm::floor((localCamera + maximumDistance) / layer.cellSize));
        std::vector<MistPuff> visible;
        for (int z = first.y; z <= last.y; ++z) {
            for (int x = first.x; x <= last.x; ++x) {
                auto random = mistCellRandom(x + static_cast<int>(layerIndex) * 41, z);
                if (random() < 0.28f) continue;
                const float jitterX = random();
                const float jitterZ = random();
                const glm::vec2 position = (glm::vec2(x, z) + 0.18f +
                    glm::vec2(jitterX, jitterZ) * 0.64f) * layer.cellSize + drift;
                const glm::vec3 center(position.x, layer.height, position.y);
                const float distance = glm::distance(camera, center);
                const float fade = 1.0f - glm::smoothstep(8500.0f, maximumDistance, distance);
                if (fade <= 0.0f) continue;

                const float radiusX = layer.cellSize * std::lerp(0.36f, 0.57f, random());
                const float radiusY = std::lerp(105.0f, 205.0f, random());
                const float radiusZ = layer.cellSize * std::lerp(0.24f, 0.44f, random());
                const glm::vec3 radii(radiusX, radiusY, radiusZ);
                if (isOutsideVulkanFrustum(cloudClip, center - radii, center + radii)) continue;
                visible.push_back({glm::vec4(center, fade * layer.opacity * std::lerp(0.62f, 1.0f, random())),
                                   glm::vec4(1.0f / radii, random() * 51.0f), distance});
            }
        }
        const std::size_t layerCount = std::min(visible.size(), ubo.cloudCenters.size() / layers.size());
        sortNearestPuffs(visible, layerCount);
        for (std::size_t i = 0; i < layerCount; ++i, ++count) {
            const MistPuff& puff = visible[i];
            ubo.cloudCenters[count] = puff.centerAndDensity;
            ubo.cloudInverseRadii[count] = puff.inverseRadiiAndSeed;
            const float radiusY = 1.0f / puff.inverseRadiiAndSeed.y;
            lowest = std::min(lowest, layer.height - radiusY);
            highest = std::max(highest, layer.height + radiusY);
        }
    }
    if (count > 0) ubo.cloudMist = glm::vec4(static_cast<float>(count), lowest, highest, 0.0f);
}

} // namespace

bool Renderer::importModelResource(
    const std::filesystem::path& path,
    std::optional<std::uint32_t> preferredRootNodeId,
    bool playerModel,
    bool rebuildResources
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
        if (!importedMesh.hierarchy.hasBounds) {
            throw std::runtime_error("the imported model has no bounds");
        }
        const glm::vec3 extent = importedMesh.hierarchy.boundsMaximum - importedMesh.hierarchy.boundsMinimum;
        if (!std::isfinite(extent.x) || !std::isfinite(extent.y) || !std::isfinite(extent.z) ||
            std::max({extent.x, extent.y, extent.z}) <= std::numeric_limits<float>::epsilon()) {
            throw std::runtime_error("the imported model has no measurable bounds");
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
    if (rebuildResources) context_->waitIdle();
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
        preferredRootNodeId,
        playerModel,
        rebuildResources
    );
    if (rebuildResources) discoverModelResources();
    if (preserveSun) {
        createSunMesh(&preservedSunNode, &preservedSunMaterial);
    }
    if (rebuildResources) rebuildSceneGeometryResources();

    boundingBoxVertices_.fill(Vertex{});
    materialNameMaterialIndex_ = -1;
    materialTexturePathMaterialIndex_ = -1;
    materialNormalTexturePathMaterialIndex_ = -1;
    materialSpecularTexturePathMaterialIndex_ = -1;
    selectedResourceType_ = ResourceSelectionType::Model;
    selectedResourceMaterialIndex_ = -1;
    resourceImportStatus_ = std::string(playerModel ? "Set player model to " : "Imported ") +
        resolvedPath.filename().string() +
        (playerModel ? " (" : " into the scene (") +
        std::to_string(importedVertexCount) + " vertices, " +
        std::to_string(importedTriangleCount) + " triangles).";
    if (rebuildResources) selectModelResource(resolvedPath);
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
        for (auto& model : forestModels_) {
            for (auto& part : model.parts) {
                if (part.materialIndex > removedMaterialIndex) --part.materialIndex;
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
    auto buffers = createVertexBufferSteps();
    while (buffers.advance()) {}
}

SceneLoadTask Renderer::createVertexBufferSteps() {
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
        co_yield 0;
    }
    co_return true;
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

void Renderer::updateGrassCoverage() {
    if (!terrainScene_) return;
    const glm::vec3 center = cameraPosition_ / modelScale_ + modelCenter_;
    if (grassCoverage_.update(*terrainScene_, {center.x, center.z})) {
        ++grassRevision_;
    }

    const auto& instances = grassCoverage_.instances();
    auto& frame = grassFrameBuffers_[currentFrame_];
    if (instances.empty() || frame.uploadedRevision == grassRevision_) return;

    const VkDeviceSize size = sizeof(GrassInstance) * instances.size();
    constexpr auto memory =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    // drawFrame has already waited for this frame's fence. Allocations for
    // other in-flight frames and the shared terrain buffers stay resident.
    if (!frame.instances || frame.instances->size() < size) {
        frame.instances = std::make_unique<Buffer>(
            *context_, size + size / 4, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, memory
        );
    }
    frame.instances->upload(instances.data(), size);
    frame.uploadedRevision = grassRevision_;
    if (!grassIndexBuffer_) {
        grassIndexBuffer_ = std::make_unique<Buffer>(
            *context_, sizeof(kGrassIndices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, memory
        );
        grassIndexBuffer_->upload(kGrassIndices.data(), sizeof(kGrassIndices));
    }
}

void Renderer::createParticleBuffer() {
    const VkDeviceSize bufferSize = sizeof(ParticleVertex) * (
        ParticleSystem::kMaximumSupportedParticles * ParticleSystem::kVerticesPerParticle *
        kMaximumParticleSystems + kMoveMarkerVertexCount);
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
    particleVertices_.reserve(maximumVertices + kMoveMarkerVertexCount);

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

    appendPlayerMoveMarker(deltaSeconds);
}

void Renderer::appendPlayerMoveMarker(float deltaSeconds) {
    if (!playerMoveMarker_ || !terrainScene_) return;
    constexpr float duration = 0.55f;
    auto& marker = *playerMoveMarker_;
    const float progress = marker.age / duration;
    marker.age += deltaSeconds;
    if (progress >= 1.0f) {
        playerMoveMarker_.reset();
        return;
    }

    const float fade = 1.0f - glm::smoothstep(0.15f, 1.0f, progress);
    const float radius = glm::mix(0.33f, 0.88f, progress);
    const float halfThickness = glm::mix(0.065f, 0.022f, progress);
    const glm::vec2 center(marker.position.x, marker.position.z);
    const auto vertex = [&](glm::vec2 offset, float lift, const glm::vec4& color) -> std::optional<ParticleVertex> {
        const glm::vec2 xz = center + offset;
        const auto height = terrainScene_->renderedHeightAtWorld(xz);
        if (!height) return std::nullopt;
        const glm::vec3 position(xz.x, *height + lift, xz.y);
        // Constant UV at the sprite center gives full triangle coverage.
        return ParticleVertex{(position - modelCenter_) * modelScale_, glm::vec2(0.5f), color};
    };
    const auto triangle = [&](const auto& a, const auto& b, const auto& c) {
        if (a && b && c) particleVertices_.insert(particleVertices_.end(), {*a, *b, *c});
    };

    const auto first = static_cast<std::uint32_t>(particleVertices_.size());
    const glm::vec4 ringColor(0.15f, 0.95f, 0.86f, 0.9f * fade);
    const auto ringEdge = [&](std::size_t segment, float edgeRadius) {
        const float angle = glm::two_pi<float>() * static_cast<float>(segment) / kMoveMarkerRingSegments;
        return vertex(glm::vec2(std::cos(angle), std::sin(angle)) * edgeRadius, 0.12f, ringColor);
    };
    auto inner = ringEdge(0, radius - halfThickness);
    auto outer = ringEdge(0, radius + halfThickness);
    for (std::size_t i = 1; i <= kMoveMarkerRingSegments; ++i) {
        const auto nextInner = ringEdge(i, radius - halfThickness);
        const auto nextOuter = ringEdge(i, radius + halfThickness);
        triangle(inner, outer, nextOuter);
        triangle(inner, nextOuter, nextInner);
        inner = nextInner;
        outer = nextOuter;
    }

    const float arrowRadius = glm::mix(0.95f, 0.42f, glm::smoothstep(0.0f, 0.78f, progress));
    // Let the arrowheads descend toward the ring. Their initial height keeps
    // the click legible over the dense meadow canopy.
    const float arrowLift = glm::mix(1.18f, 0.94f, progress);
    const glm::vec4 arrowOutline(0.02f, 0.16f, 0.14f, fade);
    const glm::vec4 arrowColor(0.43f, 1.0f, 0.92f, fade);
    for (std::size_t i = 0; i < kMoveMarkerArrowCount; ++i) {
        const float angle = glm::two_pi<float>() * static_cast<float>(i) / kMoveMarkerArrowCount;
        const glm::vec2 outward(std::cos(angle), std::sin(angle));
        const glm::vec2 tangent(-outward.y, outward.x);
        const glm::vec2 base = outward * arrowRadius;
        triangle(vertex(base - outward * 0.33f, arrowLift, arrowOutline),
            vertex(base + tangent * 0.22f, arrowLift, arrowOutline),
            vertex(base - tangent * 0.22f, arrowLift, arrowOutline));
        triangle(vertex(base - outward * 0.23f, arrowLift, arrowColor),
            vertex(base - outward * 0.04f + tangent * 0.13f, arrowLift, arrowColor),
            vertex(base - outward * 0.04f - tangent * 0.13f, arrowLift, arrowColor));
    }
    const auto count = static_cast<std::uint32_t>(particleVertices_.size()) - first;
    if (count > 0) {
        particleDrawRanges_.push_back({std::nullopt, first, count, ParticleSystemSettings::BlendMode::Alpha});
    }
}

void Renderer::createTornadoPreview() {
    if (removedSceneNodeIds_.contains(kTornadoPreviewNodeId)) {
        return;
    }

    const std::filesystem::path assets = assetsDirectory_.empty()
        ? locateAssetsDirectory()
        : assetsDirectory_;
    const std::filesystem::path meshPath = assets / "models" / "tornado" / "tornado.obj";

    MeshData tornadoMesh;
    try {
        tornadoMesh = loadModelMesh(meshPath);
    } catch (const std::exception& error) {
        std::cerr << "warning: failed to load tornado mesh '"
                  << meshPath.string() << "': " << error.what() << '\n';
        return;
    }
    if (tornadoMesh.vertices.empty() || tornadoMesh.indices.empty()) {
        std::cerr << "warning: tornado mesh contains no renderable triangles\n";
        return;
    }
    if (meshData_.vertices.size() > std::numeric_limits<std::uint32_t>::max() -
            tornadoMesh.vertices.size() ||
        meshData_.indices.size() > std::numeric_limits<std::uint32_t>::max() -
            tornadoMesh.indices.size()) {
        std::cerr << "warning: tornado mesh exceeds the renderer's 32-bit mesh limits\n";
        return;
    }

    MaterialData tornadoMaterial;
    tornadoMaterial.name = "Tornado VFX (layered energy ribbons)";
    tornadoMaterial.baseColor = glm::vec4(0.68f, 0.82f, 1.0f, 0.94f);
    tornadoMaterial.roughness = 0.18f;
    tornadoMaterial.metallic = 0.0f;
    tornadoMaterial.opacity = 0.96f;
    tornadoMaterial.emissionColor = glm::vec3(0.06f, 0.20f, 0.92f);
    tornadoMaterial.emissionStrength = 3.6f;
    tornadoMaterial.blendMode = MaterialData::BlendMode::Additive;
    tornadoMaterial.depthWrite = false;
    tornadoMaterial.doubleSided = true;
    tornadoMaterial.userCreated = true;
    tornadoMaterial.texturePath = assets / "textures" / "vfx" / "tornado_ribbon.png";
    // The shared material descriptor's normal slot carries a seamless RG
    // flow field. The dedicated shader combines it with the UV-encoded mesh
    // layers instead of treating it as a surface normal map.
    tornadoMaterial.normalTexturePath = assets / "textures" / "vfx" /
        "tornado_distortion.png";
    tornadoMaterial.shaderConfiguration.shader(ShaderStage::Vertex) =
        "shader_tornado.vert";
    tornadoMaterial.shaderConfiguration.shader(ShaderStage::Fragment) =
        "shader_tornado.frag";

    const std::uint32_t vertexOffset = static_cast<std::uint32_t>(meshData_.vertices.size());
    const std::uint32_t indexOffset = static_cast<std::uint32_t>(meshData_.indices.size());
    const auto materialIndex = static_cast<std::uint32_t>(meshData_.materials.size());
    meshData_.materials.push_back(std::move(tornadoMaterial));
    meshData_.vertices.insert(
        meshData_.vertices.end(),
        tornadoMesh.vertices.begin(),
        tornadoMesh.vertices.end()
    );
    meshData_.bindPoseVertices.insert(
        meshData_.bindPoseVertices.end(),
        tornadoMesh.vertices.begin(),
        tornadoMesh.vertices.end()
    );
    meshData_.skinSourceVertices.insert(
        meshData_.skinSourceVertices.end(),
        tornadoMesh.vertices.begin(),
        tornadoMesh.vertices.end()
    );
    for (const std::uint32_t index : tornadoMesh.indices) {
        meshData_.indices.push_back(index + vertexOffset);
    }

    const auto partIndex = static_cast<std::uint32_t>(meshData_.parts.size());
    meshData_.parts.push_back({
        indexOffset,
        static_cast<std::uint32_t>(tornadoMesh.indices.size()),
        materialIndex,
        kTornadoPreviewNodeId
    });

    ModelNode tornadoNode;
    tornadoNode.name = "Tornado VFX";
    tornadoNode.nodeId = kTornadoPreviewNodeId;
    tornadoNode.objectType = SceneObjectType::ImportedMesh;
    tornadoNode.meshCount = 1;
    tornadoNode.globalTransform = glm::mat4(1.0f);
    tornadoNode.renderEnabled = true;
    tornadoNode.meshPartIndices.push_back(partIndex);
    tornadoNode.hasBounds = tornadoMesh.hierarchy.hasBounds;
    tornadoNode.boundsMinimum = tornadoMesh.hierarchy.boundsMinimum;
    tornadoNode.boundsMaximum = tornadoMesh.hierarchy.boundsMaximum;
    meshData_.additionalNodes.push_back(std::move(tornadoNode));
    // Center the showcase around the normalized model center.
    const glm::vec3 tornadoScale(0.42f, 0.34f, 0.42f);
    const float meshHeight = tornadoMesh.hierarchy.boundsMaximum.y -
        tornadoMesh.hierarchy.boundsMinimum.y;
    tornadoPreviewTransform_ =
        glm::translate(glm::mat4(1.0f),
            modelCenter_ - glm::vec3(0.0f, meshHeight * tornadoScale.y * 0.5f, 0.0f)) *
        glm::scale(glm::mat4(1.0f), tornadoScale);
}

void Renderer::createMaterialTextures() {
    auto task = createMaterialTextureSteps(false);
    while (task.advance()) {}
}

SceneLoadTask Renderer::createMaterialTextureSteps(bool incremental) {
    materialTextures_.resize(meshData_.materials.size());
    normalTextures_.resize(meshData_.materials.size());
    specularTextures_.resize(meshData_.materials.size());
    alphaMaskTextures_.resize(meshData_.materials.size());
    bool needsWhiteFallbackTexture = false;
    bool needsNormalFallbackTexture = false;

    const auto materialCount = meshData_.materials.size();
    for (std::size_t index = 0; index < materialCount; ++index) {
        const auto& material = meshData_.materials[index];
        const std::array<const EmbeddedTextureData*, 4> embedded{
            &material.embeddedTexture, &material.embeddedNormalTexture,
            &material.embeddedSpecularTexture, &material.embeddedAlphaMask
        };
        const std::array<std::filesystem::path, 4> paths{
            material.texturePath, material.normalTexturePath, material.specularTexturePath, {}
        };
        const std::array slots{&materialTextures_[index], &normalTextures_[index],
                               &specularTextures_[index], &alphaMaskTextures_[index]};
        constexpr std::array labels{"base color", "normal", "specular", "alpha mask"};
        for (std::size_t slot = 0; slot < slots.size(); ++slot) {
            const auto* source = embedded[slot];
            if (source && source->empty()) source = nullptr;
            if (!source && paths[slot].empty()) {
                slots[slot]->reset();
                continue;
            }
            const auto progress = 16 + static_cast<int>(80 * (index * slots.size() + slot)
                                                       / (materialCount * slots.size()));
            try {
                if (source && !source->compressed) {
                    *slots[slot] = std::make_unique<Texture>(*context_, source->data, source->width,
                        source->height, false, source->srgb ? TextureColorSpace::Srgb : TextureColorSpace::Linear);
                } else {
                    const auto decodePixels = [&] {
                        return source ? decodeTexturePixels(source->data) : decodeTexturePixels(paths[slot]);
                    };
                    DecodedTexturePixels image;
                    if (incremental) {
                        auto decode = std::async(std::launch::async, decodePixels);
                        while (decode.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                            co_yield progress;
                        }
                        image = decode.get();
                        co_yield progress;
                    } else {
                        image = decodePixels();
                    }
                    *slots[slot] = std::make_unique<Texture>(*context_, image, false);
                }
            } catch (const std::exception& error) {
                slots[slot]->reset();
                std::cerr << "warning: failed to load " << labels[slot] << " texture for material "
                          << index << ": " << error.what() << '\n';
            }
            if (incremental) co_yield progress;
        }
        needsWhiteFallbackTexture = needsWhiteFallbackTexture ||
            !materialTextures_[index] || !specularTextures_[index];
        needsNormalFallbackTexture = needsNormalFallbackTexture || !normalTextures_[index];
    }
    if (incremental) co_yield 96;

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

    if (!fallbackTerrainLayers_) {
        const std::vector<std::uint8_t> whitePixel = {255, 255, 255, 255};
        fallbackTerrainLayers_ = std::make_unique<Texture>(*context_, whitePixel, 1, 1, true);
    }
    co_return true;
}

std::filesystem::path Renderer::defaultSkyboxPath() const {
    return (assetsDirectory_.empty() ? locateAssetsDirectory() : assetsDirectory_) /
        "textures" / "skybox_botw_day.png";
}

void Renderer::createSkyboxTexture() {
    skyboxTexture_.reset();
    const auto skyboxPath = skyboxTexturePath_.empty() ? defaultSkyboxPath() : skyboxTexturePath_;
    try {
        skyboxTexture_ = std::make_unique<Texture>(
            *context_,
            skyboxPath,
            VK_SAMPLER_ADDRESS_MODE_REPEAT,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
        );
    } catch (const std::exception& error) {
        std::cerr << "warning: failed to load skybox texture '"
                  << skyboxPath.string() << "': " << error.what() << '\n';
    }
}

void Renderer::applySkyboxTexture(const std::filesystem::path& path) {
    auto texture = std::make_unique<Texture>(*context_,
        path.empty() ? defaultSkyboxPath() : path,
        VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    context_->waitIdle();
    skyboxTexture_ = std::move(texture);
    skyboxTexturePath_ = path;
    rebuildMaterialResources();
}

namespace {

// A BNTX array supplies pixels by layer. A terrain pack separately assigns
// those layers to the 8-bit material IDs used by its map samples.
std::map<unsigned, unsigned> readTerrainAlbedoBindings(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open material layer table: " + path.string());

    std::map<unsigned, unsigned> bindings;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (const auto comment = line.find('#'); comment != std::string::npos) line.resize(comment);
        std::istringstream row(line);
        row >> std::ws;
        if (row.eof()) continue;

        unsigned id = 0, layer = 0;
        std::string extra;
        if (!(row >> id >> layer) || (row >> extra) || id > 255 || !bindings.emplace(id, layer).second) {
            throw std::runtime_error(path.string() + ":" + std::to_string(lineNumber) +
                ": expected a unique material ID (0–255) followed by a zero-based array layer");
        }
    }
    if (!input.eof()) throw std::runtime_error("failed to read material layer table: " + path.string());
    return bindings;
}

void decodeTerrainAlbedoArchive(const std::filesystem::path& archivePath,
    const std::filesystem::path& tablePath, std::array<int, 256>& albedoLayers,
    DecodedTexturePixels& albedo, TextureColorSpace& colorSpace, std::array<bool, 256>& rockLayers) {
    const SbfresTextureArchive archive(archivePath);
    const auto entries = archive.entries();
    const auto entry = std::find_if(entries.begin(), entries.end(), [](const SbfresTextureInfo& info) {
        return info.name == "MaterialAlb";
    });
    if (entry == entries.end() || entry->layers == 0) {
        throw std::runtime_error("missing MaterialAlb layers in " + archivePath.string());
    }
    const auto index = static_cast<std::size_t>(entry - entries.begin());

    const auto materialLayers = archive.readMaterialLayerIndices(index);
    std::vector<bool> rockSourceLayers(entry->layers);
    try {
        const auto materialFiles = archive.readMaterialFileNames(index);
        if (materialFiles.size() == materialLayers.size()) {
            for (std::size_t id = 0; id < materialFiles.size(); ++id) {
                if (materialFiles[id].starts_with("Rock_")) rockSourceLayers[materialLayers[id]] = true;
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "warning: skipped rock classification for " << archivePath << ": " << error.what() << '\n';
    }
    std::map<unsigned, unsigned> sourceLayers;
    for (unsigned id = 0; id < materialLayers.size(); ++id) {
        sourceLayers.emplace(id, materialLayers[id]);
    }
    if (std::filesystem::exists(tablePath)) {
        for (const auto& [id, layer] : readTerrainAlbedoBindings(tablePath)) {
            if (layer >= entry->layers) {
                throw std::runtime_error("MaterialAlb layer " + std::to_string(layer) + " for material ID " +
                    std::to_string(id) + " exceeds the array in " + archivePath.string());
            }
            sourceLayers[id] = layer;
        }
    }

    if (sourceLayers.empty()) {
        throw std::runtime_error("missing MaterialAlb array_index and explicit material layer bindings in " +
            archivePath.string());
    }

    std::map<unsigned, unsigned> uploadedLayers;
    for (const auto& [id, layer] : sourceLayers) uploadedLayers.try_emplace(layer, 0);
    albedo.width = entry->width;
    albedo.height = entry->height;
    for (auto& [layer, arrayIndex] : uploadedLayers) {
        auto pixels = archive.decodeLayer(index, layer);
        if (albedo.layers == 0) {
            const auto layerBytes = pixels.data.size();
            if (uploadedLayers.size() > albedo.rgba.max_size() / layerBytes) {
                throw std::runtime_error("decoded MaterialAlb array is too large");
            }
            albedo.rgba.reserve(layerBytes * uploadedLayers.size());
            colorSpace = pixels.srgb ? TextureColorSpace::Srgb : TextureColorSpace::Linear;
        }
        albedo.rgba.insert(albedo.rgba.end(), pixels.data.begin(), pixels.data.end());
        arrayIndex = albedo.layers++;
        rockLayers[arrayIndex] = rockSourceLayers[layer];
    }
    for (const auto& [id, layer] : sourceLayers) {
        albedoLayers[id] = static_cast<int>(uploadedLayers.at(layer));
    }
    std::cerr << "Terrain albedo: " << albedo.layers << '/' << entry->layers << " MaterialAlb layers from "
              << archivePath.string() << '\n';
}

void decodeTerrainAlbedoPng(const std::filesystem::path& directory, std::array<int, 256>& albedoLayers,
    DecodedTexturePixels& albedo) {
    std::map<unsigned, std::filesystem::path> materials;
    if (std::filesystem::exists(directory)) {
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".png") continue;
            const auto stem = entry.path().stem().string();
            unsigned id = 0;
            const auto [end, error] = std::from_chars(stem.data(), stem.data() + stem.size(), id);
            if (error != std::errc{} || end != stem.data() + stem.size() || id > 255) continue;
            if (!materials.emplace(id, entry.path()).second) {
                throw std::runtime_error("multiple albedo textures for material ID " + std::to_string(id));
            }
        }
    }

    for (const auto& [id, path] : materials) {
        try {
            const auto image = decodeTexturePixels(path);
            if (albedo.layers == 0) {
                albedo.width = image.width;
                albedo.height = image.height;
            }
            if (image.width != albedo.width || image.height != albedo.height) {
                throw std::runtime_error("albedo dimensions differ from the rest of the library");
            }
            albedo.rgba.insert(albedo.rgba.end(), image.rgba.begin(), image.rgba.end());
            albedoLayers[id] = static_cast<int>(albedo.layers++);
        } catch (const std::runtime_error& error) {
            std::cerr << "warning: skipped material texture '" << path.string() << "': " << error.what() << '\n';
        }
    }
}

std::array<glm::vec4, 256> buildTerrainPalette(const DecodedTexturePixels& albedo,
    TextureColorSpace colorSpace, const std::array<bool, 256>& rockLayers) {
    std::array<glm::vec4, 256> palette{};
    palette.fill(glm::vec4(0.0f));
    std::array<double, 256> linear{};
    for (std::size_t i = 0; i < linear.size(); ++i) {
        const double value = static_cast<double>(i) / 255.0;
        linear[i] = colorSpace == TextureColorSpace::Linear ? value :
            (value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4));
    }
    const auto pixelCount = static_cast<std::size_t>(albedo.width) * albedo.height;
    for (std::size_t layer = 0; layer < albedo.layers; ++layer) {
        const auto* bytes = albedo.rgba.data() + layer * pixelCount * 4;
        glm::dvec3 sum(0.0);
        for (std::size_t i = 0; i < pixelCount; ++i) {
            sum += glm::dvec3(linear[bytes[4 * i]], linear[bytes[4 * i + 1]], linear[bytes[4 * i + 2]]);
        }
        palette[layer] = glm::vec4(glm::vec3(sum / static_cast<double>(pixelCount)), rockLayers[layer] ? 1.0f : 0.0f);
    }
    return palette;
}

} // namespace

Renderer::TerrainMaterialImages Renderer::decodeTerrainMaterials(const TerrainScene& terrain,
    const std::filesystem::path& terrainRoot, const std::atomic<bool>* cancelled) {
    std::array<int, 256> albedoLayers;
    albedoLayers.fill(-1);
    TerrainMaterialImages images;
    images.albedo.layers = 0;
    std::array<bool, 256> rockLayers{};
    if (terrain.materialTileCount() != 0) {
        const auto source = terrainRoot / "source";
        const auto archive = source / "Terrain.Tex.sbfres";
        const auto table = source / "material-albedo-layers.txt";
        if (std::filesystem::exists(archive) || std::filesystem::exists(table)) {
            decodeTerrainAlbedoArchive(archive, table, albedoLayers, images.albedo, images.albedoColorSpace, rockLayers);
        } else {
            decodeTerrainAlbedoPng(terrainRoot / "materials", albedoLayers, images.albedo);
        }
    }
    auto layers = terrain.buildMaterialLayers(albedoLayers);
    if (layers.count != 0 && (!cancelled || !cancelled->load(std::memory_order_relaxed))) {
        auto macro = terrain.bakeMacroAlbedo(layers,
            buildTerrainPalette(images.albedo, images.albedoColorSpace, rockLayers), cancelled);
        images.macroAlbedo = {std::move(macro.pixels), macro.width, macro.height, 1};
        images.rockCoverage = {std::move(macro.rockCoverage), macro.width, macro.height, 1};
    }
    images.maps = {std::move(layers.pixels), kTerrainMaterialPaddedResolution,
        kTerrainMaterialPaddedResolution, layers.count};
    images.layerForTile = std::move(layers.layerForTile);
    images.missingIds = std::move(layers.missingIds);
    return images;
}

std::optional<Renderer::TerrainGrassImages> Renderer::decodeTerrainGrass(const std::filesystem::path& terrainRoot) {
    const auto source = terrainRoot / "source";
    const auto model = source / "Terrain.sbfres";
    const auto textures = source / "Terrain.Tex.sbfres";
    // Legacy terrain packs still load. Only archive-backed grass is drawn.
    if (!std::filesystem::exists(model) && !std::filesystem::exists(textures)) return std::nullopt;
    SbfresTextureArchive archive;
    // Open the model/texture pair with the existing BFRES loader. The terrain
    // grid and blade mesh use VulkCore geometry, not the source draw proxies.
    loadSbfresMesh(model, textures, {}, false, &archive);
    const auto entryIndex = [&](std::string_view name) {
        const auto entries = archive.entries();
        const auto entry = std::find_if(entries.begin(), entries.end(),
            [name](const SbfresTextureInfo& info) { return info.name == name; });
        if (entry == entries.end()) throw std::runtime_error("missing BFRES texture " + std::string(name));
        return static_cast<std::size_t>(entry - entries.begin());
    };
    const auto decode = [&](std::string_view name, std::uint32_t count = 1) {
        constexpr std::uint32_t layer = 0;
        const auto index = entryIndex(name);
        auto image = archive.decodeLayer(index, layer);
        for (std::uint32_t n = 1; n < count; ++n) {
            auto next = archive.decodeLayer(index, layer + n);
            image.data.insert(image.data.end(), next.data.begin(), next.data.end());
        }
        return image;
    };
    TerrainGrassImages images;
    images.images = {
        decode("GrassAlb"),
        kGrassCrossCardsEnabled ? decode("GrassCrossAlb", 2) : EmbeddedTextureData{},
        decode("GrassSpm")
    };
    auto coverage = archive.decodeLayer(entryIndex("GrassCoverAlb"), 0);
    std::vector<std::uint8_t> red(coverage.data.size() / 4);
    for (std::size_t i = 0; i < red.size(); ++i) red[i] = coverage.data[i * 4];
    images.coverMask = GrassCoverMask(coverage.width, coverage.height, std::move(red));
    return images;
}

SceneLoadTask Renderer::uploadTerrainGrass(TerrainGrassImages images) {
    constexpr auto clamp = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    TerrainGrassResources resources;
    const std::array targets{&resources.bladeAlbedo, &resources.crossAlbedo, &resources.specular};
    for (std::size_t i = 0; i < targets.size(); ++i) {
        if (!kGrassCrossCardsEnabled && i == 1) continue;
        auto& image = images.images[i];
        const std::uint32_t layers = i == 1 ? 2 : 1;
        *targets[i] = std::make_unique<Texture>(*context_, image.data, image.width, image.height,
            layers > 1, image.srgb ? TextureColorSpace::Srgb : TextureColorSpace::Linear, layers, clamp);
        image = {};
        co_yield static_cast<int>(i + 1);
    }
    if (!images.groundCover.rgba.empty()) {
        try {
            resources.groundCover = std::make_unique<Texture>(*context_, images.groundCover, false,
                TextureColorSpace::Linear, clamp, clamp);
        } catch (const std::exception& error) {
            std::cerr << "warning: terrain grass ground cover upload failed: " << error.what() << '\n';
        }
    }
    grassCoverage_.setCoverMask(std::move(images.coverMask));
    terrainGrass_ = std::move(resources);
    co_return true;
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

void Renderer::clearMaterialResources() {
    context_->waitIdle();
    materialTextures_.clear();
    normalTextures_.clear();
    specularTextures_.clear();
    alphaMaskTextures_.clear();
    fallbackTexture_.reset();
    fallbackNormalTexture_.reset();
    descriptorSets_.clear();
    skyboxDescriptorSets_.clear();
    particleDescriptorSets_.clear();
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(context_->device(), descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }
}

void Renderer::rebuildMaterialResources() {
    clearMaterialResources();
    createMaterialTextures();
    createDescriptorPool();
    createDescriptorSets();
}

void Renderer::createUniformBuffers() {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(context_->physicalDevice(), &properties);
    const VkDeviceSize alignment = properties.limits.minUniformBufferOffsetAlignment;
    uniformSliceStride_ = static_cast<std::uint32_t>(((sizeof(UniformBufferObject) + alignment - 1) / alignment) * alignment);
    const VkDeviceSize bufferSize = uniformSliceStride_ * (sunShadowRenderPass_ ? 1 + kSunShadowCascadeCount : 1);
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
    const uint32_t skyboxDescriptorCount = kMaxFramesInFlight;
    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    poolSizes[0].descriptorCount =
        materialDescriptorCount + particleDescriptorCapacity + skyboxDescriptorCount;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = (materialDescriptorCount + particleDescriptorCapacity + skyboxDescriptorCount) *
        kMaterialImageBindingCount;
    poolSizes[2] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        materialDescriptorCount + particleDescriptorCapacity + skyboxDescriptorCount};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets =
        materialDescriptorCount + particleDescriptorCapacity + skyboxDescriptorCount;

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
        auto& roadFrame = roadFrameBuffers_[i];
        if (!roadFrame.buffer) {
            const std::array<std::uint32_t, 5> emptyIndex{};
            roadFrame.buffer = std::make_unique<Buffer>(*context_, sizeof(emptyIndex),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            roadFrame.buffer->upload(emptyIndex.data(), sizeof(emptyIndex));
        }
        bindRoadIndex(i);
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

            const auto& fragment = meshData_.materials[materialIndex].shaderConfiguration.shader(ShaderStage::Fragment);
            const bool terrainMaterial = fragment == "shader_terrain.frag";
            const bool grassMaterial = fragment == "shader_grass.frag";
            // Terrain uses the albedo and normal slots for macro albedo and rock coverage.
            if (terrainMaterial) {
                texture = terrainMacroAlbedo_ ? terrainMacroAlbedo_.get() : fallbackTexture_.get();
                normalTexture = terrainRockCoverage_ ? terrainRockCoverage_.get() : fallbackTexture_.get();
            }
            if (grassMaterial && terrainGrass_.specular) specularTexture = terrainGrass_.specular.get();

            const Texture* mask = alphaMaskTextures_[materialIndex].get();
            if (mask == nullptr) mask = fallbackTexture_.get();
            const VkDescriptorImageInfo maskInfo{
                mask->sampler(), mask->imageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            };
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

            const auto layerInfo = [this](const std::unique_ptr<Texture>& layers) {
                const Texture& image = layers ? *layers : *fallbackTerrainLayers_;
                return VkDescriptorImageInfo{
                    image.sampler(), image.imageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                };
            };
            const std::array terrainImageInfos = {
                layerInfo(grassMaterial ? terrainGrass_.crossAlbedo : terrainAlbedoLayers_),
                layerInfo(terrainMaterial ? terrainMaterialMaps_ : fallbackTerrainLayers_)
            };

            VkDescriptorImageInfo depthImageInfo{};
            depthImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            depthImageInfo.imageView = sceneRenderTargets_[i].depthImageView;
            depthImageInfo.sampler = sceneRenderTargets_[i].depthSampler;

            const Texture* grassAlbedo = grassMaterial ? terrainGrass_.bladeAlbedo.get() : nullptr;
            if (terrainMaterial) grassAlbedo = terrainGrass_.groundCover.get();
            if (!grassAlbedo) grassAlbedo = terrainMaterial ? fallbackTexture_.get() : texture;
            const VkDescriptorImageInfo grassAlbedoInfo{
                grassAlbedo->sampler(), grassAlbedo->imageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            };
            const auto shadowInfo = sunShadowImageInfo(i);
            std::array<VkWriteDescriptorSet, kMaterialImageBindingCount> descriptorWrites{};
            descriptorWrites[9] = {
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSets_[descriptorSetIndex],
                kSunShadowBinding, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &shadowInfo, nullptr, nullptr
            };
            descriptorWrites[7] = {
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSets_[descriptorSetIndex],
                kAlphaMaskBinding, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &maskInfo, nullptr, nullptr
            };
            descriptorWrites[8] = {
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSets_[descriptorSetIndex],
                kTerrainGrassAlbedoBinding, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &grassAlbedoInfo, nullptr, nullptr
            };
            descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[0].dstSet = descriptorSets_[descriptorSetIndex];
            descriptorWrites[0].dstBinding = 0;
            descriptorWrites[0].dstArrayElement = 0;
            descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
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

            for (std::size_t layerIndex = 0; layerIndex < terrainImageInfos.size(); ++layerIndex) {
                auto& write = descriptorWrites[5 + layerIndex];
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = descriptorSets_[descriptorSetIndex];
                write.dstBinding = static_cast<uint32_t>(6 + layerIndex);
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.descriptorCount = 1;
                write.pImageInfo = &terrainImageInfos[layerIndex];
            }

            vkUpdateDescriptorSets(
                context_->device(),
                static_cast<uint32_t>(descriptorWrites.size()),
                descriptorWrites.data(),
                0,
                nullptr
            );
        }
    }
    skyboxDescriptorSets_.clear();
    std::array<VkDescriptorSetLayout, kMaxFramesInFlight> skyboxLayouts{};
    skyboxLayouts.fill(descriptorSetLayout_);
    skyboxDescriptorSets_.resize(kMaxFramesInFlight);
    VkDescriptorSetAllocateInfo skyboxAllocateInfo{};
    skyboxAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    skyboxAllocateInfo.descriptorPool = descriptorPool_;
    skyboxAllocateInfo.descriptorSetCount = kMaxFramesInFlight;
    skyboxAllocateInfo.pSetLayouts = skyboxLayouts.data();
    if (vkAllocateDescriptorSets(
            context_->device(),
            &skyboxAllocateInfo,
            skyboxDescriptorSets_.data()
        ) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate skybox descriptor sets");
    }

    const Texture* skyboxDescriptorTexture = skyboxTexture_ != nullptr
        ? skyboxTexture_.get()
        : fallbackTexture_.get();
    if (skyboxDescriptorTexture == nullptr) {
        throw std::runtime_error("skybox has no usable texture fallback");
    }
    for (std::uint32_t frameIndex = 0; frameIndex < kMaxFramesInFlight; ++frameIndex) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers_[frameIndex]->handle();
        bufferInfo.range = sizeof(UniformBufferObject);

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = skyboxDescriptorTexture->imageView();
        imageInfo.sampler = skyboxDescriptorTexture->sampler();

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = skyboxDescriptorSets_[frameIndex];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &bufferInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = skyboxDescriptorSets_[frameIndex];
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

        const auto shadowInfo = sunShadowImageInfo(frameIndex);
        std::array<VkWriteDescriptorSet, 3> writes{};
        writes[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorIt->second[frameIndex],
            kSunShadowBinding, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &shadowInfo, nullptr, nullptr};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptorIt->second[frameIndex];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
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
    const float kMinimumCameraDistance = effectCamera_
        ? std::min(0.05f, modelScale_ * 0.0001f)
        : (playerController_.active() || terrainEditorActive()
            ? std::max(0.00001f, modelScale_ * 0.02f) : 0.05f);
    glm::vec3 targetDelta = cameraTarget_ - cameraPosition_;
    if (glm::length(targetDelta) < kMinimumCameraDistance) {
        cameraPosition_ = cameraTarget_ + glm::vec3(0.0f, 0.0f, 4.0f);
        targetDelta = cameraTarget_ - cameraPosition_;
        cameraBasisInitialized_ = false;
    }

    cameraForward_ = glm::normalize(targetDelta);

    if (!cameraBasisInitialized_) {
        const glm::vec3 worldUp = terrainEditorActive() ? glm::vec3(0.0f, 1.0f, 0.0f) :
            (effectCamera_ ? effectCamera_->up :
                (authoredView_.enabled ? authoredView_.up :
                    (playerController_.active() ? glm::vec3(0.0f, 1.0f, 0.0f) : kDefaultSceneUp)));
        // At the exact pole the position alone does not define camera roll,
        // so retain the historical front-view orientation as the initial one.
        glm::vec3 initialUp = worldUp -
            cameraForward_ * glm::dot(worldUp, cameraForward_);
        if (glm::length(initialUp) < kMinimumBasisLength) {
            const glm::vec3 fallback = std::abs(cameraForward_.y) < 0.9f
                ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
            initialUp = fallback - cameraForward_ * glm::dot(fallback, cameraForward_);
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
                : kDefaultSceneUp;
            continuousUp = worldUp -
                cameraForward_ * glm::dot(worldUp, cameraForward_);
        }
        cameraUp_ = glm::normalize(continuousUp);
    }

    cameraRight_ = glm::normalize(glm::cross(cameraForward_, cameraUp_));
    cameraUp_ = glm::normalize(glm::cross(cameraRight_, cameraForward_));
}

std::optional<glm::mat4> Renderer::updateUniformBuffer() {
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
    const float weatherTime = fixedFrameOutput_.empty() ? elapsedSeconds : transitionPlayer_.flowTime;
    ubo.time = glm::vec4(elapsedSeconds, transitionPlayer_.flowTime, hdrSettings_.exposure, weatherTime);
    ubo.view = glm::lookAt(cameraPosition_, cameraTarget_, cameraUp_);

    const VkExtent2D extent = swapChain_->extent();
    if (extent.height == 0) {
        return std::nullopt;
    }
    ubo.viewport = glm::vec4(
        static_cast<float>(extent.width),
        static_cast<float>(extent.height),
        1.0f / static_cast<float>(extent.width),
        1.0f / static_cast<float>(extent.height)
    );
    const float nearPlane = playerController_.active() || terrainEditorActive()
        ? std::max(0.00001f, modelScale_ * 0.02f)
        : 0.01f;
#ifdef VULKCORE_OHOS
    constexpr bool coverReferenceViewport = true;
#else
    constexpr bool coverReferenceViewport = false;
#endif
    const float aspect = static_cast<float>(extent.width) / extent.height;
    const bool playerView = playerController_.active() && !terrainEditorActive();
    const float verticalFov = playerView ? kPlayerSceneVerticalFov : kDefaultSceneVerticalFov;
    ubo.projection = sceneProjection(
        aspect,
        nearPlane,
        100.0f,
        coverReferenceViewport,
        verticalFov
    );
    if (playerView) {
        // Widen the horizontal frustum by 10% at the selected vertical FOV.
        ubo.projection[0][0] /= kPlayerHorizontalFovScale;
    }

    if (authoredView_.enabled) {
        const float zoom = authoredView_.minimumAspect > 0.0f
            ? std::min(1.0f, aspect / authoredView_.minimumAspect) : 1.0f;
        const float fov = transitionSettings_.enabled ? transitionCameraFov_ : authoredView_.fov;
        ubo.projection = glm::perspectiveRH_ZO(
            2.0f * std::atan(std::tan(glm::radians(fov) * 0.5f) / zoom), aspect, nearPlane, 100.0f
        );
        ubo.projection[1][1] *= -1.0f;
    }

    if (effectCamera_ && !terrainEditorActive()) {
        glm::vec4 area{0.0f, 0.0f, 1.0f, 1.0f};
#ifndef VULKCORE_OHOS
        if (toolUiVisible() && fixedFrameOutput_.empty()) {
            const auto* viewport = ImGui::GetMainViewport();
            const auto& rect = sceneEditorViewport_;
            if (rect.z > rect.x && rect.w > rect.y && viewport->Size.x > 0 && viewport->Size.y > 0) {
                const glm::vec4 origin(viewport->Pos.x, viewport->Pos.y, viewport->Pos.x, viewport->Pos.y);
                const glm::vec4 size(viewport->Size.x, viewport->Size.y, viewport->Size.x, viewport->Size.y);
                area = glm::clamp((rect - origin) / size, 0.0f, 1.0f);
            }
        }
#endif
        const float distance = glm::distance(cameraPosition_, cameraTarget_);
        ubo.projection = fittedSceneProjection(effectCamera_->fov, effectCamera_->aspect, aspect, area,
            std::min(nearPlane, distance * 0.01f), std::max(100.0f, distance * 8.0f));
    }

#ifndef VULKCORE_OHOS
    if (terrainEditorActive() && terrainEditorTopDown_) {
        const float halfHeight = terrainEditorCamera_->distance * modelScale_ * 0.42f;
        const float halfWidth = halfHeight * aspect;
        ubo.projection = glm::orthoRH_ZO(-halfWidth, halfWidth, -halfHeight, halfHeight, nearPlane, 100.0f);
        ubo.projection[1][1] *= -1.0f;
    }
#endif

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
    ubo.grassBladeWidthRange = glm::vec4(grassGrowth_.minimumBladeWidth, grassGrowth_.maximumBladeWidth, 0.0f, 0.0f);
    if (skyboxTexture_ != nullptr && skyboxEnabled_) {
        ubo.daylight = glm::vec4(1.0f, weatherTime, 0.0f, 0.0f);
    }
    if (distanceFogSettings_.enabled) {
        const auto& fog = distanceFogSettings_;
        ubo.distanceFogParams = glm::vec4(fog.startDistance, fog.halfDistance, fog.maxOpacity, 1.0f);
        ubo.distanceFogHorizon = glm::vec4(fog.horizonColor, 0.0f);
        ubo.distanceFogUpper = glm::vec4(fog.upperColor, 0.0f);
    }
    if (playerController_.active()) {
        const glm::vec3 normalizedPlayerPosition = modelScale_ * (
            playerController_.position() - modelCenter_
        );
        ubo.playerPosition = glm::vec4(normalizedPlayerPosition, 1.0f);
        ubo.playerVelocity = glm::vec4(playerController_.velocity(), 0.0f);
    }

    const glm::mat4 clipFromWorld = ubo.projection * ubo.view * ubo.model;
    if (ubo.daylight.x > 0.0f) {
        fillCloudMist(ubo, weatherTime, clipFromWorld);
    }
    if (terrainScene_ && distanceFogSettings_.floatingMistOpacity > 0.0f) {
        fillFloatingMist(ubo, *terrainScene_, distanceFogSettings_.floatingMistOpacity, weatherTime, clipFromWorld);
    }
    updateSunShadowUniforms(ubo);
    prepareRealtimeEffects(ubo);
    uniformBuffers_[currentFrame_]->upload(&ubo, sizeof(ubo));
    return clipFromWorld;
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
    inFlightFences_.resize(kMaxFramesInFlight);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (vkCreateSemaphore(context_->device(), &semaphoreInfo, nullptr, &imageAvailableSemaphores_[i]) != VK_SUCCESS ||
            vkCreateFence(context_->device(), &fenceInfo, nullptr, &inFlightFences_[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create synchronization objects for a frame!");
        }
    }
    recreatePresentSemaphores();
}

void Renderer::recreatePresentSemaphores() {
    // A frame fence does not cover presentation. Reacquiring the same swap
    // chain image guarantees its previous presentation wait has completed.
    const VkDevice device = context_->device();
    for (const VkSemaphore semaphore : renderFinishedSemaphores_) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
    renderFinishedSemaphores_.assign(swapChain_->framebuffers().size(), VK_NULL_HANDLE);
    VkSemaphoreCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (VkSemaphore& semaphore : renderFinishedSemaphores_) {
        if (vkCreateSemaphore(device, &info, nullptr, &semaphore) != VK_SUCCESS) {
            throw std::runtime_error("failed to create a presentation semaphore!");
        }
    }
}



} // namespace VulkCore
