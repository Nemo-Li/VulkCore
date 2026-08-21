#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "vulkcore/Renderer/Vertex.hpp"

namespace VulkCore {

class TerrainScene;

// The three representations overlap slightly. The grass shader uses a
// screen-door dissolve in those overlap bands, so changing representation
// does not produce a hard ring around the camera.
enum class GrassLod : std::size_t {
    NearMesh = 0,
    MidCluster,
    FarCard,
    FlowerCard,
    Count,
};

struct GrassLodMeshRange {
    std::size_t indexOffset = 0;
    std::size_t indexCount = 0;
    std::uint32_t primitiveCount = 0;
};

struct GrassPatchOptions {
    float radius = 40.0f;
    float plantingSpacing = 0.52f;
    float denseRadius = 22.0f;
    std::uint32_t seed = 0xB07F123u;
};

struct GrassPatchMesh {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::array<GrassLodMeshRange, static_cast<std::size_t>(GrassLod::Count)> lods{};
    glm::vec3 boundsMinimum{0.0f};
    glm::vec3 boundsMaximum{0.0f};
};

// Builds a deterministic BOTW-inspired grass patch on the rendered terrain
// surface. Near blades are curved triangle meshes; the lower LODs use grouped
// ribbons and sparse cards while retaining the same shader deformation ABI.
GrassPatchMesh buildBotwGrassPatch(
    const TerrainScene& terrain,
    const glm::vec3& center,
    const GrassPatchOptions& options = {}
);

} // namespace VulkCore
