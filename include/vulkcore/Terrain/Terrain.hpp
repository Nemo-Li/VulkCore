#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "vulkcore/Renderer/Vertex.hpp"

namespace VulkCore {

inline constexpr std::uint32_t kTerrainHeightmapResolution = 256;

struct TerrainTileMetadata {
    std::string fileBase;
    glm::vec2 gridPosition{0.0f};
    float areaSize = 0.0f;
    float minimumHeight = 0.0f;
    float maximumHeight = 0.0f;
    float minimumWaterHeight = 0.0f;
    float maximumWaterHeight = 0.0f;
    std::uint32_t lod = 0;
    std::filesystem::path sourcePath;
};

// A single BOTW .hght file. The source samples stay resident so chunks can
// be rebuilt at a different sampling density without reparsing TSCB.
class TerrainTile {
public:
    TerrainTile(
        TerrainTileMetadata metadata,
        std::vector<std::uint16_t> heights,
        float worldScale,
        float maximumHeight
    );

    const TerrainTileMetadata& metadata() const { return metadata_; }
    const std::vector<std::uint16_t>& heights() const { return heights_; }
    const glm::vec3& boundsMinimum() const { return boundsMinimum_; }
    const glm::vec3& boundsMaximum() const { return boundsMaximum_; }

    // Appends a sampled regular grid to a shared mesh. The stride is applied
    // to the 256x256 source samples and the final edge sample is always kept.
    void appendMesh(
        std::uint32_t sampleStride,
        float worldScale,
        float maximumHeight,
        std::vector<Vertex>& vertices,
        std::vector<std::uint32_t>& indices
    ) const;

private:
    std::uint16_t sample(std::uint32_t x, std::uint32_t z) const;
    float worldHeight(std::uint16_t sampleValue, float maximumHeight) const;
    glm::vec3 position(
        std::uint32_t x,
        std::uint32_t z,
        float worldScale,
        float maximumHeight
    ) const;

    TerrainTileMetadata metadata_;
    std::vector<std::uint16_t> heights_;
    glm::vec3 boundsMinimum_{0.0f};
    glm::vec3 boundsMaximum_{0.0f};
};

struct TerrainChunkMesh {
    std::uint32_t lod = 0;
    glm::ivec2 coordinate{0};
    std::uint32_t tileCount = 0;
    std::size_t indexOffset = 0;
    std::size_t indexCount = 0;
    glm::vec3 boundsMinimum{0.0f};
    glm::vec3 boundsMaximum{0.0f};
};

struct TerrainMeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<TerrainChunkMesh> chunks;
};

class TerrainChunk {
public:
    TerrainChunk(std::uint32_t lod, glm::ivec2 coordinate);

    std::uint32_t lod() const { return lod_; }
    const glm::ivec2& coordinate() const { return coordinate_; }
    const std::vector<TerrainTile>& tiles() const { return tiles_; }
    const glm::vec3& boundsMinimum() const { return boundsMinimum_; }
    const glm::vec3& boundsMaximum() const { return boundsMaximum_; }

    void addTile(TerrainTile tile);
    void appendMesh(
        std::uint32_t sampleStride,
        float worldScale,
        float maximumHeight,
        TerrainMeshData& mesh
    ) const;

private:
    std::uint32_t lod_ = 0;
    glm::ivec2 coordinate_{0};
    std::vector<TerrainTile> tiles_;
    glm::vec3 boundsMinimum_{0.0f};
    glm::vec3 boundsMaximum_{0.0f};
};

struct TerrainMeshOptions {
    // Four keeps the 144 supplied LOD4 tiles at a practical editor size
    // while retaining the source .hght data for future higher detail modes.
    std::uint32_t sampleStride = 4;
};

struct TerrainSceneHeader {
    float worldScale = 0.0f;
    float maximumHeight = 0.0f;
    float tileSize = 0.0f;
    std::uint32_t areaCount = 0;
};

class TerrainScene {
public:
    static std::unique_ptr<TerrainScene> load(
        const std::filesystem::path& tscbPath,
        const std::filesystem::path& terrainRoot
    );

    const TerrainSceneHeader& header() const { return header_; }
    const std::vector<TerrainChunk>& chunks() const { return chunks_; }
    std::size_t tileCount() const;

    TerrainMeshData buildMesh(const TerrainMeshOptions& options = {}) const;

private:
    TerrainScene() = default;

    TerrainSceneHeader header_;
    std::vector<TerrainChunk> chunks_;
};

} // namespace VulkCore
