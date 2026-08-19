#pragma once

#include <filesystem>

#include "vulkcore/Renderer/Mesh.hpp"

namespace VulkCore {

// Loads all meshes from a model into one indexed mesh and copies the source
// node hierarchy into MeshData::hierarchy. Assimp applies node transforms and
// triangulates faces so the render mesh can be uploaded directly to Vulkan
// vertex and index buffers.
MeshData loadModelMesh(const std::filesystem::path& path);

} // namespace VulkCore
