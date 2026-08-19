#pragma once

#include <cstdint>
#include <vector>

#include "vulkcore/Renderer/Vertex.hpp"

namespace VulkCore {

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

} // namespace VulkCore
