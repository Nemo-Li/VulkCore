#include "vulkcore/Terrain/Terrain.hpp"

#include "vulkcore/Utils/FileUtils.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace VulkCore {

namespace {

constexpr std::size_t kTscbHeaderSize = 0x34;
constexpr float kTerrainGridSize = 32.0f;
constexpr float kTerrainWorldExtentMultiplier = 20.0f;

std::uint32_t readU32BE(const std::vector<char>& data, std::size_t offset) {
    if (offset > data.size() || data.size() - offset < sizeof(std::uint32_t)) {
        throw std::runtime_error("TSCB contains a truncated 32-bit value");
    }
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset])) << 24) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset + 1])) << 16) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset + 2])) << 8) |
        static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset + 3]));
}

float readF32BE(const std::vector<char>& data, std::size_t offset) {
    const std::uint32_t bits = readU32BE(data, offset);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void requireRange(
    const std::vector<char>& data,
    std::size_t offset,
    std::size_t length,
    const char* description
) {
    if (offset > data.size() || length > data.size() - offset) {
        throw std::runtime_error(std::string("TSCB contains a truncated ") + description);
    }
}

std::filesystem::path findHeightmapPath(
    const std::filesystem::path& terrainRoot,
    const std::string& fileBase
) {
    std::vector<std::filesystem::path> candidates;
    // Extracted SARC directories are named after the first file in a group
    // of four: 540000003B.hght lives in 5400000038.hght, for example.
    if (fileBase.size() >= 2) {
        const auto hexValue = [](char value) -> int {
            if (value >= '0' && value <= '9') {
                return value - '0';
            }
            if (value >= 'A' && value <= 'F') {
                return value - 'A' + 10;
            }
            if (value >= 'a' && value <= 'f') {
                return value - 'a' + 10;
            }
            return -1;
        };
        const int high = hexValue(fileBase[fileBase.size() - 2]);
        const int low = hexValue(fileBase[fileBase.size() - 1]);
        if (high >= 0 && low >= 0) {
            const int groupedValue = ((high << 4) | low) & ~0x3;
            std::string groupBase = fileBase;
            constexpr char hexDigits[] = "0123456789ABCDEF";
            groupBase[groupBase.size() - 2] = hexDigits[groupedValue >> 4];
            groupBase[groupBase.size() - 1] = hexDigits[groupedValue & 0xF];
            candidates.push_back(
                terrainRoot / "terrain" / (groupBase + ".hght") / (fileBase + ".hght")
            );
        }
    }

    candidates.push_back(terrainRoot / "terrain" / (fileBase + ".hght") / fileBase);
    candidates.push_back(
        terrainRoot / "terrain" / (fileBase + ".hght") / (fileBase + ".hght")
    );
    candidates.push_back(terrainRoot / "terrain" / (fileBase + ".hght"));
    candidates.push_back(terrainRoot / (fileBase + ".hght"));
    for (const auto& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate;
        }
    }
    return {};
}

std::vector<std::uint16_t> loadHeightSamples(const std::filesystem::path& path) {
    const std::vector<char> bytes = readBinaryFile(path);
    constexpr std::size_t expectedBytes =
        static_cast<std::size_t>(kTerrainHeightmapResolution) *
        kTerrainHeightmapResolution * sizeof(std::uint16_t);
    if (bytes.size() != expectedBytes) {
        throw std::runtime_error(
            "heightmap must contain exactly 256x256 little-endian uint16 samples: " +
            path.string()
        );
    }

    std::vector<std::uint16_t> samples(
        static_cast<std::size_t>(kTerrainHeightmapResolution) *
        kTerrainHeightmapResolution
    );
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const std::uint32_t low = static_cast<unsigned char>(bytes[index * 2]);
        const std::uint32_t high = static_cast<unsigned char>(bytes[index * 2 + 1]);
        samples[index] = static_cast<std::uint16_t>(low | (high << 8));
    }
    return samples;
}

std::uint32_t lodFromFileBase(const std::string& fileBase) {
    if (fileBase.size() < 2 || fileBase.front() != '5' ||
        fileBase[1] < '0' || fileBase[1] > '9') {
        return 0;
    }
    return static_cast<std::uint32_t>(fileBase[1] - '0');
}

std::string readRelativeString(
    const std::vector<char>& data,
    std::size_t entryOffset,
    std::uint32_t relativeOffset
) {
    const std::size_t stringOffset = entryOffset + 0x20u + relativeOffset;
    if (stringOffset >= data.size()) {
        throw std::runtime_error("TSCB contains an invalid terrain file name offset");
    }
    const auto end = std::find(
        data.begin() + static_cast<std::ptrdiff_t>(stringOffset),
        data.end(),
        '\0'
    );
    if (end == data.end()) {
        throw std::runtime_error("TSCB terrain file name is not null terminated");
    }
    return std::string(
        data.begin() + static_cast<std::ptrdiff_t>(stringOffset),
        end
    );
}

struct ParsedTile {
    TerrainTileMetadata metadata;
    std::vector<std::uint16_t> heights;
};

struct TerrainVertexKey {
    std::int64_t x = 0;
    std::int64_t z = 0;

    bool operator==(const TerrainVertexKey& other) const {
        return x == other.x && z == other.z;
    }
};

struct TerrainVertexKeyHash {
    std::size_t operator()(const TerrainVertexKey& key) const {
        const std::size_t xHash = std::hash<std::int64_t>{}(key.x);
        const std::size_t zHash = std::hash<std::int64_t>{}(key.z);
        return xHash ^ (zHash + static_cast<std::size_t>(0x9e3779b9) +
            (xHash << 6) + (xHash >> 2));
    }
};

TerrainVertexKey terrainVertexKey(const Vertex& vertex) {
    constexpr double kCoordinateQuantization = 1000.0;
    return {
        static_cast<std::int64_t>(std::llround(
            static_cast<double>(vertex.position.x) * kCoordinateQuantization
        )),
        static_cast<std::int64_t>(std::llround(
            static_cast<double>(vertex.position.z) * kCoordinateQuantization
        ))
    };
}

void stitchTerrainMesh(TerrainMeshData& mesh) {
    std::unordered_map<TerrainVertexKey, std::vector<std::uint32_t>, TerrainVertexKeyHash>
        vertexGroups;
    vertexGroups.reserve(mesh.vertices.size());
    for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
        vertexGroups[terrainVertexKey(mesh.vertices[index])].push_back(
            static_cast<std::uint32_t>(index)
        );
    }

    std::vector<glm::vec3> normalSums(mesh.vertices.size(), glm::vec3(0.0f));

    for (const auto& [key, indices] : vertexGroups) {
        (void)key;
        if (indices.size() < 2) {
            continue;
        }
        float heightSum = 0.0f;
        glm::vec4 colorSum(0.0f);
        for (const std::uint32_t index : indices) {
            heightSum += mesh.vertices[index].position.y;
            colorSum += mesh.vertices[index].color;
        }
        const float averageHeight = heightSum / static_cast<float>(indices.size());
        const glm::vec4 averageColor = colorSum / static_cast<float>(indices.size());
        for (const std::uint32_t index : indices) {
            mesh.vertices[index].position.y = averageHeight;
            mesh.vertices[index].color = averageColor;
        }
    }

    for (std::size_t index = 0; index + 2 < mesh.indices.size(); index += 3) {
        const std::uint32_t first = mesh.indices[index];
        const std::uint32_t second = mesh.indices[index + 1];
        const std::uint32_t third = mesh.indices[index + 2];
        if (first >= mesh.vertices.size() || second >= mesh.vertices.size() ||
            third >= mesh.vertices.size()) {
            continue;
        }
        const glm::vec3 faceNormal = glm::cross(
            mesh.vertices[second].position - mesh.vertices[first].position,
            mesh.vertices[third].position - mesh.vertices[first].position
        );
        normalSums[first] += faceNormal;
        normalSums[second] += faceNormal;
        normalSums[third] += faceNormal;
    }

    for (const auto& [key, indices] : vertexGroups) {
        (void)key;
        glm::vec3 averageNormal(0.0f);
        for (const std::uint32_t index : indices) {
            averageNormal += normalSums[index];
        }
        if (glm::length(averageNormal) <= std::numeric_limits<float>::epsilon()) {
            averageNormal = glm::vec3(0.0f, 1.0f, 0.0f);
        } else {
            averageNormal = glm::normalize(averageNormal);
        }
        for (const std::uint32_t index : indices) {
            mesh.vertices[index].normal = averageNormal;
        }
    }
}

bool metadataContainsPoint(const TerrainTileMetadata& tile, const glm::vec2& point) {
    const glm::vec2 halfSize(tile.areaSize * 0.5f);
    const glm::vec2 minimum = tile.gridPosition - halfSize;
    const glm::vec2 maximum = tile.gridPosition + halfSize;
    constexpr float epsilon = 0.0001f;
    return point.x >= minimum.x - epsilon && point.y >= minimum.y - epsilon &&
        point.x <= maximum.x + epsilon && point.y <= maximum.y + epsilon;
}

bool coveredByHigherResolutionTiles(
    const TerrainTileMetadata& candidate,
    const std::vector<ParsedTile>& selectedTiles
) {
    const glm::vec2 halfSize(candidate.areaSize * 0.5f);
    const glm::vec2 minimum = candidate.gridPosition - halfSize;
    const glm::vec2 maximum = candidate.gridPosition + halfSize;
    const std::array<glm::vec2, 5> probePoints = {{
        minimum,
        glm::vec2(maximum.x, minimum.y),
        glm::vec2(minimum.x, maximum.y),
        maximum,
        candidate.gridPosition
    }};
    return std::all_of(
        probePoints.begin(),
        probePoints.end(),
        [&candidate, &selectedTiles](const glm::vec2& point) {
            return std::any_of(
                selectedTiles.begin(),
                selectedTiles.end(),
                [&candidate, &point](const ParsedTile& selected) {
                    return selected.metadata.lod > candidate.lod &&
                        metadataContainsPoint(selected.metadata, point);
                }
            );
        }
    );
}

} // namespace

TerrainTile::TerrainTile(
    TerrainTileMetadata metadata,
    std::vector<std::uint16_t> heights,
    float worldScale,
    float maximumHeight
)
    : metadata_(std::move(metadata)), heights_(std::move(heights)) {
    constexpr std::size_t expectedSamples =
        static_cast<std::size_t>(kTerrainHeightmapResolution) *
        kTerrainHeightmapResolution;
    if (heights_.size() != expectedSamples) {
        throw std::invalid_argument("TerrainTile requires a 256x256 height sample table");
    }

    const float worldExtent = worldScale * kTerrainWorldExtentMultiplier;
    const glm::vec2 worldCenter =
        metadata_.gridPosition / kTerrainGridSize * worldExtent;
    const float worldSize =
        metadata_.areaSize / kTerrainGridSize * worldExtent;
    const auto minimumIt = std::min_element(heights_.begin(), heights_.end());
    const auto maximumIt = std::max_element(heights_.begin(), heights_.end());
    const float minimumY = worldHeight(*minimumIt, maximumHeight);
    const float maximumY = worldHeight(*maximumIt, maximumHeight);
    boundsMinimum_ = glm::vec3(
        worldCenter.x - worldSize * 0.5f,
        minimumY,
        worldCenter.y - worldSize * 0.5f
    );
    boundsMaximum_ = glm::vec3(
        worldCenter.x + worldSize * 0.5f,
        maximumY,
        worldCenter.y + worldSize * 0.5f
    );
}

bool TerrainTile::containsWorldPoint(const glm::vec2& worldPosition) const {
    return worldPosition.x >= boundsMinimum_.x &&
        worldPosition.x <= boundsMaximum_.x &&
        worldPosition.y >= boundsMinimum_.z &&
        worldPosition.y <= boundsMaximum_.z;
}

std::optional<float> TerrainTile::heightAtWorld(
    const glm::vec2& worldPosition,
    float worldScale,
    float maximumHeight
) const {
    if (!containsWorldPoint(worldPosition)) {
        return std::nullopt;
    }

    const float worldExtent = worldScale * kTerrainWorldExtentMultiplier;
    const glm::vec2 worldCenter =
        metadata_.gridPosition / kTerrainGridSize * worldExtent;
    const float worldSize = metadata_.areaSize / kTerrainGridSize * worldExtent;
    if (worldSize <= std::numeric_limits<float>::epsilon()) {
        return std::nullopt;
    }

    const glm::vec2 normalized = glm::clamp(
        (worldPosition - (worldCenter - glm::vec2(worldSize * 0.5f))) / worldSize,
        glm::vec2(0.0f),
        glm::vec2(1.0f)
    );
    const float sampleX = normalized.x * static_cast<float>(kTerrainHeightmapResolution - 1);
    const float sampleZ = normalized.y * static_cast<float>(kTerrainHeightmapResolution - 1);
    const std::uint32_t x0 = static_cast<std::uint32_t>(std::floor(sampleX));
    const std::uint32_t z0 = static_cast<std::uint32_t>(std::floor(sampleZ));
    const std::uint32_t x1 = std::min(x0 + 1, kTerrainHeightmapResolution - 1);
    const std::uint32_t z1 = std::min(z0 + 1, kTerrainHeightmapResolution - 1);
    const float tx = sampleX - static_cast<float>(x0);
    const float tz = sampleZ - static_cast<float>(z0);
    const float top = glm::mix(
        static_cast<float>(sample(x0, z0)),
        static_cast<float>(sample(x1, z0)),
        tx
    );
    const float bottom = glm::mix(
        static_cast<float>(sample(x0, z1)),
        static_cast<float>(sample(x1, z1)),
        tx
    );
    return worldHeight(
        static_cast<std::uint16_t>(std::clamp(glm::mix(top, bottom, tz), 0.0f, 65535.0f)),
        maximumHeight
    );
}

std::uint16_t TerrainTile::sample(std::uint32_t x, std::uint32_t z) const {
    x = std::min(x, kTerrainHeightmapResolution - 1);
    z = std::min(z, kTerrainHeightmapResolution - 1);
    return heights_[static_cast<std::size_t>(z) * kTerrainHeightmapResolution + x];
}

float TerrainTile::worldHeight(std::uint16_t sampleValue, float maximumHeight) const {
    return static_cast<float>(sampleValue) / 65535.0f * maximumHeight;
}

glm::vec3 TerrainTile::position(
    std::uint32_t x,
    std::uint32_t z,
    float worldScale,
    float maximumHeight
) const {
    const float worldExtent = worldScale * kTerrainWorldExtentMultiplier;
    const glm::vec2 worldCenter =
        metadata_.gridPosition / kTerrainGridSize * worldExtent;
    const float worldSize =
        metadata_.areaSize / kTerrainGridSize * worldExtent;
    const float normalizedX = static_cast<float>(x) /
        static_cast<float>(kTerrainHeightmapResolution - 1);
    const float normalizedZ = static_cast<float>(z) /
        static_cast<float>(kTerrainHeightmapResolution - 1);
    return glm::vec3(
        worldCenter.x + (normalizedX - 0.5f) * worldSize,
        worldHeight(sample(x, z), maximumHeight),
        worldCenter.y + (normalizedZ - 0.5f) * worldSize
    );
}

void TerrainTile::appendMesh(
    std::uint32_t sampleStride,
    float worldScale,
    float maximumHeight,
    std::vector<Vertex>& vertices,
    std::vector<std::uint32_t>& indices
) const {
    sampleStride = std::clamp(sampleStride, 1u, kTerrainHeightmapResolution - 1);
    std::vector<std::uint32_t> sampleCoordinates;
    for (std::uint32_t sampleCoordinate = 0;
         sampleCoordinate < kTerrainHeightmapResolution - 1;
         sampleCoordinate += sampleStride) {
        sampleCoordinates.push_back(sampleCoordinate);
    }
    if (sampleCoordinates.empty() || sampleCoordinates.back() != kTerrainHeightmapResolution - 1) {
        sampleCoordinates.push_back(kTerrainHeightmapResolution - 1);
    }

    const std::uint32_t rowSize = static_cast<std::uint32_t>(sampleCoordinates.size());
    const std::size_t vertexStart = vertices.size();
    vertices.reserve(vertices.size() +
        static_cast<std::size_t>(rowSize) * rowSize);
    for (const std::uint32_t z : sampleCoordinates) {
        for (const std::uint32_t x : sampleCoordinates) {
            const glm::vec3 current = position(x, z, worldScale, maximumHeight);
            const glm::vec3 left = position(
                x == 0 ? 0 : x - 1,
                z,
                worldScale,
                maximumHeight
            );
            const glm::vec3 right = position(
                x + 1 >= kTerrainHeightmapResolution
                    ? kTerrainHeightmapResolution - 1
                    : x + 1,
                z,
                worldScale,
                maximumHeight
            );
            const glm::vec3 down = position(
                x,
                z == 0 ? 0 : z - 1,
                worldScale,
                maximumHeight
            );
            const glm::vec3 up = position(
                x,
                z + 1 >= kTerrainHeightmapResolution
                    ? kTerrainHeightmapResolution - 1
                    : z + 1,
                worldScale,
                maximumHeight
            );
            glm::vec3 normal = glm::cross(up - down, right - left);
            if (glm::length(normal) <= std::numeric_limits<float>::epsilon()) {
                normal = glm::vec3(0.0f, 1.0f, 0.0f);
            } else {
                normal = glm::normalize(normal);
            }
            const float normalizedHeight = maximumHeight <= 0.0f
                ? 0.0f
                : current.y / maximumHeight;
            vertices.push_back({
                current,
                normal,
                glm::vec2(
                    static_cast<float>(x) /
                        static_cast<float>(kTerrainHeightmapResolution - 1),
                    static_cast<float>(z) /
                        static_cast<float>(kTerrainHeightmapResolution - 1)
                ),
                glm::vec4(
                    0.25f + normalizedHeight * 0.22f,
                    0.34f + normalizedHeight * 0.20f,
                    0.16f + normalizedHeight * 0.12f,
                    1.0f
                )
            });
        }
    }

    indices.reserve(indices.size() +
        static_cast<std::size_t>(rowSize - 1) * (rowSize - 1) * 6);
    for (std::uint32_t z = 0; z + 1 < rowSize; ++z) {
        for (std::uint32_t x = 0; x + 1 < rowSize; ++x) {
            const std::uint32_t first = static_cast<std::uint32_t>(
                vertexStart + static_cast<std::size_t>(z) * rowSize + x
            );
            const std::uint32_t second = first + rowSize;
            indices.insert(indices.end(), {
                first, second, first + 1,
                second, second + 1, first + 1
            });
        }
    }
}

TerrainChunk::TerrainChunk(std::uint32_t lod, glm::ivec2 coordinate)
    : lod_(lod), coordinate_(coordinate),
      boundsMinimum_(std::numeric_limits<float>::max()),
      boundsMaximum_(std::numeric_limits<float>::lowest()) {}

void TerrainChunk::addTile(TerrainTile tile) {
    boundsMinimum_ = glm::min(boundsMinimum_, tile.boundsMinimum());
    boundsMaximum_ = glm::max(boundsMaximum_, tile.boundsMaximum());
    tiles_.push_back(std::move(tile));
}

void TerrainChunk::appendMesh(
    std::uint32_t sampleStride,
    float worldScale,
    float maximumHeight,
    TerrainMeshData& mesh
) const {
    TerrainChunkMesh chunkMesh;
    chunkMesh.lod = lod_;
    chunkMesh.coordinate = coordinate_;
    chunkMesh.tileCount = static_cast<std::uint32_t>(tiles_.size());
    chunkMesh.indexOffset = mesh.indices.size();
    chunkMesh.boundsMinimum = boundsMinimum_;
    chunkMesh.boundsMaximum = boundsMaximum_;
    for (const TerrainTile& tile : tiles_) {
        tile.appendMesh(
            sampleStride,
            worldScale,
            maximumHeight,
            mesh.vertices,
            mesh.indices
        );
    }
    chunkMesh.indexCount = mesh.indices.size() - chunkMesh.indexOffset;
    mesh.chunks.push_back(chunkMesh);
}

std::unique_ptr<TerrainScene> TerrainScene::load(
    const std::filesystem::path& tscbPath,
    const std::filesystem::path& terrainRoot
) {
    const std::vector<char> data = readBinaryFile(tscbPath);
    requireRange(data, 0, kTscbHeaderSize, "header");
    if (std::string(data.data(), data.data() + 4) != "TSCB") {
        throw std::runtime_error("invalid TSCB signature: " + tscbPath.string());
    }
    if (readU32BE(data, 0x04) != 0x0A000000u) {
        throw std::runtime_error("unsupported TSCB version: " + tscbPath.string());
    }
    if (readU32BE(data, 0x08) != 1u) {
        throw std::runtime_error("unsupported TSCB scene marker: " + tscbPath.string());
    }

    auto scene = std::unique_ptr<TerrainScene>(new TerrainScene());
    scene->header_.worldScale = readF32BE(data, 0x10);
    scene->header_.maximumHeight = readF32BE(data, 0x14);
    const std::uint32_t materialCount = readU32BE(data, 0x18);
    scene->header_.areaCount = readU32BE(data, 0x1c);
    scene->header_.tileSize = readF32BE(data, 0x28);
    if (scene->header_.worldScale <= 0.0f ||
        scene->header_.maximumHeight <= 0.0f ||
        scene->header_.tileSize <= 0.0f) {
        throw std::runtime_error("TSCB has invalid terrain scale values");
    }

    const std::uint32_t materialSectionSize = readU32BE(data, 0x30);
    const std::size_t areaLookupOffset =
        kTscbHeaderSize - sizeof(std::uint32_t) + materialSectionSize;
    requireRange(
        data,
        areaLookupOffset,
        static_cast<std::size_t>(scene->header_.areaCount) * sizeof(std::uint32_t),
        "area lookup table"
    );
    (void)materialCount;

    std::vector<ParsedTile> parsedTiles;
    parsedTiles.reserve(scene->header_.areaCount);
    for (std::uint32_t index = 0; index < scene->header_.areaCount; ++index) {
        const std::size_t lookupOffset =
            areaLookupOffset + static_cast<std::size_t>(index) * sizeof(std::uint32_t);
        const std::size_t entryOffset = lookupOffset + readU32BE(data, lookupOffset);
        requireRange(data, entryOffset, 0x30, "area entry");

        TerrainTileMetadata metadata;
        metadata.gridPosition = glm::vec2(
            readF32BE(data, entryOffset + 0x00),
            readF32BE(data, entryOffset + 0x04)
        );
        metadata.areaSize = readF32BE(data, entryOffset + 0x08);
        metadata.minimumHeight = readF32BE(data, entryOffset + 0x0c);
        metadata.maximumHeight = readF32BE(data, entryOffset + 0x10);
        metadata.minimumWaterHeight = readF32BE(data, entryOffset + 0x14);
        metadata.maximumWaterHeight = readF32BE(data, entryOffset + 0x18);
        metadata.lod = lodFromFileBase(
            readRelativeString(data, entryOffset, readU32BE(data, entryOffset + 0x20))
        );
        metadata.fileBase = readRelativeString(
            data,
            entryOffset,
            readU32BE(data, entryOffset + 0x20)
        );

        const std::filesystem::path heightmapPath = findHeightmapPath(
            terrainRoot,
            metadata.fileBase
        );
        if (heightmapPath.empty()) {
            continue;
        }
        try {
            parsedTiles.push_back({
                std::move(metadata),
                loadHeightSamples(heightmapPath)
            });
            parsedTiles.back().metadata.sourcePath = heightmapPath;
        } catch (const std::exception& error) {
            throw std::runtime_error(
                "failed to load terrain tile " + metadata.fileBase + ": " + error.what()
            );
        }
    }

    if (parsedTiles.empty()) {
        throw std::runtime_error(
            "TSCB parsed successfully but no referenced .hght files were found under " +
            terrainRoot.string()
        );
    }

    // If several resolutions are present, keep the finest loaded tile where
    // it completely covers a coarser tile. This prevents overlapping LOD
    // meshes while still allowing a partial high-resolution extraction to
    // fall back to coarser data.
    std::sort(
        parsedTiles.begin(),
        parsedTiles.end(),
        [](const ParsedTile& left, const ParsedTile& right) {
            if (left.metadata.lod != right.metadata.lod) {
                return left.metadata.lod > right.metadata.lod;
            }
            return left.metadata.fileBase < right.metadata.fileBase;
        }
    );
    std::vector<ParsedTile> selectedTiles;
    selectedTiles.reserve(parsedTiles.size());
    for (ParsedTile& candidate : parsedTiles) {
        const bool covered = coveredByHigherResolutionTiles(
            candidate.metadata,
            selectedTiles
        );
        if (!covered) {
            selectedTiles.push_back(std::move(candidate));
        }
    }

    using ChunkKey = std::tuple<std::uint32_t, int, int>;
    std::map<ChunkKey, std::size_t> chunkIndices;
    for (ParsedTile& parsed : selectedTiles) {
        const float chunkSpan = parsed.metadata.areaSize * 2.0f;
        const int chunkX = static_cast<int>(std::floor(
            (parsed.metadata.gridPosition.x + scene->header_.tileSize * 0.5f) /
            chunkSpan
        ));
        const int chunkZ = static_cast<int>(std::floor(
            (parsed.metadata.gridPosition.y + scene->header_.tileSize * 0.5f) /
            chunkSpan
        ));
        const ChunkKey key{parsed.metadata.lod, chunkX, chunkZ};
        auto [it, inserted] = chunkIndices.emplace(key, scene->chunks_.size());
        if (inserted) {
            scene->chunks_.emplace_back(
                parsed.metadata.lod,
                glm::ivec2(chunkX, chunkZ)
            );
        }
        scene->chunks_[it->second].addTile(TerrainTile(
            std::move(parsed.metadata),
            std::move(parsed.heights),
            scene->header_.worldScale,
            scene->header_.maximumHeight
        ));
    }

    std::sort(
        scene->chunks_.begin(),
        scene->chunks_.end(),
        [](const TerrainChunk& left, const TerrainChunk& right) {
            if (left.lod() != right.lod()) {
                return left.lod() < right.lod();
            }
            if (left.coordinate().y != right.coordinate().y) {
                return left.coordinate().y < right.coordinate().y;
            }
            return left.coordinate().x < right.coordinate().x;
        }
    );
    scene->boundsMinimum_ = scene->chunks_.front().boundsMinimum();
    scene->boundsMaximum_ = scene->chunks_.front().boundsMaximum();
    for (const TerrainChunk& chunk : scene->chunks_) {
        scene->boundsMinimum_ = glm::min(scene->boundsMinimum_, chunk.boundsMinimum());
        scene->boundsMaximum_ = glm::max(scene->boundsMaximum_, chunk.boundsMaximum());
    }
    return scene;
}

std::size_t TerrainScene::tileCount() const {
    std::size_t count = 0;
    for (const TerrainChunk& chunk : chunks_) {
        count += chunk.tiles().size();
    }
    return count;
}

std::optional<float> TerrainScene::heightAtWorld(const glm::vec2& worldPosition) const {
    const TerrainTile* selectedTile = nullptr;
    for (const TerrainChunk& chunk : chunks_) {
        for (const TerrainTile& tile : chunk.tiles()) {
            if (!tile.containsWorldPoint(worldPosition)) {
                continue;
            }
            if (selectedTile == nullptr || tile.metadata().lod > selectedTile->metadata().lod) {
                selectedTile = &tile;
            }
        }
    }
    return selectedTile == nullptr
        ? std::nullopt
        : selectedTile->heightAtWorld(
            worldPosition,
            header_.worldScale,
            header_.maximumHeight
        );
}

glm::vec3 TerrainScene::centerPosition() const {
    const glm::vec2 terrainCenter(
        (boundsMinimum_.x + boundsMaximum_.x) * 0.5f,
        (boundsMinimum_.z + boundsMaximum_.z) * 0.5f
    );

    // MainField's geometric midpoint is the shared corner of four tiles. A
    // player spawned exactly on that corner sees the independent source
    // height samples as a hard cross-shaped seam. Pick the center of the
    // nearest central tile instead, which remains at the middle of MainField
    // while giving the player a continuous local ground surface.
    const TerrainTile* centralTile = nullptr;
    glm::vec3 centralTileCenter(0.0f);
    float bestDistance = std::numeric_limits<float>::max();
    for (const TerrainChunk& chunk : chunks_) {
        for (const TerrainTile& tile : chunk.tiles()) {
            const glm::vec3 tileCenter =
                (tile.boundsMinimum() + tile.boundsMaximum()) * 0.5f;
            const float distance = glm::distance(
                glm::vec2(tileCenter.x, tileCenter.z),
                terrainCenter
            );
            const bool preferredTie = centralTile != nullptr &&
                std::abs(distance - bestDistance) < 0.0001f &&
                (tileCenter.x > centralTileCenter.x ||
                 (tileCenter.x == centralTileCenter.x && tileCenter.z > centralTileCenter.z));
            if (centralTile == nullptr || distance < bestDistance || preferredTie) {
                centralTile = &tile;
                centralTileCenter = tileCenter;
                bestDistance = distance;
            }
        }
    }
    if (centralTile != nullptr) {
        const std::optional<float> height = centralTile->heightAtWorld(
            glm::vec2(centralTileCenter.x, centralTileCenter.z),
            header_.worldScale,
            header_.maximumHeight
        );
        return glm::vec3(
            centralTileCenter.x,
            height.value_or(centralTileCenter.y),
            centralTileCenter.z
        );
    }

    const std::optional<float> height = heightAtWorld(terrainCenter);
    return glm::vec3(terrainCenter.x, height.value_or(boundsMinimum_.y), terrainCenter.y);
}

TerrainMeshData TerrainScene::buildMesh(const TerrainMeshOptions& options) const {
    TerrainMeshData mesh;
    for (const TerrainChunk& chunk : chunks_) {
        chunk.appendMesh(
            options.sampleStride,
            header_.worldScale,
            header_.maximumHeight,
            mesh
        );
    }
    stitchTerrainMesh(mesh);
    return mesh;
}

} // namespace VulkCore
