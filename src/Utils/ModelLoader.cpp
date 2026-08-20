#include "vulkcore/Utils/ModelLoader.hpp"

#include <assimp/Importer.hpp>
#include <assimp/anim.h>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/matrix3x3.h>
#include <assimp/matrix4x4.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>

namespace {

std::filesystem::path resolveTexturePath(
    const std::filesystem::path& modelPath,
    const aiString& texturePath
) {
    std::string pathString = texturePath.C_Str();
    std::replace(pathString.begin(), pathString.end(), '\\', '/');
    if (pathString.empty() || pathString.front() == '*') {
        return {};
    }

    const std::filesystem::path rawPath(pathString);
    const std::filesystem::path modelDirectory = modelPath.parent_path();
    const std::filesystem::path candidates[] = {
        rawPath,
        modelDirectory / rawPath,
        modelDirectory / rawPath.filename()
    };
    for (const auto& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate;
        }
    }
    return {};
}

std::filesystem::path defaultDiffuseTexturePath(
    const std::filesystem::path& modelPath,
    const std::string& materialName
) {
    return modelPath.parent_path() / "tex" / (materialName + "-diffuse.png");
}

std::filesystem::path defaultMaterialTexturePath(
    const std::filesystem::path& modelPath,
    const std::string& materialName,
    const char* textureName
) {
    return modelPath.parent_path() / "tex" / (materialName + "-" + textureName + ".png");
}

std::filesystem::path replaceDiffuseTextureName(
    const std::filesystem::path& diffusePath,
    const char* textureName
) {
    std::string filename = diffusePath.filename().string();
    const std::string diffuseSuffix = "-diffuse";
    const std::size_t suffixPosition = filename.rfind(diffuseSuffix);
    if (suffixPosition != std::string::npos) {
        filename.replace(suffixPosition, diffuseSuffix.size(), "-" + std::string(textureName));
        return diffusePath.parent_path() / filename;
    }

    const std::filesystem::path stem = diffusePath.stem();
    const std::string extension = diffusePath.extension().string();
    return diffusePath.parent_path() /
        (stem.string() + "-" + textureName + extension);
}

std::string stripNumericSuffix(std::string materialName) {
    const std::size_t dotPosition = materialName.rfind('.');
    if (dotPosition == std::string::npos || dotPosition == 0 || dotPosition + 1 >= materialName.size()) {
        return materialName;
    }

    const bool isNumericSuffix = std::all_of(
        materialName.begin() + static_cast<std::ptrdiff_t>(dotPosition + 1),
        materialName.end(),
        [](unsigned char character) {
            return std::isdigit(character) != 0;
        }
    );
    if (isNumericSuffix) {
        materialName.erase(dotPosition);
    }
    return materialName;
}

VulkCore::EmbeddedTextureData copyEmbeddedTexture(
    const aiScene& scene,
    const aiString& texturePath
) {
    VulkCore::EmbeddedTextureData embeddedTexture;
    const aiTexture* texture = scene.GetEmbeddedTexture(texturePath.C_Str());
    if (texture == nullptr || texture->pcData == nullptr || texture->mWidth == 0) {
        return embeddedTexture;
    }

    if (texture->mHeight == 0) {
        embeddedTexture.data.resize(texture->mWidth);
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(texture->pcData);
        std::copy(bytes, bytes + texture->mWidth, embeddedTexture.data.begin());
        embeddedTexture.width = texture->mWidth;
        embeddedTexture.height = 1;
        embeddedTexture.compressed = true;
        return embeddedTexture;
    }

    const std::uint64_t pixelCount =
        static_cast<std::uint64_t>(texture->mWidth) * texture->mHeight;
    if (pixelCount > std::numeric_limits<std::size_t>::max() / 4) {
        throw std::overflow_error("embedded texture is too large");
    }

    embeddedTexture.data.resize(static_cast<std::size_t>(pixelCount) * 4);
    for (std::uint64_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
        const aiTexel& source = texture->pcData[pixelIndex];
        const std::size_t destinationIndex = static_cast<std::size_t>(pixelIndex) * 4;
        embeddedTexture.data[destinationIndex + 0] = source.r;
        embeddedTexture.data[destinationIndex + 1] = source.g;
        embeddedTexture.data[destinationIndex + 2] = source.b;
        embeddedTexture.data[destinationIndex + 3] = source.a;
    }
    embeddedTexture.width = texture->mWidth;
    embeddedTexture.height = texture->mHeight;
    return embeddedTexture;
}

std::optional<std::uint32_t> appendMeshGeometry(
    const aiMesh* mesh,
    const aiMatrix4x4& transform,
    std::uint32_t nodeId,
    VulkCore::MeshData& meshData,
    const std::unordered_map<std::string, std::uint32_t>& boneIndices
) {
    if (mesh == nullptr || mesh->mNumVertices == 0) {
        return std::nullopt;
    }

    if (meshData.vertices.size() > std::numeric_limits<std::uint32_t>::max() - mesh->mNumVertices ||
        meshData.indices.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::overflow_error("model contains more vertices or indices than 32-bit buffers support");
    }

    const std::size_t initialVertexCount = meshData.vertices.size();
    const std::uint32_t vertexOffset = static_cast<std::uint32_t>(initialVertexCount);
    const std::uint32_t firstIndex = static_cast<std::uint32_t>(meshData.indices.size());
    const std::uint32_t materialIndex =
        mesh->mMaterialIndex < meshData.materials.size() ? mesh->mMaterialIndex : 0;
    const bool isSkinned = mesh->mNumBones > 0 && mesh->mBones != nullptr;
    aiMatrix3x3 normalTransform(transform);
    normalTransform.Inverse().Transpose();

    struct VertexInfluences {
        std::array<std::uint32_t, 4> indices{};
        std::array<float, 4> weights{};
        std::size_t count = 0;
    };
    std::vector<VertexInfluences> influences(mesh->mNumVertices);
    if (isSkinned) {
        for (unsigned int sourceBoneIndex = 0;
             sourceBoneIndex < mesh->mNumBones;
             ++sourceBoneIndex) {
            const aiBone* bone = mesh->mBones[sourceBoneIndex];
            if (bone == nullptr) {
                continue;
            }
            const auto boneIt = boneIndices.find(bone->mName.C_Str());
            if (boneIt == boneIndices.end()) {
                continue;
            }
            for (unsigned int weightIndex = 0;
                 weightIndex < bone->mNumWeights;
                 ++weightIndex) {
                const aiVertexWeight& weight = bone->mWeights[weightIndex];
                if (weight.mVertexId >= influences.size() || weight.mWeight <= 0.0f) {
                    continue;
                }
                VertexInfluences& vertexInfluences = influences[weight.mVertexId];
                if (vertexInfluences.count < vertexInfluences.indices.size()) {
                    vertexInfluences.indices[vertexInfluences.count] = boneIt->second;
                    vertexInfluences.weights[vertexInfluences.count] = weight.mWeight;
                    ++vertexInfluences.count;
                } else {
                    const auto minimumIt = std::min_element(
                        vertexInfluences.weights.begin(),
                        vertexInfluences.weights.end()
                    );
                    if (*minimumIt >= weight.mWeight) {
                        continue;
                    }
                    const std::size_t minimumIndex = static_cast<std::size_t>(
                        minimumIt - vertexInfluences.weights.begin()
                    );
                    vertexInfluences.indices[minimumIndex] = boneIt->second;
                    vertexInfluences.weights[minimumIndex] = weight.mWeight;
                }
            }
        }
    }

    meshData.vertices.reserve(meshData.vertices.size() + mesh->mNumVertices);
    meshData.bindPoseVertices.reserve(meshData.bindPoseVertices.size() + mesh->mNumVertices);
    meshData.skinSourceVertices.reserve(meshData.skinSourceVertices.size() + mesh->mNumVertices);
    for (unsigned int vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex) {
        const aiVector3D sourcePosition = mesh->mVertices[vertexIndex];
        const aiVector3D transformedPosition = transform * sourcePosition;
        const aiVector3D localNormal = mesh->HasNormals()
            ? mesh->mNormals[vertexIndex]
            : aiVector3D(0.0f, 0.0f, 1.0f);
        const aiVector3D transformedNormal = normalTransform * localNormal;
        const aiVector3D texCoord = mesh->HasTextureCoords(0)
            ? mesh->mTextureCoords[0][vertexIndex]
            : aiVector3D(0.0f, 0.0f, 0.0f);
        const aiColor4D color = mesh->HasVertexColors(0)
            ? mesh->mColors[0][vertexIndex]
            : aiColor4D(1.0f, 1.0f, 1.0f, 1.0f);

        VulkCore::Vertex vertex{
            {transformedPosition.x, transformedPosition.y, transformedPosition.z},
            {transformedNormal.x, transformedNormal.y, transformedNormal.z},
            {texCoord.x, texCoord.y},
            {color.r, color.g, color.b, color.a}
        };
        if (isSkinned) {
            const VertexInfluences& vertexInfluences = influences[vertexIndex];
            float totalWeight = 0.0f;
            for (std::size_t influenceIndex = 0;
                 influenceIndex < vertexInfluences.count;
                 ++influenceIndex) {
                vertex.boneIndices[static_cast<int>(influenceIndex)] = static_cast<int>(
                    vertexInfluences.indices[influenceIndex]
                );
                vertex.boneWeights[static_cast<int>(influenceIndex)] =
                    vertexInfluences.weights[influenceIndex];
                totalWeight += vertexInfluences.weights[influenceIndex];
            }
            if (totalWeight > std::numeric_limits<float>::epsilon()) {
                vertex.boneWeights /= totalWeight;
            }
        }
        meshData.vertices.push_back(vertex);
        meshData.bindPoseVertices.push_back(vertex);
        VulkCore::Vertex skinSourceVertex = vertex;
        if (isSkinned) {
            skinSourceVertex.position = {
                sourcePosition.x,
                sourcePosition.y,
                sourcePosition.z
            };
            skinSourceVertex.normal = {
                localNormal.x,
                localNormal.y,
                localNormal.z
            };
        }
        meshData.skinSourceVertices.push_back(skinSourceVertex);
    }

    for (unsigned int faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex) {
        const aiFace& face = mesh->mFaces[faceIndex];
        if (face.mNumIndices < 3) {
            continue;
        }

        // Triangulate normally makes every face a triangle. Keeping a fan
        // fallback makes the loader robust if an importer emits a polygon.
        for (unsigned int index = 1; index + 1 < face.mNumIndices; ++index) {
            meshData.indices.push_back(vertexOffset + face.mIndices[0]);
            meshData.indices.push_back(vertexOffset + face.mIndices[index]);
            meshData.indices.push_back(vertexOffset + face.mIndices[index + 1]);
        }
    }

    const std::size_t indexCount = meshData.indices.size() - firstIndex;
    if (indexCount == 0) {
        meshData.vertices.resize(initialVertexCount);
        meshData.bindPoseVertices.resize(initialVertexCount);
        meshData.skinSourceVertices.resize(initialVertexCount);
        return std::nullopt;
    }
    if (indexCount > std::numeric_limits<std::uint32_t>::max()) {
        throw std::overflow_error("mesh contains more indices than a 32-bit index buffer supports");
    }

    const std::uint32_t partIndex = static_cast<std::uint32_t>(meshData.parts.size());
    meshData.parts.push_back({
        firstIndex,
        static_cast<std::uint32_t>(indexCount),
        materialIndex,
        nodeId
    });
    return partIndex;
}

glm::mat4 toGlmMatrix(const aiMatrix4x4& matrix) {
    // Assimp stores matrices in row-major members while GLM's constructor
    // takes column-major columns.
    return glm::mat4(
        matrix.a1, matrix.b1, matrix.c1, matrix.d1,
        matrix.a2, matrix.b2, matrix.c2, matrix.d2,
        matrix.a3, matrix.b3, matrix.c3, matrix.d3,
        matrix.a4, matrix.b4, matrix.c4, matrix.d4
    );
}

void expandBounds(VulkCore::ModelNode& node, const glm::vec3& position) {
    if (!node.hasBounds) {
        node.boundsMinimum = position;
        node.boundsMaximum = position;
        node.hasBounds = true;
        return;
    }

    node.boundsMinimum = glm::min(node.boundsMinimum, position);
    node.boundsMaximum = glm::max(node.boundsMaximum, position);
}

void mergeBounds(VulkCore::ModelNode& node, const VulkCore::ModelNode& child) {
    if (!child.hasBounds) {
        return;
    }
    expandBounds(node, child.boundsMinimum);
    expandBounds(node, child.boundsMaximum);
}

VulkCore::ModelNode readModelNode(
    const aiScene& scene,
    const aiNode* node,
    const aiMatrix4x4& parentTransform,
    std::uint32_t& nextNodeId,
    VulkCore::MeshData& meshData,
    const std::unordered_map<std::string, std::uint32_t>& boneIndices
) {
    VulkCore::ModelNode modelNode;
    if (node == nullptr) {
        return modelNode;
    }

    modelNode.name = node->mName.length > 0 ? node->mName.C_Str() : "<unnamed>";
    modelNode.nodeId = nextNodeId++;
    modelNode.meshCount = node->mNumMeshes;
    modelNode.meshPartIndices.reserve(node->mNumMeshes);
    modelNode.children.reserve(node->mNumChildren);

    const aiMatrix4x4 globalTransform = parentTransform * node->mTransformation;
    modelNode.localTransform = toGlmMatrix(node->mTransformation);
    modelNode.globalTransform = toGlmMatrix(globalTransform);
    for (unsigned int meshIndex = 0; meshIndex < node->mNumMeshes; ++meshIndex) {
        if (node->mMeshes == nullptr || node->mMeshes[meshIndex] >= scene.mNumMeshes) {
            continue;
        }

        const std::size_t vertexStart = meshData.vertices.size();
        const std::optional<std::uint32_t> partIndex = appendMeshGeometry(
            scene.mMeshes[node->mMeshes[meshIndex]],
            globalTransform,
            modelNode.nodeId,
            meshData,
            boneIndices
        );
        if (partIndex.has_value()) {
            modelNode.meshPartIndices.push_back(*partIndex);
            for (std::size_t vertexIndex = vertexStart;
                 vertexIndex < meshData.vertices.size();
                 ++vertexIndex) {
                expandBounds(modelNode, meshData.vertices[vertexIndex].position);
            }
        }
    }

    for (unsigned int childIndex = 0; childIndex < node->mNumChildren; ++childIndex) {
        VulkCore::ModelNode child = readModelNode(
            scene,
            node->mChildren[childIndex],
            globalTransform,
            nextNodeId,
            meshData,
            boneIndices
        );
        mergeBounds(modelNode, child);
        modelNode.children.push_back(std::move(child));
    }
    return modelNode;
}

VulkCore::MaterialData readMaterial(
    const aiScene& scene,
    const aiMaterial* material,
    const std::filesystem::path& modelPath,
    unsigned int materialIndex
) {
    VulkCore::MaterialData materialData;
    if (material == nullptr) {
        materialData.name = "unnamed";
        materialData.texturePath = defaultDiffuseTexturePath(modelPath, "unnamed");
        materialData.normalTexturePath = defaultMaterialTexturePath(modelPath, "unnamed", "normal");
        materialData.specularTexturePath = defaultMaterialTexturePath(modelPath, "unnamed", "specular");
        std::cerr << "[VulkCore][FBX] material[" << materialIndex
                  << "] has no material data; default texture='"
                  << materialData.texturePath.string() << "'\n";
        return materialData;
    }

    aiString materialName;
    material->Get(AI_MATKEY_NAME, materialName);
    const std::string materialNameString =
        materialName.length > 0 ? materialName.C_Str() : "unnamed";
    materialData.name = materialNameString;
    const std::string textureMaterialName = stripNumericSuffix(materialNameString);
    std::cerr << "[VulkCore][FBX] material[" << materialIndex << "] '"
              << materialNameString << "'\n";

    aiColor4D color(1.0f, 1.0f, 1.0f, 1.0f);
    if (material->Get(AI_MATKEY_BASE_COLOR, color) != AI_SUCCESS &&
        material->Get(AI_MATKEY_COLOR_DIFFUSE, color) != AI_SUCCESS) {
        color = aiColor4D(1.0f, 1.0f, 1.0f, 1.0f);
    }
    materialData.baseColor = {color.r, color.g, color.b, color.a};

    aiString texturePath;
    bool hasBaseTextureReference = false;
    bool hasEmbeddedBaseTexture = false;
    bool hasEmbeddedNormalTexture = false;
    bool hasEmbeddedSpecularTexture = false;
    std::filesystem::path externalDiffuseTexturePath;
    for (int textureTypeValue = aiTextureType_DIFFUSE;
         textureTypeValue <= aiTextureType_UNKNOWN;
         ++textureTypeValue) {
        const aiTextureType textureType = static_cast<aiTextureType>(textureTypeValue);
        const unsigned int textureCount = material->GetTextureCount(textureType);
        for (unsigned int textureIndex = 0; textureIndex < textureCount; ++textureIndex) {
            if (material->GetTexture(textureType, textureIndex, &texturePath) != AI_SUCCESS) {
                continue;
            }

            const bool isBaseTexture =
                textureType == aiTextureType_BASE_COLOR || textureType == aiTextureType_DIFFUSE;
            const bool isNormalTexture =
                textureType == aiTextureType_NORMALS || textureType == aiTextureType_HEIGHT;
            const bool isSpecularTexture = textureType == aiTextureType_SPECULAR;
            hasBaseTextureReference = hasBaseTextureReference || isBaseTexture;
            const aiTexture* embeddedTexture = scene.GetEmbeddedTexture(texturePath.C_Str());
            const std::filesystem::path resolvedPath = resolveTexturePath(modelPath, texturePath);
            if (isBaseTexture && externalDiffuseTexturePath.empty() && !resolvedPath.empty()) {
                externalDiffuseTexturePath = resolvedPath;
            }

            std::cerr << "[VulkCore][FBX]   "
                      << aiTextureTypeToString(textureType) << "[" << textureIndex << "] "
                      << "reference='" << texturePath.C_Str() << "'";
            if (embeddedTexture != nullptr) {
                std::cerr << " -> <embedded "
                          << (embeddedTexture->mHeight == 0 ? "compressed" : "raw")
                          << " texture>";
            } else if (!resolvedPath.empty()) {
                std::cerr << " -> '" << resolvedPath.string() << "'";
            } else {
                std::cerr << " -> <file not found>";
            }
            std::cerr << '\n';

            if (isBaseTexture && !hasEmbeddedBaseTexture && embeddedTexture != nullptr) {
                materialData.embeddedTexture = copyEmbeddedTexture(scene, texturePath);
                hasEmbeddedBaseTexture = !materialData.embeddedTexture.empty();
            } else if (isNormalTexture && !hasEmbeddedNormalTexture && embeddedTexture != nullptr) {
                materialData.embeddedNormalTexture = copyEmbeddedTexture(scene, texturePath);
                hasEmbeddedNormalTexture = !materialData.embeddedNormalTexture.empty();
            } else if (isSpecularTexture && !hasEmbeddedSpecularTexture && embeddedTexture != nullptr) {
                materialData.embeddedSpecularTexture = copyEmbeddedTexture(scene, texturePath);
                hasEmbeddedSpecularTexture = !materialData.embeddedSpecularTexture.empty();
            } else if (isNormalTexture && materialData.normalTexturePath.empty() && !resolvedPath.empty()) {
                materialData.normalTexturePath = resolvedPath;
            } else if (isSpecularTexture && materialData.specularTexturePath.empty() && !resolvedPath.empty()) {
                materialData.specularTexturePath = resolvedPath;
            }
        }
    }

    if (!hasEmbeddedBaseTexture) {
        materialData.texturePath = defaultDiffuseTexturePath(modelPath, textureMaterialName);
        std::cerr << "[VulkCore][FBX]   "
                  << (hasBaseTextureReference
                          ? "no embedded Base Color/Diffuse texture"
                          : "no Base Color/Diffuse texture")
                  << "; material key='" << textureMaterialName
                  << "'; default texture='" << materialData.texturePath.string() << "'\n";
    }

    if (!hasEmbeddedNormalTexture && materialData.normalTexturePath.empty()) {
        const std::filesystem::path diffusePath = externalDiffuseTexturePath.empty()
            ? defaultDiffuseTexturePath(modelPath, textureMaterialName)
            : externalDiffuseTexturePath;
        materialData.normalTexturePath = replaceDiffuseTextureName(diffusePath, "normal");
    }
    if (!hasEmbeddedSpecularTexture && materialData.specularTexturePath.empty()) {
        const std::filesystem::path diffusePath = externalDiffuseTexturePath.empty()
            ? defaultDiffuseTexturePath(modelPath, textureMaterialName)
            : externalDiffuseTexturePath;
        materialData.specularTexturePath = replaceDiffuseTextureName(diffusePath, "specular");
    }

    return materialData;
}

VulkCore::AnimationBehavior readAnimationBehavior(aiAnimBehaviour behavior) {
    switch (behavior) {
    case aiAnimBehaviour_DEFAULT:
        return VulkCore::AnimationBehavior::Default;
    case aiAnimBehaviour_CONSTANT:
        return VulkCore::AnimationBehavior::Constant;
    case aiAnimBehaviour_LINEAR:
        return VulkCore::AnimationBehavior::Linear;
    case aiAnimBehaviour_REPEAT:
        return VulkCore::AnimationBehavior::Repeat;
    case _aiAnimBehaviour_Force32Bit:
        break;
    }
    return VulkCore::AnimationBehavior::Default;
}

std::size_t repairIsolatedRotationKeys(
    std::vector<VulkCore::AnimationRotationKey>& keys
) {
    if (keys.size() < 3) {
        return 0;
    }

    const auto normalized = [](const VulkCore::AnimationRotationKey& key) {
        glm::quat rotation(key.value.w, key.value.x, key.value.y, key.value.z);
        const float length = glm::length(rotation);
        if (!std::isfinite(length) || length <= std::numeric_limits<float>::epsilon()) {
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }
        return rotation / length;
    };
    const auto store = [](VulkCore::AnimationRotationKey& key, const glm::quat& rotation) {
        const glm::quat normalizedRotation = glm::normalize(rotation);
        key.value = {
            normalizedRotation.x,
            normalizedRotation.y,
            normalizedRotation.z,
            normalizedRotation.w
        };
    };

    // Work from an immutable snapshot. Several adjacent sub-frame keys can
    // be emitted around one FBX Euler-rotation discontinuity; repairing them
    // in place would make the first repair become the input of the next one.
    const std::vector<VulkCore::AnimationRotationKey> sourceKeys = keys;
    std::vector<glm::quat> sourceRotations;
    sourceRotations.reserve(sourceKeys.size());
    for (const auto& key : sourceKeys) {
        glm::quat rotation = normalized(key);
        if (!sourceRotations.empty() && glm::dot(sourceRotations.back(), rotation) < 0.0f) {
            rotation = -rotation;
        }
        sourceRotations.push_back(rotation);
    }

    // Most FBX tracks are sampled on a regular cadence, but Assimp can also
    // expose an extra sub-frame key at an Euler wrap. Estimate that cadence
    // from the median spacing and only inspect dense, off-grid keys. This
    // keeps legitimate non-uniform animation keys intact.
    std::vector<double> spacings;
    spacings.reserve(sourceKeys.size() - 1);
    for (std::size_t keyIndex = 1; keyIndex < sourceKeys.size(); ++keyIndex) {
        const double spacing = sourceKeys[keyIndex].timeTicks - sourceKeys[keyIndex - 1].timeTicks;
        if (spacing > std::numeric_limits<double>::epsilon()) {
            spacings.push_back(spacing);
        }
    }
    if (spacings.empty()) {
        return 0;
    }
    std::sort(spacings.begin(), spacings.end());
    const double regularSpacing = spacings[spacings.size() / 2];
    if (regularSpacing <= std::numeric_limits<double>::epsilon()) {
        return 0;
    }

    std::vector<bool> removeKey(sourceKeys.size(), false);
    std::size_t repairedCount = 0;
    for (std::size_t keyIndex = 1; keyIndex + 1 < sourceKeys.size(); ++keyIndex) {
        const double gridPosition =
            (sourceKeys[keyIndex].timeTicks - sourceKeys.front().timeTicks) / regularSpacing;
        const double gridOffset = std::abs(gridPosition - std::round(gridPosition));
        const double localSpan =
            sourceKeys[keyIndex + 1].timeTicks - sourceKeys[keyIndex - 1].timeTicks;
        if (gridOffset <= 1.0e-3 || localSpan > regularSpacing * 1.5) {
            continue;
        }

        glm::quat next = sourceRotations[keyIndex + 1];
        if (glm::dot(sourceRotations[keyIndex - 1], next) < 0.0f) {
            next = -next;
        }
        const double span = sourceKeys[keyIndex + 1].timeTicks - sourceKeys[keyIndex - 1].timeTicks;
        const float factor = span > std::numeric_limits<double>::epsilon()
            ? static_cast<float>(
                (sourceKeys[keyIndex].timeTicks - sourceKeys[keyIndex - 1].timeTicks) / span
            )
            : 0.5f;
        const glm::quat predicted = glm::slerp(
            sourceRotations[keyIndex - 1],
            next,
            std::clamp(factor, 0.0f, 1.0f)
        );
        if (glm::dot(sourceRotations[keyIndex], predicted) >= 0.99f) {
            continue;
        }
        removeKey[keyIndex] = true;
        ++repairedCount;
    }

    if (repairedCount == 0) {
        for (std::size_t keyIndex = 0; keyIndex < keys.size(); ++keyIndex) {
            store(keys[keyIndex], sourceRotations[keyIndex]);
        }
        return 0;
    }

    std::vector<VulkCore::AnimationRotationKey> repairedKeys;
    repairedKeys.reserve(sourceKeys.size() - repairedCount);
    for (std::size_t keyIndex = 0; keyIndex < sourceKeys.size(); ++keyIndex) {
        if (!removeKey[keyIndex]) {
            repairedKeys.push_back(sourceKeys[keyIndex]);
            store(repairedKeys.back(), sourceRotations[keyIndex]);
        }
    }
    keys = std::move(repairedKeys);
    return repairedCount;
}

void collectModelNodeMetadata(
    const VulkCore::ModelNode& node,
    std::unordered_map<std::string, std::uint32_t>& nodeIds,
    std::unordered_map<std::uint32_t, std::uint32_t>& parentNodeIds,
    std::unordered_map<std::uint32_t, glm::mat4>& globalTransforms
) {
    if (!node.name.empty()) {
        nodeIds.emplace(node.name, node.nodeId);
    }
    globalTransforms.emplace(node.nodeId, node.globalTransform);
    for (const VulkCore::ModelNode& child : node.children) {
        parentNodeIds[child.nodeId] = node.nodeId;
        collectModelNodeMetadata(child, nodeIds, parentNodeIds, globalTransforms);
    }
}

void readSkeleton(
    const aiScene& scene,
    VulkCore::MeshData& meshData,
    std::unordered_map<std::string, std::uint32_t>& boneIndices
) {
    boneIndices.reserve(scene.mNumMeshes);

    for (unsigned int meshIndex = 0; meshIndex < scene.mNumMeshes; ++meshIndex) {
        const aiMesh* mesh = scene.mMeshes[meshIndex];
        if (mesh == nullptr || mesh->mNumBones == 0 || mesh->mBones == nullptr) {
            continue;
        }

        ++meshData.skinnedMeshCount;
        for (unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {
            const aiBone* bone = mesh->mBones[boneIndex];
            if (bone == nullptr) {
                continue;
            }

            const std::string boneName = bone->mName.length > 0
                ? bone->mName.C_Str()
                : "<unnamed bone>";
            auto [boneIt, inserted] = boneIndices.emplace(
                boneName,
                static_cast<std::uint32_t>(meshData.skeletonBones.size())
            );
            if (inserted) {
                VulkCore::SkeletonBone skeletonBone;
                skeletonBone.name = boneName;
                skeletonBone.offsetMatrix = toGlmMatrix(bone->mOffsetMatrix);
                meshData.skeletonBones.push_back(std::move(skeletonBone));
            }

            VulkCore::SkeletonBone& skeletonBone =
                meshData.skeletonBones[boneIt->second];
            skeletonBone.influenceCount += bone->mNumWeights;
            meshData.skinWeightCount += bone->mNumWeights;
        }
    }

}

void resolveSkeleton(
    VulkCore::MeshData& meshData
) {
    if (meshData.skeletonBones.empty()) {
        return;
    }
    std::unordered_map<std::string, std::uint32_t> nodeIds;
    std::unordered_map<std::uint32_t, std::uint32_t> parentNodeIds;
    std::unordered_map<std::uint32_t, glm::mat4> globalTransforms;
    collectModelNodeMetadata(meshData.hierarchy, nodeIds, parentNodeIds, globalTransforms);

    std::unordered_map<std::uint32_t, std::uint32_t> nodeBoneIndices;
    for (std::size_t boneIndex = 0;
         boneIndex < meshData.skeletonBones.size();
         ++boneIndex) {
        VulkCore::SkeletonBone& bone = meshData.skeletonBones[boneIndex];
        const auto nodeIt = nodeIds.find(bone.name);
        if (nodeIt == nodeIds.end()) {
            continue;
        }

        bone.nodeId = nodeIt->second;
        const auto globalTransformIt = globalTransforms.find(bone.nodeId);
        if (globalTransformIt != globalTransforms.end()) {
            bone.bindPoseGlobalTransform = globalTransformIt->second;
        }
        nodeBoneIndices.emplace(bone.nodeId, static_cast<std::uint32_t>(boneIndex));
    }

    for (VulkCore::SkeletonBone& bone : meshData.skeletonBones) {
        if (bone.nodeId == std::numeric_limits<std::uint32_t>::max()) {
            continue;
        }
        std::uint32_t currentNodeId = bone.nodeId;
        while (true) {
            const auto parentIt = parentNodeIds.find(currentNodeId);
            if (parentIt == parentNodeIds.end()) {
                break;
            }
            currentNodeId = parentIt->second;
            const auto parentBoneIt = nodeBoneIndices.find(currentNodeId);
            if (parentBoneIt != nodeBoneIndices.end()) {
                bone.parentIndex = static_cast<std::int32_t>(parentBoneIt->second);
                break;
            }
        }
    }

    std::cerr << "[VulkCore][FBX] skeleton has "
              << meshData.skeletonBones.size() << " bone(s), "
              << meshData.skinnedMeshCount << " skinned mesh(es), and "
              << meshData.skinWeightCount << " weight(s)\n";
}

void readSkeletalAnimations(
    const aiScene& scene,
    const VulkCore::MeshData& meshData,
    std::vector<VulkCore::SkeletalAnimationClip>& animations
) {
    if (scene.mNumAnimations == 0 || scene.mAnimations == nullptr) {
        return;
    }

    std::unordered_map<std::string, std::int32_t> boneIndices;
    boneIndices.reserve(meshData.skeletonBones.size());
    for (std::size_t boneIndex = 0;
         boneIndex < meshData.skeletonBones.size();
         ++boneIndex) {
        boneIndices.emplace(
            meshData.skeletonBones[boneIndex].name,
            static_cast<std::int32_t>(boneIndex)
        );
    }
    std::unordered_map<std::string, std::uint32_t> nodeIds;
    std::unordered_map<std::uint32_t, std::uint32_t> ignoredParentNodeIds;
    std::unordered_map<std::uint32_t, glm::mat4> ignoredGlobalTransforms;
    collectModelNodeMetadata(
        meshData.hierarchy,
        nodeIds,
        ignoredParentNodeIds,
        ignoredGlobalTransforms
    );

    animations.reserve(scene.mNumAnimations);
    for (unsigned int animationIndex = 0;
         animationIndex < scene.mNumAnimations;
         ++animationIndex) {
        const aiAnimation* animation = scene.mAnimations[animationIndex];
        if (animation == nullptr) {
            continue;
        }

        VulkCore::SkeletalAnimationClip clip;
        clip.name = animation->mName.length > 0
            ? animation->mName.C_Str()
            : "Animation " + std::to_string(animationIndex);
        clip.durationTicks = animation->mDuration;
        clip.ticksPerSecond = animation->mTicksPerSecond;
        clip.meshChannelCount = animation->mNumMeshChannels;
        clip.channels.reserve(animation->mNumChannels);
        std::size_t repairedRotationKeyCount = 0;

        for (unsigned int channelIndex = 0;
             channelIndex < animation->mNumChannels;
             ++channelIndex) {
            if (animation->mChannels == nullptr) {
                break;
            }
            const aiNodeAnim* channel = animation->mChannels[channelIndex];
            if (channel == nullptr) {
                continue;
            }

            VulkCore::SkeletalAnimationChannel animationChannel;
            animationChannel.nodeName = channel->mNodeName.length > 0
                ? channel->mNodeName.C_Str()
                : "<unnamed node>";
            const auto nodeIt = nodeIds.find(animationChannel.nodeName);
            if (nodeIt != nodeIds.end()) {
                animationChannel.nodeId = nodeIt->second;
            }
            const auto boneIt = boneIndices.find(animationChannel.nodeName);
            if (boneIt != boneIndices.end()) {
                animationChannel.boneIndex = boneIt->second;
            }
            animationChannel.preBehavior = readAnimationBehavior(channel->mPreState);
            animationChannel.postBehavior = readAnimationBehavior(channel->mPostState);

            animationChannel.positionKeys.reserve(channel->mNumPositionKeys);
            for (unsigned int keyIndex = 0;
                 keyIndex < channel->mNumPositionKeys;
                 ++keyIndex) {
                const aiVectorKey& key = channel->mPositionKeys[keyIndex];
                animationChannel.positionKeys.push_back({
                    key.mTime,
                    {key.mValue.x, key.mValue.y, key.mValue.z}
                });
            }

            animationChannel.rotationKeys.reserve(channel->mNumRotationKeys);
            for (unsigned int keyIndex = 0;
                 keyIndex < channel->mNumRotationKeys;
                 ++keyIndex) {
                const aiQuatKey& key = channel->mRotationKeys[keyIndex];
                animationChannel.rotationKeys.push_back({
                    key.mTime,
                    {key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w}
                });
            }

            animationChannel.scalingKeys.reserve(channel->mNumScalingKeys);
            for (unsigned int keyIndex = 0;
                 keyIndex < channel->mNumScalingKeys;
                 ++keyIndex) {
                const aiVectorKey& key = channel->mScalingKeys[keyIndex];
                animationChannel.scalingKeys.push_back({
                    key.mTime,
                    {key.mValue.x, key.mValue.y, key.mValue.z}
                });
            }
            repairedRotationKeyCount += repairIsolatedRotationKeys(
                animationChannel.rotationKeys
            );
            clip.channels.push_back(std::move(animationChannel));
        }
        if (repairedRotationKeyCount > 0) {
            std::cerr << "[VulkCore][FBX] repaired " << repairedRotationKeyCount
                      << " isolated rotation key(s) in animation '" << clip.name
                      << "' to prevent skeletal transform spikes\n";
        }
        animations.push_back(std::move(clip));
    }
}

} // namespace

namespace VulkCore {

MeshData loadModelMesh(const std::filesystem::path& path) {
    if (path.empty()) {
        throw std::invalid_argument("model path must not be empty!");
    }
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("model file does not exist: " + path.string());
    }

    Assimp::Importer importer;
    importer.SetPropertyInteger(AI_CONFIG_IMPORT_FBX_READ_ALL_GEOMETRY_LAYERS, 1);

    constexpr unsigned int importFlags =
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices |
        aiProcess_GenSmoothNormals |
        aiProcess_ImproveCacheLocality |
        aiProcess_FlipUVs;

    const aiScene* scene = importer.ReadFile(path.string(), importFlags);
    if (scene == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 || scene->mRootNode == nullptr) {
        const std::string error = importer.GetErrorString();
        throw std::runtime_error(
            "failed to load model '" + path.string() + "': " +
            (error.empty() ? "unknown Assimp error" : error)
        );
    }

    std::cerr << "[VulkCore][FBX] loaded '" << path.string() << "' with "
              << scene->mNumMaterials << " material(s) and " << scene->mNumTextures
              << " embedded texture(s)\n";

    MeshData meshData;

    meshData.materials.reserve(scene->mNumMaterials);
    for (unsigned int materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex) {
        meshData.materials.push_back(
            readMaterial(*scene, scene->mMaterials[materialIndex], path, materialIndex)
        );
    }
    if (meshData.materials.empty()) {
        meshData.materials.emplace_back();
    }

    std::unordered_map<std::string, std::uint32_t> boneIndices;
    aiMatrix4x4 identityTransform;
    std::uint32_t nextNodeId = 0;
    readSkeleton(*scene, meshData, boneIndices);
    meshData.hierarchy = readModelNode(
        *scene,
        scene->mRootNode,
        identityTransform,
        nextNodeId,
        meshData,
        boneIndices
    );
    resolveSkeleton(meshData);
    readSkeletalAnimations(*scene, meshData, meshData.skeletalAnimations);
    meshData.bindPoseVertices = meshData.vertices;
    meshData.skeletalAnimationInstances.clear();
    if (!meshData.skeletonBones.empty() && !meshData.skeletalAnimations.empty()) {
        VulkCore::SkeletalAnimationInstance instance;
        instance.rootNodeId = meshData.hierarchy.nodeId;
        instance.boneCount = static_cast<std::uint32_t>(meshData.skeletonBones.size());
        instance.animationCount = static_cast<std::uint32_t>(meshData.skeletalAnimations.size());
        instance.globalInverseTransform = glm::inverse(meshData.hierarchy.globalTransform);
        meshData.skeletalAnimationInstances.push_back(instance);
    }

    std::cerr << "[VulkCore][FBX] parsed "
              << meshData.skeletalAnimations.size() << " skeletal animation clip(s)\n";

    if (meshData.vertices.empty() || meshData.indices.empty() || meshData.parts.empty()) {
        throw std::runtime_error("model contains no renderable triangle mesh: " + path.string());
    }

    return meshData;
}

} // namespace VulkCore
