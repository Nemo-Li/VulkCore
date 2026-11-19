#pragma once

#include <filesystem>

#include "vulkcore/Renderer/Mesh.hpp"

namespace VulkCore {

// Loads the static Switch BFRES 5.x subset. An omitted texture path probes
// the sibling <stem>.Tex<extension>; use an explicit path to require a pack.
MeshData loadSbfresMesh(const std::filesystem::path& modelPath,
                        const std::filesystem::path& texturePath = {});
bool isBfresPath(const std::filesystem::path& path);
bool isBfresTextureSidecarPath(const std::filesystem::path& path);

} // namespace VulkCore
