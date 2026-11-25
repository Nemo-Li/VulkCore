#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

#include "vulkcore/Renderer/Mesh.hpp"

namespace VulkCore {

struct SbfresTextureInfo {
    std::string name;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t layers = 0;
    std::uint16_t mipLevels = 0;
    std::uint32_t format = 0;
    std::array<std::uint8_t, 4> components{};
};

// Keeps the archive bytes so a single array layer can be decoded on demand.
class SbfresTextureArchive {
public:
    SbfresTextureArchive() = default;
    explicit SbfresTextureArchive(const std::filesystem::path& path);
    std::span<const SbfresTextureInfo> entries() const;
    // Reads BNTX array_index in material ID order. Empty if the mapping is absent.
    std::vector<std::uint32_t> readMaterialLayerIndices(std::size_t index) const;
    // Reads BNTX file names in the same material ID order.
    std::vector<std::string> readMaterialFileNames(std::size_t index) const;
    EmbeddedTextureData decodeLayer(std::size_t index, std::uint32_t layer) const;

private:
    struct Data;
    std::shared_ptr<const Data> data_;

    friend MeshData loadSbfresMesh(const std::filesystem::path&, const std::filesystem::path&,
        const std::filesystem::path&, bool, SbfresTextureArchive*);
};

// Loads the Switch BFRES 5.x subset. An omitted texture path probes the sibling
// <stem>.Tex<extension>; an optional animation path imports FSKA clips from a
// separate BFRES animation package. Preview callers can opt in to embedded clips
// and receive a browsable archive. In that mode array materials use fallback maps.
MeshData loadSbfresMesh(const std::filesystem::path& modelPath,
                        const std::filesystem::path& texturePath = {},
                        const std::filesystem::path& animationPath = {},
                        bool includeEmbeddedAnimations = false,
                        SbfresTextureArchive* previewTextures = nullptr);

enum class BfresContent { Model, SkeletalAnimation, Texture, Other };
BfresContent inspectBfresContent(const std::filesystem::path& path);
std::filesystem::path findBfresTextureSidecar(const std::filesystem::path& modelPath);
bool isBfresPath(const std::filesystem::path& path);
bool isBfresTextureSidecarPath(const std::filesystem::path& path);
bool isBfresAnimationPath(const std::filesystem::path& path);

} // namespace VulkCore
