#include "vulkcore/Terrain/Grass.hpp"

#include "vulkcore/Terrain/Terrain.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace VulkCore {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kGoldenAngle = 2.39996322972865332f;
constexpr float kGrassHeightScale = 1.15f;
constexpr int kNearInnerCompanionCount = 23;
constexpr int kNearOuterCompanionCount = 17;

std::uint32_t hashBits(std::uint32_t value) {
    value ^= value >> 16u;
    value *= 0x7FEB352Du;
    value ^= value >> 15u;
    value *= 0x846CA68Bu;
    value ^= value >> 16u;
    return value;
}

float hash01(std::uint32_t value) {
    return static_cast<float>(hashBits(value) & 0x00FFFFFFu) /
        static_cast<float>(0x01000000u);
}

std::uint32_t cellSeed(int x, int z, std::uint32_t seed) {
    const std::uint32_t ux = static_cast<std::uint32_t>(x);
    const std::uint32_t uz = static_cast<std::uint32_t>(z);
    return hashBits(seed ^ hashBits(ux * 0x9E3779B9u) ^ hashBits(uz * 0x85EBCA6Bu));
}

float smoothstep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

struct PlantingSample {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
};

std::optional<PlantingSample> samplePlantingSurface(
    const TerrainScene& terrain,
    const glm::vec2& position,
    float probeDistance
) {
    const std::optional<float> center = terrain.renderedHeightAtWorld(position);
    const std::optional<float> left = terrain.renderedHeightAtWorld(
        position - glm::vec2(probeDistance, 0.0f)
    );
    const std::optional<float> right = terrain.renderedHeightAtWorld(
        position + glm::vec2(probeDistance, 0.0f)
    );
    const std::optional<float> down = terrain.renderedHeightAtWorld(
        position - glm::vec2(0.0f, probeDistance)
    );
    const std::optional<float> up = terrain.renderedHeightAtWorld(
        position + glm::vec2(0.0f, probeDistance)
    );
    if (!center.has_value() || !left.has_value() || !right.has_value() ||
        !down.has_value() || !up.has_value()) {
        return std::nullopt;
    }

    glm::vec3 normal(
        *left - *right,
        probeDistance * 2.0f,
        *down - *up
    );
    if (glm::length(normal) < 0.0001f) {
        normal = glm::vec3(0.0f, 1.0f, 0.0f);
    } else {
        normal = glm::normalize(normal);
    }
    return PlantingSample{
        glm::vec3(position.x, *center + 0.012f, position.y),
        normal
    };
}

class GrassMeshBuilder {
public:
    explicit GrassMeshBuilder(GrassPatchMesh& mesh) : mesh_(mesh) {}

    void beginLod(GrassLod lod) {
        currentLod_ = lod;
        GrassLodMeshRange& range = mesh_.lods[static_cast<std::size_t>(lod)];
        range.indexOffset = mesh_.indices.size();
    }

    void endLod() {
        GrassLodMeshRange& range = mesh_.lods[static_cast<std::size_t>(currentLod_)];
        range.indexCount = mesh_.indices.size() - range.indexOffset;
    }

    void addNearBlade(
        const PlantingSample& surface,
        float yaw,
        float height,
        float width,
        float lean,
        float phase,
        float colorVariation
    ) {
        constexpr std::array<float, 4> kRowHeights = {0.0f, 0.28f, 0.58f, 0.82f};
        const glm::vec3 right(std::cos(yaw), 0.0f, std::sin(yaw));
        const glm::vec3 forward(-right.z, 0.0f, right.x);
        const glm::vec3 bladeNormal = glm::normalize(
            forward * 0.42f + surface.normal * 0.58f
        );
        const std::uint32_t baseVertex = static_cast<std::uint32_t>(mesh_.vertices.size());
        for (float t : kRowHeights) {
            const float halfWidth = width * 0.5f * (1.0f - t * 0.72f);
            const glm::vec3 center = surface.position +
                surface.normal * (height * t) +
                forward * (lean * height * t * t);
            appendVertex(
                center - right * halfWidth,
                bladeNormal,
                glm::vec2(0.0f, t),
                surface.position,
                phase,
                colorVariation
            );
            appendVertex(
                center + right * halfWidth,
                bladeNormal,
                glm::vec2(1.0f, t),
                surface.position,
                phase,
                colorVariation
            );
        }
        const glm::vec3 tip = surface.position + surface.normal * height +
            forward * (lean * height);
        appendVertex(
            tip,
            bladeNormal,
            glm::vec2(0.5f, 1.0f),
            surface.position,
            phase,
            colorVariation
        );

        for (std::uint32_t row = 0; row < 3; ++row) {
            const std::uint32_t first = baseVertex + row * 2u;
            mesh_.indices.insert(mesh_.indices.end(), {
                first, first + 1u, first + 2u,
                first + 1u, first + 3u, first + 2u
            });
        }
        mesh_.indices.insert(mesh_.indices.end(), {
            baseVertex + 6u, baseVertex + 7u, baseVertex + 8u
        });
        ++mesh_.lods[static_cast<std::size_t>(currentLod_)].primitiveCount;
    }

    void addTexturedCard(
        const PlantingSample& surface,
        float yaw,
        float height,
        float width,
        float phase,
        float colorVariation
    ) {
        const glm::vec3 right(std::cos(yaw), 0.0f, std::sin(yaw));
        const glm::vec3 forward(-right.z, 0.0f, right.x);
        const glm::vec3 bladeNormal = glm::normalize(
            forward * 0.32f + surface.normal * 0.68f
        );
        const std::uint32_t baseVertex = static_cast<std::uint32_t>(mesh_.vertices.size());
        for (const float t : {0.0f, 1.0f}) {
            const float halfWidth = width * 0.5f;
            const glm::vec3 rowCenter = surface.position + surface.normal * (height * t);
            appendVertex(
                rowCenter - right * halfWidth,
                bladeNormal,
                glm::vec2(0.0f, 1.0f - t),
                surface.position,
                phase,
                colorVariation
            );
            appendVertex(
                rowCenter + right * halfWidth,
                bladeNormal,
                glm::vec2(1.0f, 1.0f - t),
                surface.position,
                phase,
                colorVariation
            );
        }
        mesh_.indices.insert(mesh_.indices.end(), {
            baseVertex, baseVertex + 1u, baseVertex + 2u,
            baseVertex + 1u, baseVertex + 3u, baseVertex + 2u
        });
        ++mesh_.lods[static_cast<std::size_t>(currentLod_)].primitiveCount;
    }

private:
    void appendVertex(
        const glm::vec3& position,
        const glm::vec3& normal,
        const glm::vec2& uv,
        const glm::vec3& root,
        float phase,
        float colorVariation
    ) {
        Vertex vertex{};
        vertex.position = position;
        vertex.normal = normal;
        vertex.texCoord = uv;
        vertex.color = glm::vec4(
            colorVariation,
            1.0f - colorVariation,
            0.72f + colorVariation * 0.28f,
            1.0f
        );
        vertex.terrainMapping = glm::vec4(
            root.x,
            root.z,
            phase,
            static_cast<float>(currentLod_)
        );
        mesh_.boundsMinimum = glm::min(mesh_.boundsMinimum, position);
        mesh_.boundsMaximum = glm::max(mesh_.boundsMaximum, position);
        mesh_.vertices.push_back(vertex);
    }

    GrassPatchMesh& mesh_;
    GrassLod currentLod_ = GrassLod::NearMesh;
};

struct GrassPlant {
    PlantingSample surface;
    std::uint32_t seed = 0;
    int cellX = 0;
    int cellZ = 0;
    float centerDistance = 0.0f;
};

} // namespace

GrassPatchMesh buildBotwGrassPatch(
    const TerrainScene& terrain,
    const glm::vec3& center,
    const GrassPatchOptions& options
) {
    GrassPatchMesh mesh;
    mesh.boundsMinimum = glm::vec3(std::numeric_limits<float>::max());
    mesh.boundsMaximum = glm::vec3(std::numeric_limits<float>::lowest());
    if (options.radius <= 0.0f || options.plantingSpacing <= 0.05f) {
        return mesh;
    }

    std::vector<GrassPlant> plants;
    const int cellRadius = static_cast<int>(std::ceil(
        options.radius / options.plantingSpacing
    ));
    plants.reserve(static_cast<std::size_t>(cellRadius * cellRadius * 3));
    const float probeDistance = std::max(options.plantingSpacing * 0.45f, 0.18f);
    const float maximumTerrainHeight = std::max(terrain.header().maximumHeight, 1.0f);

    for (int z = -cellRadius; z <= cellRadius; ++z) {
        for (int x = -cellRadius; x <= cellRadius; ++x) {
            const std::uint32_t seed = cellSeed(x, z, options.seed);
            const glm::vec2 jitter(
                hash01(seed + 1u) - 0.5f,
                hash01(seed + 2u) - 0.5f
            );
            const glm::vec2 position = glm::vec2(center.x, center.z) +
                (glm::vec2(static_cast<float>(x), static_cast<float>(z)) +
                 jitter * 0.78f) * options.plantingSpacing;
            const float distance = glm::distance(position, glm::vec2(center.x, center.z));
            if (distance > options.radius) {
                continue;
            }

            const std::optional<PlantingSample> surface = samplePlantingSurface(
                terrain,
                position,
                probeDistance
            );
            if (!surface.has_value() || surface->normal.y < 0.76f) {
                continue;
            }

            // Match the broad terrain material logic: grass thins on steep
            // faces and above the snowy upper height band, with a low-frequency
            // field breaking up the remaining regular planting grid.
            const float slopeDensity = smoothstep(0.76f, 0.94f, surface->normal.y);
            const float normalizedHeight = std::clamp(
                surface->position.y / maximumTerrainHeight,
                0.0f,
                1.0f
            );
            const float altitudeDensity = 1.0f - smoothstep(0.66f, 0.84f, normalizedHeight);
            const float regionalNoise = 0.72f + 0.28f * std::sin(
                position.x * 0.091f + std::sin(position.y * 0.037f) +
                position.y * 0.053f
            );
            float density = std::clamp(
                slopeDensity * altitudeDensity * regionalNoise,
                0.0f,
                1.0f
            );
            // The playable near field is a continuous grass carpet. Keep the
            // biome masks for steep and snowy terrain, but remove random bare
            // cells near the player so companion blades cannot form islands.
            const float nearCoverage = 1.0f - smoothstep(
                options.denseRadius * 0.72f,
                options.denseRadius,
                distance
            );
            density = std::max(
                density,
                nearCoverage * slopeDensity * altitudeDensity * 0.985f
            );
            if (hash01(seed + 3u) > density) {
                continue;
            }
            plants.push_back({*surface, seed, x, z, distance});
        }
    }

    std::size_t estimatedNearBladeCount = 0;
    for (const GrassPlant& plant : plants) {
        ++estimatedNearBladeCount;
        if (plant.centerDistance < options.denseRadius * 0.72f) {
            estimatedNearBladeCount += kNearInnerCompanionCount;
        } else if (plant.centerDistance < options.denseRadius) {
            estimatedNearBladeCount += kNearOuterCompanionCount;
        }
    }
    mesh.vertices.reserve(estimatedNearBladeCount * 9u + plants.size() * 5u);
    mesh.indices.reserve(estimatedNearBladeCount * 21u + plants.size() * 8u);

    GrassMeshBuilder builder(mesh);
    builder.beginLod(GrassLod::NearMesh);
    for (const GrassPlant& plant : plants) {
        const float yaw = hash01(plant.seed + 10u) * 2.0f * kPi;
        const float height = (
            0.42f + hash01(plant.seed + 11u) * 0.30f
        ) * kGrassHeightScale;
        const float width = 0.045f + hash01(plant.seed + 12u) * 0.038f;
        const float lean = (hash01(plant.seed + 13u) - 0.5f) * 0.22f;
        const float phase = hash01(plant.seed + 14u);
        const float colorVariation = hash01(plant.seed + 15u);
        builder.addNearBlade(
            plant.surface,
            yaw,
            height,
            width,
            lean,
            phase,
            colorVariation
        );

        // Fill each accepted cell with a blue-noise-like disc of mesh blades.
        // The golden-angle sequence prevents clumps and radial spokes while
        // tangent-plane projection avoids five extra terrain queries per root.
        // The visible near ring reaches roughly 75-90 blades per square metre.
        int companionCount = 0;
        if (plant.centerDistance < options.denseRadius * 0.72f) {
            companionCount = kNearInnerCompanionCount;
        } else if (plant.centerDistance < options.denseRadius) {
            companionCount = kNearOuterCompanionCount;
        }
        for (int companion = 0; companion < companionCount; ++companion) {
            const std::uint32_t companionSeed = plant.seed +
                17u + static_cast<std::uint32_t>(companion) * 11u;
            const float normalizedRadius = std::sqrt(
                (static_cast<float>(companion) + 0.55f) /
                static_cast<float>(companionCount)
            );
            const float offsetYaw = hash01(plant.seed + 16u) * 2.0f * kPi +
                static_cast<float>(companion) * kGoldenAngle +
                (hash01(companionSeed) - 0.5f) * 0.34f;
            const float offsetDistance = (
                0.045f + normalizedRadius * options.plantingSpacing * 0.78f
            ) * (0.9f + hash01(companionSeed + 1u) * 0.2f);
            const glm::vec2 offset = glm::vec2(
                std::cos(offsetYaw),
                std::sin(offsetYaw)
            ) * offsetDistance;
            PlantingSample companionSurface = plant.surface;
            companionSurface.position.x += offset.x;
            companionSurface.position.z += offset.y;
            companionSurface.position.y -= (
                plant.surface.normal.x * offset.x +
                plant.surface.normal.z * offset.y
            ) / std::max(plant.surface.normal.y, 0.1f);
            builder.addNearBlade(
                companionSurface,
                yaw + 1.31f * static_cast<float>(companion + 1),
                height * (0.72f + hash01(companionSeed + 2u) * 0.46f),
                width * (0.88f + hash01(companionSeed + 3u) * 0.34f),
                -lean * (0.45f + hash01(companionSeed + 4u) * 0.55f),
                hash01(companionSeed + 5u),
                hash01(companionSeed + 6u)
            );
        }
    }
    builder.endLod();

    builder.beginLod(GrassLod::MidCluster);
    for (const GrassPlant& plant : plants) {
        if (hash01(plant.seed + 29u) > 0.72f) {
            continue;
        }
        const float baseYaw = hash01(plant.seed + 30u) * 2.0f * kPi;
        const float baseHeight = (
            0.52f + hash01(plant.seed + 31u) * 0.28f
        ) * kGrassHeightScale;
        for (int card = 0; card < 3; ++card) {
            const float cardPhase = static_cast<float>(card) / 3.0f;
            builder.addTexturedCard(
                plant.surface,
                baseYaw + cardPhase * kPi,
                baseHeight * (0.88f + 0.12f * static_cast<float>(card)),
                0.42f + hash01(plant.seed + 32u + static_cast<std::uint32_t>(card)) * 0.16f,
                hash01(plant.seed + 36u + static_cast<std::uint32_t>(card)),
                hash01(plant.seed + 40u + static_cast<std::uint32_t>(card))
            );
        }
    }
    builder.endLod();

    builder.beginLod(GrassLod::FarCard);
    for (const GrassPlant& plant : plants) {
        if (hash01(plant.seed + 49u) > 0.065f) {
            continue;
        }
        builder.addTexturedCard(
            plant.surface,
            hash01(plant.seed + 50u) * 2.0f * kPi,
            (0.48f + hash01(plant.seed + 51u) * 0.18f) * kGrassHeightScale,
            0.3f + hash01(plant.seed + 52u) * 0.12f,
            hash01(plant.seed + 53u),
            hash01(plant.seed + 54u)
        );
    }
    builder.endLod();

    builder.beginLod(GrassLod::FlowerCard);
    for (const GrassPlant& plant : plants) {
        if (plant.centerDistance > 30.0f || hash01(plant.seed + 60u) > 0.014f) {
            continue;
        }
        const float baseYaw = hash01(plant.seed + 61u) * 2.0f * kPi;
        const float height = 0.48f + hash01(plant.seed + 62u) * 0.32f;
        const float width = height * (0.48f + hash01(plant.seed + 63u) * 0.12f);
        // Two crossed cards keep each sparse flower cluster readable from any
        // player heading without turning it into a camera-facing billboard.
        for (int card = 0; card < 2; ++card) {
            builder.addTexturedCard(
                plant.surface,
                baseYaw + static_cast<float>(card) * kPi * 0.5f,
                height,
                width,
                hash01(plant.seed + 64u),
                hash01(plant.seed + 65u)
            );
        }
    }
    builder.endLod();

    if (mesh.vertices.empty()) {
        mesh.boundsMinimum = glm::vec3(0.0f);
        mesh.boundsMaximum = glm::vec3(0.0f);
    }
    return mesh;
}

} // namespace VulkCore
