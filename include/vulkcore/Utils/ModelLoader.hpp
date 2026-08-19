#pragma once

#include <filesystem>

#include "vulkcore/Renderer/Mesh.hpp"

namespace VulkCore {

// Loads all meshes from a model into one indexed mesh. Assimp applies node
// transforms and triangulates faces so the result can be uploaded directly to
// Vulkan vertex and index buffers.
MeshData loadModelMesh(const std::filesystem::path& path);

} // namespace VulkCore
