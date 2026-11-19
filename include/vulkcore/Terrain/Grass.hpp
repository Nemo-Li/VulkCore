#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

namespace VulkCore {

class TerrainScene;

enum class GrassLod : std::uint32_t {
    NearMesh,
    MidCluster,
    FarCard,
    FlowerCard,
    Count,
};

// Per-instance vertex stream shared by the curved blade and card templates.
struct GrassInstance {
    glm::vec4 rootHeight;
    glm::vec4 normalWidth;
    glm::vec4 motion; // yaw, lean, wind phase, color variation
    std::array<std::uint8_t, 4> tint; // RGB plus a map-presence flag
};

struct GrassDrawRange {
    GrassLod lod;
    std::uint32_t firstInstance;
    std::uint32_t instanceCount;
    glm::vec3 minimum;
    glm::vec3 maximum;
};

inline constexpr char kGrassVertexShader[] = "shader_grass.vert";
inline constexpr std::array<std::uint16_t, 21> kGrassIndices{
    0, 1, 2, 1, 3, 2, 2, 3, 4, 3, 5, 4, 4, 5, 6, 5, 7, 6, 6, 7, 8
};

glm::vec4 grassFadeDistances(GrassLod lod);

class GrassCoverage {
public:
    // Returns true when the active cells change. Distance fades and frustum
    // visibility use the actual camera every frame, including when stationary.
    bool update(const TerrainScene& terrain, glm::vec2 center, float viewPadding = 0.0f);

    const std::vector<GrassInstance>& instances() const { return instances_; }
    const std::vector<GrassDrawRange>& ranges() const { return ranges_; }

private:
    struct Cell {
        std::uint64_t lastUse = 0;
        std::vector<GrassInstance> instances;
        glm::vec3 minimum{0.0f};
        glm::vec3 maximum{0.0f};
    };
    using CellCoordinate = std::pair<int, int>;

    static Cell buildCell(const TerrainScene& terrain, GrassLod lod, CellCoordinate coordinate);

    std::optional<CellCoordinate> anchor_;
    std::uint64_t generation_ = 0;
    std::array<std::map<CellCoordinate, Cell>, static_cast<std::size_t>(GrassLod::Count)> cells_;
    std::vector<GrassInstance> instances_;
    std::vector<GrassDrawRange> ranges_;
};

} // namespace VulkCore
