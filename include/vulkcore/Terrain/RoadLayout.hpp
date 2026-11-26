#pragma once

#include "vulkcore/Terrain/Terrain.hpp"

namespace VulkCore {

// Read authored road rails from one YAML file or a directory of layout tiles.
// Translate is in world coordinates. The terrain provides the final elevation.
std::vector<TerrainRoadPath> loadTerrainRoadLayout(
    const std::filesystem::path& source, const std::atomic<bool>* cancelled = nullptr);

} // namespace VulkCore
