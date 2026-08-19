#pragma once

#include <filesystem>

#include "vulkcore/Renderer/Mesh.hpp"

namespace VulkCore {

// Loads all meshes from a model into one indexed mesh and copies the source
// node hierarchy into MeshData::hierarchy. Node transforms are applied while
// flattening the render mesh, and each ModelNode keeps the corresponding
// MeshPart indices for its directly attached meshes.
MeshData loadModelMesh(const std::filesystem::path& path);

} // namespace VulkCore
