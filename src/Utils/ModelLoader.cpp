#include "vulkcore/Utils/ModelLoader.hpp"

#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

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

VulkCore::MaterialData readMaterial(
    const aiScene& scene,
    const aiMaterial* material,
    const std::filesystem::path& modelPath,
    unsigned int materialIndex
) {
    VulkCore::MaterialData materialData;
    if (material == nullptr) {
        materialData.texturePath = defaultDiffuseTexturePath(modelPath, "unnamed");
        std::cerr << "[VulkCore][FBX] material[" << materialIndex
                  << "] has no material data; default texture='"
                  << materialData.texturePath.string() << "'\n";
        return materialData;
    }

    aiString materialName;
    material->Get(AI_MATKEY_NAME, materialName);
    const std::string materialNameString =
        materialName.length > 0 ? materialName.C_Str() : "unnamed";
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
            hasBaseTextureReference = hasBaseTextureReference || isBaseTexture;
            const aiTexture* embeddedTexture = scene.GetEmbeddedTexture(texturePath.C_Str());
            const std::filesystem::path resolvedPath = resolveTexturePath(modelPath, texturePath);

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

            if (hasEmbeddedBaseTexture || !isBaseTexture) {
                continue;
            }

            if (embeddedTexture != nullptr) {
                materialData.embeddedTexture = copyEmbeddedTexture(scene, texturePath);
                hasEmbeddedBaseTexture = !materialData.embeddedTexture.empty();
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

    return materialData;
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
        aiProcess_PreTransformVertices |
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

    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        const aiMesh* mesh = scene->mMeshes[meshIndex];
        if (mesh == nullptr || mesh->mNumVertices == 0) {
            continue;
        }

        if (meshData.vertices.size() > std::numeric_limits<uint32_t>::max() - mesh->mNumVertices) {
            throw std::overflow_error("model contains more vertices than a 32-bit index buffer supports");
        }

        const uint32_t vertexOffset = static_cast<uint32_t>(meshData.vertices.size());
        const uint32_t firstIndex = static_cast<uint32_t>(meshData.indices.size());
        const std::uint32_t materialIndex =
            mesh->mMaterialIndex < meshData.materials.size() ? mesh->mMaterialIndex : 0;
        meshData.vertices.reserve(meshData.vertices.size() + mesh->mNumVertices);
        for (unsigned int vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex) {
            const aiVector3D& position = mesh->mVertices[vertexIndex];
            const aiVector3D normal = mesh->HasNormals()
                ? mesh->mNormals[vertexIndex]
                : aiVector3D(0.0f, 0.0f, 1.0f);
            const aiVector3D texCoord = mesh->HasTextureCoords(0)
                ? mesh->mTextureCoords[0][vertexIndex]
                : aiVector3D(0.0f, 0.0f, 0.0f);
            const aiColor4D color = mesh->HasVertexColors(0)
                ? mesh->mColors[0][vertexIndex]
                : aiColor4D(1.0f, 1.0f, 1.0f, 1.0f);

            meshData.vertices.push_back({
                {position.x, position.y, position.z},
                {normal.x, normal.y, normal.z},
                {texCoord.x, texCoord.y},
                {color.r, color.g, color.b, color.a}
            });
        }

        for (unsigned int faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex) {
            const aiFace& face = mesh->mFaces[faceIndex];
            if (face.mNumIndices < 3) {
                continue;
            }

            // Triangulate normally makes every face a triangle. Keeping a
            // fan fallback makes the loader robust if an importer emits a
            // polygon despite the post-process flag.
            for (unsigned int index = 1; index + 1 < face.mNumIndices; ++index) {
                meshData.indices.push_back(vertexOffset + face.mIndices[0]);
                meshData.indices.push_back(vertexOffset + face.mIndices[index]);
                meshData.indices.push_back(vertexOffset + face.mIndices[index + 1]);
            }
        }

        const uint32_t indexCount = static_cast<uint32_t>(meshData.indices.size()) - firstIndex;
        if (indexCount != 0) {
            meshData.parts.push_back({firstIndex, indexCount, materialIndex});
        }
    }

    if (meshData.vertices.empty() || meshData.indices.empty() || meshData.parts.empty()) {
        throw std::runtime_error("model contains no renderable triangle mesh: " + path.string());
    }

    return meshData;
}

} // namespace VulkCore
