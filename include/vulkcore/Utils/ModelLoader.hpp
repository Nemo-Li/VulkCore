#pragma once

#include <filesystem>

#include "vulkcore/Renderer/Mesh.hpp"

namespace VulkCore {

// Loads all meshes from a model into one indexed mesh and copies the source
// node hierarchy into MeshData::hierarchy. Node transforms are applied while
// flattening the render mesh, while each ModelNode also keeps its global
// transform so local node animation can be applied at draw time. Each node
// keeps the corresponding MeshPart indices for its directly attached meshes.
// Skinning bones and Assimp node-animation keyframes are also copied into
// MeshData for resource inspection and later animation playback.
MeshData loadModelMesh(const std::filesystem::path& path);

} // namespace VulkCore
