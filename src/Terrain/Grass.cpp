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

constexpr GrassLodSettings kNearMeshSettings{8.0f, 20, {0.0f, 0.0f, 30.0f, 40.0f}};
constexpr GrassLodSettings kCrossCardSettings{16.0f, 40,
    {kNearMeshSettings.fade.z, kNearMeshSettings.fade.w, 65.0f, 96.0f}};
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

    constexpr int layerCount = 3;
    constexpr int anglesPerSector = 120;
    constexpr int angleCount = layerCount * anglesPerSector;
    static const auto rotations = [] {
        std::array<glm::vec2, angleCount> directions;
        for (int i = 0; i < angleCount; ++i) {
            const float angle = 2.0f * kPi * static_cast<float>(i) / angleCount;
            directions[i] = {std::cos(angle), std::sin(angle)};
        }
        return directions;
    }();
    const glm::vec2 scaled = worldPosition / kTileSize;
    const glm::vec2 block = glm::floor(scaled);
    const glm::vec2 local = scaled - block - 0.5f;
    // Seed from the mask block so both LODs agree, even when a root lies in a neighboring draw cell.
    const std::uint32_t seed = cellSeed(static_cast<int>(block.x), static_cast<int>(block.y), GrassLod::NearMesh);
    const int phase = static_cast<int>(seed % angleCount);
    for (int layer = 0; layer < layerCount; ++layer) {
        const auto jitter = hashBits(seed + 1u + static_cast<std::uint32_t>(layer)) % anglesPerSector;
        const int angle = (phase + layer * anglesPerSector + static_cast<int>(jitter)) % angleCount;
        const glm::vec2 direction = rotations[angle];
        const glm::vec2 rotated(direction.x * local.x - direction.y * local.y,
                                direction.y * local.x + direction.x * local.y);
        if (matches(rotated + 0.5f)) return true;
    }
    return false;
}

bool GrassCoverMask::matches(glm::vec2 uv) const {
    const glm::vec2 texel = glm::fract(uv) * glm::vec2(width_, height_) - 0.5f;
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
    constexpr float groundOffset = 0.012f;
    constexpr float baseClearanceRatio = 0.06f;
    const auto append = [&](glm::vec3 root, glm::vec3 normal, float height, float widthOrSeed,
                            float yaw, float tiltSeed, std::uint32_t seed, float variation,
                            const GrassSample& map) {
        // A fixed offset would leave millimeter-height blades floating above the ground.
        root.y += std::min(groundOffset, height * baseClearanceRatio);
        GrassInstance instance{
            glm::vec4(root, height), glm::vec4(normal, widthOrSeed),
            glm::vec4(yaw, tiltSeed, hash01(seed + 8u), variation)
        };
        if (!flower) {
            const auto channel = [](float value) {
                return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
            };
            instance.mapColor = {channel(map.color.r), channel(map.color.g), channel(map.color.b), 255};
        }
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
            const auto map = terrain.grassAtWorld(point);
            if (!map || (!bladeMesh && map->height <= 0.0f)) continue;
            const float heightVariation = hash01(seed + 4u);
            const float canopy = map->height * kMappedGrassHeight;
            const float cardHeight = canopy * std::lerp(0.90f, 1.10f, heightVariation);
            const float cardWidth = cardHeight * 1.75f;
            if (lod == GrassLod::CrossCard && cardWidth <= kGrassCardMinWidth) continue;
            const auto ground = terrain.renderedHeightAtWorld(point);
            if (!ground) continue;
            if (!bladeMesh) {
                const auto water = terrain.waterHeightAtWorld(point);
                if (water && *water > *ground + 0.02f) continue;
            }

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

            const glm::vec3 root(point.x, *ground, point.y);
            const float yaw = hash01(seed + 5u) * 2.0f * kPi;
            if (bladeMesh) {
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
                    if (!bladeMap || bladeMap->height <= 0.0f) continue;
                    const auto bladeGround = terrain.renderedHeightAtWorld(bladePoint);
                    if (!bladeGround) continue;
                    const auto water = terrain.waterHeightAtWorld(bladePoint);
                    if (water && *water > *bladeGround + 0.02f) continue;
                    const float bladeHeight = bladeMap->height * kMappedGrassHeight;
                    const float bladeYaw = angle + std::lerp(-0.85f, 0.85f, hash01(bladeSeed + 5u));
                    const float variation = hash01(bladeSeed + 9u);
                    const float heightScale = std::lerp(0.90f, 1.10f, hash01(bladeSeed + 1u));
                    append({bladePoint.x, *bladeGround, bladePoint.y}, normal,
                           bladeHeight * heightScale, hash01(bladeSeed + 2u), bladeYaw,
                           hash01(bladeSeed + 3u), bladeSeed, variation, *bladeMap);
                }
            } else {
                float height = std::min(0.48f + heightVariation * 0.32f, canopy);
                float width = std::min(0.42f, height * 0.65f);
                if (!flower) {
                    height = cardHeight;
                    width = cardWidth;
                } else if (yellowFlower) {
                    height = canopy * std::lerp(0.94f, 1.14f, heightVariation);
                    width = std::min(0.092f, height * 0.092f);
                }
                const float shade = hash01(seed + 9u);
                const float tilt = hash01(seed + 6u);
                append(root, normal, height, width, yaw, tilt, seed, shade, *map);
                append(root, normal, height, width, yaw + kPi * 0.5f, tilt, seed, shade, *map);
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
    if (terrain.grassTileCount() == 0) {
        for (auto& cache : cells_) cache.clear();
        anchor_ = anchor;
        return true;
    }

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
