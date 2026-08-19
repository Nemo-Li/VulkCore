#pragma once

#include <cstdint>
#include <filesystem>
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

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<MaterialData> materials;
    std::vector<MeshPart> parts;
};

} // namespace VulkCore
