#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "vulkcore/Renderer/Vertex.hpp"

namespace VulkCore {

inline constexpr std::uint32_t kTerrainHeightmapResolution = 256;
inline constexpr std::uint32_t kTerrainWaterResolution = 64;
inline constexpr std::uint32_t kTerrainGrassResolution = 64;
inline constexpr std::uint32_t kTerrainMaterialResolution = 256;
inline constexpr std::uint32_t kTerrainRenderSampleStride = 2;

struct TerrainMaterialInfo {
    std::uint32_t index = 0;
    glm::vec2 textureScale{1.0f};
    // The exact names of these two source fields are unknown. Preserve their
    // values so the generated terrain shader can still use TSCB tuning data.
    float paletteBias = 0.0f;
    float paletteContrast = 1.0f;
};

struct TerrainTileMetadata {
    std::string fileBase;
    glm::vec2 gridPosition{0.0f};
    float areaSize = 0.0f;
    float minimumHeight = 0.0f;
    float maximumHeight = 0.0f;
    float minimumWaterHeight = 0.0f;
    float maximumWaterHeight = 0.0f;
    std::uint32_t lod = 0;
    std::uint32_t areaFlags = 0;
    std::uint32_t materialIndex = 0;
    TerrainMaterialInfo materialInfo;
    int materialTile = -1;
    std::filesystem::path sourcePath;
};

// A single BOTW .hght tile. Repaired, filtered samples stay resident so height
// queries, rendering and collision geometry all use the same terrain surface.
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

    bool containsWorldPoint(const glm::vec2& worldPosition) const;
    std::optional<float> heightAtWorld(
        const glm::vec2& worldPosition,
        float worldScale,
        float maximumHeight
    ) const;
    std::optional<float> renderedHeightAtWorld(
        const glm::vec2& worldPosition,
        float worldScale,
        float maximumHeight,
        std::uint32_t sampleStride
    ) const;

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

    // Use after the height mesh has been consumed by physics. Keeps chunk IDs stable.
    std::size_t selectMaterialTiles(std::span<const int> textureLayers);
};

struct TerrainMaterialSample {
    std::uint8_t firstId;
    std::uint8_t secondId;
    std::uint8_t firstWeight;
    std::uint8_t unknown;
};

struct TerrainMaterialLayers {
    std::vector<std::uint8_t> pixels;
    std::vector<int> layerForTile;
    std::vector<std::uint8_t> missingIds;
    std::uint32_t count = 0;
};

struct WaterMeshOptions {
    // The .water.extm files are native 64x64 flow/height grids. Keeping the
    // source resolution preserves narrow rivers and shoreline boundaries.
    std::uint32_t sampleStride = 1;
};

struct WaterSample {
    std::uint16_t height = 0;
    std::uint16_t flowX = 0x8000;
    std::uint16_t flowZ = 0x8000;
    std::uint8_t material = 0;
};

struct WaterTileData {
    TerrainTileMetadata metadata;
    std::vector<WaterSample> samples;
};

struct GrassSample {
    float height = 0.0f; // Normalized blade height, with zero indicating bare ground.
    glm::vec3 color{0.0f}; // RGB in the map's encoding, normalized to [0, 1].
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
    // The source tiles are 256x256. A stride of two keeps the generated
    // terrain practical while reducing the long, visibly planar mountain
    // faces produced by the previous stride-four mesh.
    std::uint32_t sampleStride = kTerrainRenderSampleStride;
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
        const std::filesystem::path& terrainRoot,
        const std::filesystem::path& waterRoot = {},
        const std::atomic<bool>* cancelled = nullptr
    );

    const TerrainSceneHeader& header() const { return header_; }
    const std::vector<TerrainMaterialInfo>& materialInfos() const { return materialInfos_; }
    const std::vector<TerrainChunk>& chunks() const { return chunks_; }
    const glm::vec3& boundsMinimum() const { return boundsMinimum_; }
    const glm::vec3& boundsMaximum() const { return boundsMaximum_; }
    std::size_t tileCount() const;
    std::size_t waterTileCount() const;
    std::size_t grassTileCount() const { return grassTiles_.size(); }
    std::size_t materialTileCount() const { return materialTiles_.size(); }
    TerrainMaterialLayers buildMaterialLayers(const std::array<int, 256>& albedoLayers) const;
    std::optional<GrassSample> grassAtWorld(const glm::vec2& worldPosition) const;
    std::optional<float> waterHeightAtWorld(const glm::vec2& worldPosition) const;

    std::optional<float> heightAtWorld(const glm::vec2& worldPosition) const;
    std::optional<float> renderedHeightAtWorld(const glm::vec2& worldPosition) const;
    glm::vec3 centerPosition() const;

    TerrainMeshData buildMesh(const TerrainMeshOptions& options = {},
        const std::atomic<bool>* cancelled = nullptr) const;
    TerrainMeshData buildWaterMesh(const WaterMeshOptions& options = {},
        const std::atomic<bool>* cancelled = nullptr) const;

private:
    TerrainScene() = default;

    TerrainSceneHeader header_;
    std::vector<TerrainMaterialInfo> materialInfos_;
    std::vector<TerrainChunk> chunks_;
    std::vector<std::vector<TerrainMaterialSample>> materialTiles_;
    std::vector<WaterTileData> waterTiles_;
    struct GrassTile {
        glm::vec2 minimum;
        glm::vec2 maximum;
        std::uint32_t lod;
        std::vector<std::uint8_t> bytes;
    };
    std::vector<GrassTile> grassTiles_;
    std::map<std::pair<int, int>, std::vector<std::size_t>> grassTilesByCell_;
    std::map<std::pair<int, int>, std::vector<std::size_t>> waterTilesByCell_;
    glm::vec3 boundsMinimum_{0.0f};
    glm::vec3 boundsMaximum_{0.0f};
};

} // namespace VulkCore
