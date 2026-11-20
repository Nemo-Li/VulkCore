#pragma once

#include <filesystem>

#include "vulkcore/Renderer/Mesh.hpp"

namespace VulkCore {

// Loads the Switch BFRES 5.x subset. An omitted texture path probes the sibling
// <stem>.Tex<extension>; an optional animation path imports FSKA clips from a
// separate BFRES animation package.
MeshData loadSbfresMesh(const std::filesystem::path& modelPath,
                        const std::filesystem::path& texturePath = {},
                        const std::filesystem::path& animationPath = {});
bool isBfresPath(const std::filesystem::path& path);
bool isBfresTextureSidecarPath(const std::filesystem::path& path);
bool isBfresAnimationPath(const std::filesystem::path& path);

} // namespace VulkCore
