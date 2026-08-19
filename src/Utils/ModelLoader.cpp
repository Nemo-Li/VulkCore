#include "vulkcore/Utils/ModelLoader.hpp"

#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <limits>
#include <stdexcept>
#include <string>

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

    MeshData meshData;
    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        const aiMesh* mesh = scene->mMeshes[meshIndex];
        if (mesh == nullptr || mesh->mNumVertices == 0) {
            continue;
        }

        if (meshData.vertices.size() > std::numeric_limits<uint32_t>::max() - mesh->mNumVertices) {
            throw std::overflow_error("model contains more vertices than a 32-bit index buffer supports");
        }

        const uint32_t vertexOffset = static_cast<uint32_t>(meshData.vertices.size());
        meshData.vertices.reserve(meshData.vertices.size() + mesh->mNumVertices);
        for (unsigned int vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex) {
            const aiVector3D& position = mesh->mVertices[vertexIndex];
            const aiVector3D normal = mesh->HasNormals()
                ? mesh->mNormals[vertexIndex]
                : aiVector3D(0.0f, 0.0f, 1.0f);
            const aiVector3D texCoord = mesh->HasTextureCoords(0)
                ? mesh->mTextureCoords[0][vertexIndex]
                : aiVector3D(0.0f, 0.0f, 0.0f);

            meshData.vertices.push_back({
                {position.x, position.y, position.z},
                {normal.x, normal.y, normal.z},
                {texCoord.x, texCoord.y}
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
    }

    if (meshData.vertices.empty() || meshData.indices.empty()) {
        throw std::runtime_error("model contains no renderable triangle mesh: " + path.string());
    }

    return meshData;
}

} // namespace VulkCore
