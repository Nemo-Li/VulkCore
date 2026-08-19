#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "vulkcore/Renderer/Vertex.hpp"

namespace VulkCore {

struct EmbeddedTextureData {
    std::vector<std::uint8_t> data;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool compressed = false;

    bool empty() const {
        return data.empty() || width == 0 || height == 0;
    }
};

struct MaterialData {
    glm::vec4 baseColor{1.0f};
    std::filesystem::path texturePath;
    EmbeddedTextureData embeddedTexture;
};

struct MeshPart {
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t materialIndex = 0;
};

// The node hierarchy is kept separately from the flattened render mesh. This
// lets the renderer draw the pre-transformed mesh while the UI still exposes
// the original model structure.
struct ModelNode {
    std::string name;
    std::uint32_t meshCount = 0;
    // Indices into MeshData::parts for this node's directly attached meshes.
    std::vector<std::uint32_t> meshPartIndices;
    std::vector<ModelNode> children;
    bool hasBounds = false;
    glm::vec3 boundsMinimum{0.0f};
    glm::vec3 boundsMaximum{0.0f};
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<MaterialData> materials;
    std::vector<MeshPart> parts;
    ModelNode hierarchy;
};

} // namespace VulkCore
