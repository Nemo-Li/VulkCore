#include "vulkcore/Terrain/Grass.hpp"

#include "vulkcore/Terrain/Terrain.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace VulkCore {
namespace {

struct GrassLodSettings {
    float cellSize;
    int samplesPerSide;
    glm::vec4 fade; // Fade-in start/end and fade-out start/end, in terrain units.
};

constexpr GrassLodSettings kNearMeshSettings{8.0f, 16, {0.0f, 0.0f, 30.0f, 40.0f}};
constexpr GrassLodSettings kCrossCardSettings{16.0f, 20,
    {kNearMeshSettings.fade.z, kNearMeshSettings.fade.w, 83.0f, 112.0f}};
constexpr GrassLodSettings kFlowerSettings{8.0f, 8, {0.0f, 0.0f, 19.0f, 27.0f}};
constexpr float kMappedGrassHeight = kDefaultGrassHeight * 1.18f;
constexpr float kAnchorSize = 4.0f;
constexpr float kAnchorPadding = 3.0f; // Half the anchor cell's diagonal, rounded up.
constexpr float kPi = 3.14159265358979323846f;
constexpr float kGoldenAngle = 2.39996323f;

const GrassLodSettings& settings(GrassLod lod) {
    if (lod == GrassLod::NearMesh) return kNearMeshSettings;
    if (lod == GrassLod::CrossCard) return kCrossCardSettings;
    return kFlowerSettings;
}

std::uint32_t hashBits(std::uint32_t value) {
    value ^= value >> 16u;
    value *= 0x7FEB352Du;
    value ^= value >> 15u;
    value *= 0x846CA68Bu;
    return value ^ (value >> 16u);
}

float hash01(std::uint32_t value) {
    return static_cast<float>(hashBits(value) & 0x00FFFFFFu) / 16777216.0f;
}

std::uint32_t cellSeed(int x, int z, GrassLod lod) {
    return hashBits(0xB07F123u ^ (static_cast<std::uint32_t>(lod) * 0x94D049BBu) ^
        hashBits(static_cast<std::uint32_t>(x) * 0x9E3779B9u) ^
        hashBits(static_cast<std::uint32_t>(z) * 0x85EBCA6Bu));
}

float smoothstep(float low, float high, float value) {
    const float t = std::clamp((value - low) / (high - low), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

bool intersectsDisk(glm::vec2 origin, glm::vec2 minimum, glm::vec2 maximum, float radius) {
    const glm::vec2 nearest = glm::clamp(origin, minimum, maximum) - origin;
    return glm::dot(nearest, nearest) <= radius * radius;
}

} // namespace

GrassCoverMask::GrassCoverMask(std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t> red)
    : width_(width), height_(height), red_(std::move(red)) {
    if (!width_ || !height_ || red_.size() != static_cast<std::uint64_t>(width_) * height_) {
        throw std::invalid_argument("invalid grass cover mask dimensions");
    }
}

bool GrassCoverMask::contains(glm::vec2 worldPosition) const {
    if (empty()) return true;
    const glm::vec2 texel = glm::fract(worldPosition / kTileSize) * glm::vec2(width_, height_) - 0.5f;
    const glm::ivec2 low(glm::floor(texel));
    const glm::vec2 fraction = glm::fract(texel);
    const auto sample = [&](glm::ivec2 position) {
        const int x = (position.x + static_cast<int>(width_)) % static_cast<int>(width_);
        const int z = (position.y + static_cast<int>(height_)) % static_cast<int>(height_);
        return static_cast<float>(red_[std::size_t(z) * width_ + x]);
    };
    const float row0 = std::lerp(sample(low), sample(low + glm::ivec2(1, 0)), fraction.x);
    const float row1 = std::lerp(sample(low + glm::ivec2(0, 1)), sample(low + glm::ivec2(1, 1)), fraction.x);
    // The source's background starts at 127 rather than black.
    return std::lerp(row0, row1, fraction.y) >= 191.0f;
}

void GrassCoverage::setCoverMask(GrassCoverMask coverMask) {
    coverMask_ = std::move(coverMask);
    anchor_.reset();
    for (auto& cache : cells_) cache.clear();
    instances_.clear();
    ranges_.clear();
}

glm::vec4 grassFadeDistances(GrassLod lod) {
    return settings(lod).fade;
}

GrassCoverage::Cell GrassCoverage::buildCell(
    const TerrainScene& terrain, GrassLod lod, CellCoordinate coordinate
) const {
    Cell cell;
    cell.minimum = glm::vec3(std::numeric_limits<float>::max());
    cell.maximum = glm::vec3(std::numeric_limits<float>::lowest());
    const auto& lodSettings = settings(lod);
    const int count = lodSettings.samplesPerSide;
    const float spacing = lodSettings.cellSize / static_cast<float>(count);
    const bool flower = !isGrassVegetation(lod);
    const bool bladeMesh = lod == GrassLod::NearMesh;
    const bool yellowFlower = lod == GrassLod::YellowFlowerCard;
    const float flowerChance = yellowFlower ? 0.39f : 0.05f;
    const auto append = [&](glm::vec3 root, glm::vec3 normal, float height, float widthOrSeed,
                            float yaw, float tiltSeed, std::uint32_t seed, float variation) {
        GrassInstance instance{
            glm::vec4(root, height), glm::vec4(normal, widthOrSeed),
            glm::vec4(yaw, tiltSeed, hash01(seed + 8u), variation)
        };
        const float maxWidth = bladeMesh ? GrassGrowthSettings::kMaxBladeWidth : widthOrSeed;
        const float reach = height + maxWidth + 0.9f;
        const float drop = 1.1f;
        cell.minimum = glm::min(cell.minimum, root - glm::vec3(reach, drop, reach));
        cell.maximum = glm::max(cell.maximum, root + glm::vec3(reach, height, reach));
        cell.instances.push_back(instance);
    };

    for (int z = 0; z < count; ++z) {
        for (int x = 0; x < count; ++x) {
            const int gx = coordinate.first * count + x;
            const int gz = coordinate.second * count + z;
            const std::uint32_t seed = cellSeed(gx, gz, lod);
            if (flower && hash01(seed) > flowerChance) continue;
            const glm::vec2 point = (glm::vec2(gx, gz) + glm::vec2(
                0.12f + hash01(seed + 1u) * 0.76f, 0.12f + hash01(seed + 2u) * 0.76f
            )) * spacing;
            if (lod == GrassLod::CrossCard && !coverMask_.contains(point)) continue;
            const auto map = terrain.grassAtWorld(point);
            if (map && (map->height <= 0.0f || lod == GrassLod::FlowerCard)) continue;
            const auto ground = terrain.renderedHeightAtWorld(point);
            if (!ground) continue;
            const auto water = terrain.waterHeightAtWorld(point);
            if (water && *water > *ground + 0.02f) continue;

            constexpr float probe = 0.28f;
            const glm::vec2 dx(probe, 0.0f);
            const glm::vec2 dz(0.0f, probe);
            const glm::vec3 normal = glm::normalize(glm::vec3(
                terrain.renderedHeightAtWorld(point - dx).value_or(*ground) -
                    terrain.renderedHeightAtWorld(point + dx).value_or(*ground),
                2.0f * probe,
                terrain.renderedHeightAtWorld(point - dz).value_or(*ground) -
                    terrain.renderedHeightAtWorld(point + dz).value_or(*ground)
            ));
            if (!map) {
                const float slope = smoothstep(0.76f, 0.94f, normal.y);
                const float altitude = 1.0f - smoothstep(
                    0.66f, 0.84f, *ground / terrain.header().maximumHeight
                );
                const float patch = 0.83f + 0.17f * std::sin(
                    point.x * 0.091f + std::sin(point.y * 0.037f) + point.y * 0.053f
                );
                if (hash01(seed + 3u) >= slope * altitude * patch) continue;
            }

            const glm::vec3 root(point.x, *ground + 0.012f, point.y);
            const float heightVariation = hash01(seed + 4u);
            const float yaw = hash01(seed + 5u) * 2.0f * kPi;
            if (bladeMesh) {
                const float canopyHeight = kDefaultGrassHeight * (0.91f + heightVariation * 0.18f);
                constexpr int blades = 14;
                for (int blade = 0; blade < blades; ++blade) {
                    const auto bladeSeed = hashBits(seed + static_cast<std::uint32_t>(blade + 1));
                    const float angle = yaw + kGoldenAngle * static_cast<float>(blade);
                    const float radius = 0.34f * std::sqrt(static_cast<float>(blade) / blades) *
                        std::lerp(0.78f, 1.18f, hash01(bladeSeed + 6u));
                    const glm::vec2 offset = glm::vec2(std::cos(angle), std::sin(angle)) * radius;
                    const glm::vec2 bladePoint = point + offset;
                    if (!coverMask_.contains(bladePoint)) continue;
                    const auto bladeMap = terrain.grassAtWorld(bladePoint);
                    if (bladeMap ? bladeMap->height <= 0.0f : map.has_value()) continue;
                    const float y = root.y -
                        glm::dot(glm::vec2(normal.x, normal.z), offset) / std::max(normal.y, 0.35f);
                    const float bladeHeight = bladeMap ? bladeMap->height * kMappedGrassHeight : canopyHeight;
                    const float bladeYaw = angle + std::lerp(-0.85f, 0.85f, hash01(bladeSeed + 5u));
                    const float variation = hash01(bladeSeed + 9u);
                    append({bladePoint.x, y, bladePoint.y}, normal,
                           bladeHeight * std::lerp(0.79f, 1.20f, hash01(bladeSeed + 1u)),
                           hash01(bladeSeed + 2u), bladeYaw,
                           hash01(bladeSeed + 3u), bladeSeed, variation);
                }
            } else {
                float height = 0.48f + heightVariation * 0.32f;
                float width = 0.42f;
                if (!flower) {
                    const float canopy = map ? map->height * kMappedGrassHeight : kDefaultGrassHeight;
                    height = canopy * std::lerp(0.85f, 1.02f, heightVariation);
                    width = height * 1.75f;
                } else if (yellowFlower) {
                    const float canopy = map ? map->height * kMappedGrassHeight : kDefaultGrassHeight;
                    height = canopy * std::lerp(0.94f, 1.14f, heightVariation);
                    width = std::min(0.092f, height * 0.092f);
                }
                const float shade = hash01(seed + 9u);
                const float tilt = hash01(seed + 6u);
                append(root, normal, height, width, yaw, tilt, seed, shade);
                append(root, normal, height, width, yaw + kPi * 0.5f, tilt, seed, shade);
            }
        }
    }
    return cell;
}

bool GrassCoverage::update(const TerrainScene& terrain, glm::vec2 center) {
    if (!std::isfinite(center.x) || !std::isfinite(center.y)) return false;
    const glm::vec2 terrainMin(terrain.boundsMinimum().x, terrain.boundsMinimum().z);
    const glm::vec2 terrainMax(terrain.boundsMaximum().x, terrain.boundsMaximum().z);
    const glm::vec2 limit(kCrossCardSettings.fade.w + kAnchorPadding + kAnchorSize);
    center = glm::clamp(center, terrainMin - limit, terrainMax + limit);
    const glm::ivec2 rounded(glm::floor(center / kAnchorSize));
    const CellCoordinate anchor{rounded.x, rounded.y};
    if (anchor_ == anchor) return false;

    const glm::vec2 origin = (glm::vec2(rounded) + 0.5f) * kAnchorSize;
    ++generation_;
    instances_.clear();
    ranges_.clear();
    for (std::size_t layer = 0; layer < cells_.size(); ++layer) {
        const auto lod = kGrassLods[layer];
        const auto& lodSettings = settings(lod);
        auto& cache = cells_[layer];
        const float span = lodSettings.cellSize;
        const float far = lodSettings.fade.w + kAnchorPadding;
        const glm::ivec2 first(glm::floor(glm::max(origin - far, terrainMin) / span));
        const glm::ivec2 last(glm::floor(glm::min(origin + far, terrainMax) / span));
        for (int z = first.y; z <= last.y; ++z) {
            for (int x = first.x; x <= last.x; ++x) {
                const glm::vec2 minimum = glm::vec2(x, z) * span;
                if (!intersectsDisk(origin, minimum, minimum + span, far)) continue;
                const CellCoordinate coordinate{x, z};
                auto it = cache.find(coordinate);
                if (it == cache.end()) {
                    it = cache.emplace(coordinate, buildCell(terrain, lod, coordinate)).first;
                }
                auto& cell = it->second;
                cell.lastUse = generation_;
                if (cell.instances.empty()) continue;
                ranges_.push_back({
                    lod, static_cast<std::uint32_t>(instances_.size()),
                    static_cast<std::uint32_t>(cell.instances.size()), cell.minimum, cell.maximum
                });
                instances_.insert(instances_.end(), cell.instances.begin(), cell.instances.end());
            }
        }
        std::erase_if(cache, [this](const auto& item) {
            return item.second.lastUse != generation_;
        });
    }
    anchor_ = anchor;
    return true;
}

} // namespace VulkCore
