#include "vulkcore/Renderer/Frustum.hpp"
#include "vulkcore/Terrain/Grass.hpp"
#include "vulkcore/Terrain/Terrain.hpp"
#include "vulkcore/Utils/SbfresLoader.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Fixture {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("vulkcore-terrain-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    void create(bool missingCorner, bool mixedLod = false) const {
        const std::size_t count = mixedLod ? 2 : (missingCorner ? 3 : 4);
        constexpr std::size_t lookup = 0x4c;
        const std::size_t entries = lookup + count * 4;
        std::vector<char> tscb(entries + count * 0x40, 0);
        const auto u32 = [&](std::size_t offset, std::uint32_t value) {
            for (int byte = 0; byte < 4; ++byte) {
                tscb[offset + byte] = static_cast<char>(value >> (24 - byte * 8));
            }
        };
        const auto f32 = [&](std::size_t offset, float value) {
            u32(offset, std::bit_cast<std::uint32_t>(value));
        };
        std::copy_n("TSCB", 4, tscb.begin());
        u32(0x04, 0x0a000000);
        u32(0x08, 1);
        f32(0x10, 500.0f);
        f32(0x14, 800.0f);
        u32(0x18, 1);
        u32(0x1c, static_cast<std::uint32_t>(count));
        f32(0x28, 32.0f);
        u32(0x30, lookup - 0x30);
        u32(0x34, 4); // Material lookup points at the record at 0x38.
        f32(0x3c, 1.0f);
        f32(0x40, 1.0f);
        f32(0x48, 1.0f);
        const auto directory = root / "terrain" / "5400000000.hght";
        std::filesystem::create_directories(directory);
        for (std::size_t tile = 0; tile < count; ++tile) {
            const std::size_t entry = entries + tile * 0x40;
            u32(lookup + tile * 4, static_cast<std::uint32_t>(entry - lookup - tile * 4));
            const int tileX = static_cast<int>(tile % 2);
            const int tileZ = static_cast<int>(tile / 2);
            f32(entry, mixedLod && tile == 1 ? 0.5f : 1.0f + tileX * 2.0f);
            f32(entry + 4, mixedLod && tile == 1 ? 0.5f : 1.0f + tileZ * 2.0f);
            f32(entry + 8, mixedLod && tile == 1 ? 1.0f : 2.0f);
            f32(entry + 0x10, 1.0f);
            u32(entry + 0x20, 0x10);
            const std::string name = mixedLod && tile == 0
                ? "5300000000" : "540000000" + std::to_string(tile);
            std::copy(name.begin(), name.end(), tscb.begin() + entry + 0x30);
            std::ofstream heightmap((mixedLod ? root : directory) / (name + ".hght"), std::ios::binary);
            for (int z = 0; z < 256; ++z) {
                for (int x = 0; x < 256; ++x) {
                    int value = 8000 + (tileX * 255 + x) * 3 +
                        (tileZ * 255 + z) * 5 + tileX * 40000;
                    if (mixedLod) {
                        value = tile == 0 ? 10000 : 20000;
                    } else if (tile == 0 && x == 100 && z == 100) {
                        value = 65000; // An isolated, unsupported peak.
                    }
                    const std::array<char, 2> bytes = {
                        static_cast<char>(value & 255), static_cast<char>(value >> 8)
                    };
                    heightmap.write(bytes.data(), bytes.size());
                }
            }
        }
        std::ofstream scene(root / "MainField.tscb", std::ios::binary);
        scene.write(tscb.data(), static_cast<std::streamsize>(tscb.size()));
    }
};

void checkFixture(bool missingCorner) {
    Fixture fixture;
    fixture.create(missingCorner);
    const auto scene = VulkCore::TerrainScene::load(fixture.root / "MainField.tscb", fixture.root);
    require(scene->tileCount() == (missingCorner ? 3u : 4u), "fixture tiles were not loaded");
    std::map<std::pair<int, int>, const VulkCore::TerrainTile*> tiles;
    for (const auto& chunk : scene->chunks()) {
        for (const auto& tile : chunk.tiles()) {
            tiles[{static_cast<int>(tile.metadata().gridPosition.x),
                static_cast<int>(tile.metadata().gridPosition.y)}] = &tile;
        }
    }
    int maximumJump = 0;
    for (const auto& [coordinate, tile] : tiles) {
        const auto& heights = tile->heights();
        for (std::size_t z = 0; z < 256; ++z) {
            for (std::size_t x = 0; x < 256; ++x) {
                const int value = heights[z * 256 + x];
                if (x < 255) {
                    maximumJump = std::max(maximumJump, std::abs(value - heights[z * 256 + x + 1]));
                }
                if (z < 255) {
                    maximumJump = std::max(maximumJump, std::abs(value - heights[(z + 1) * 256 + x]));
                }
            }
        }
        const auto right = tiles.find({coordinate.first + 2, coordinate.second});
        const auto below = tiles.find({coordinate.first, coordinate.second + 2});
        for (std::size_t sample = 0; sample < 256; ++sample) {
            if (right != tiles.end()) {
                require(heights[sample * 256 + 255] == right->second->heights()[sample * 256],
                    "horizontal tile edge has a height discontinuity");
            }
            if (below != tiles.end()) {
                require(heights[255 * 256 + sample] == below->second->heights()[sample],
                    "vertical tile edge has a height discontinuity");
            }
        }
    }
    std::cout << "Maximum cliff sample difference (" << scene->tileCount()
        << " tiles): " << maximumJump << '\n';
    // The step is 40000 samples. Even an incomplete corner must reduce the
    // largest adjacent difference by at least 70%, without lowering plateaus.
    require(maximumJump < 12000, "the continuous cliff was not smoothed across tiles");
    const auto& first = *tiles.at({1, 1});
    require(std::abs(static_cast<int>(first.heights()[100 * 256 + 100]) - 8800) <= 2,
        "isolated peak was not repaired");
    require(first.heights()[64 * 256 + 64] == 8512, "smoothing changed a supported planar slope");
    require(tiles.at({3, 1})->heights()[64 * 256 + 64] == 49277,
        "smoothing changed the distant plateau");

    const auto mesh = scene->buildMesh();
    constexpr std::size_t rowSize = 129;
    require(mesh.vertices.size() == scene->tileCount() * rowSize * rowSize,
        "smoothing unexpectedly changed mesh density");
    for (const auto& vertex : mesh.vertices) {
        // The fixture spans 1250 world units on each axis. Interior seams
        // must keep their global UV (e.g. U=0.5), regardless of chunk or tile.
        const glm::vec2 macroUv(vertex.terrainMapping.z, vertex.terrainMapping.w);
        const glm::vec2 expectedUv = glm::vec2(vertex.position.x, vertex.position.z) / 1250.0f;
        require(glm::distance(macroUv, expectedUv) < 0.00001f,
            "macro UV restarted at a tile boundary");
        require(std::isfinite(vertex.normal.x) && std::isfinite(vertex.normal.y) &&
            std::isfinite(vertex.normal.z) && std::abs(glm::length(vertex.normal) - 1.0f) < 0.0001f,
            "mesh contains an invalid normal");
        const auto height = scene->renderedHeightAtWorld({vertex.position.x, vertex.position.z});
        require(height.has_value() && std::abs(*height - vertex.position.y) < 0.002f,
            "rendered height query disagrees with mesh geometry");
    }
    // An interior normal must come from source gradients, independent of the
    // stride-two triangle diagonals, including along the rounded cliff edge.
    std::vector<VulkCore::Vertex> tileVertices;
    std::vector<std::uint32_t> tileIndices;
    first.appendMesh(2, scene->header().worldScale, scene->header().maximumHeight,
        tileVertices, tileIndices);
    const std::size_t probe = 64 * rowSize + 126;
    const auto samePosition = std::find_if(mesh.vertices.begin(), mesh.vertices.end(), [&](const auto& vertex) {
        return glm::distance(vertex.position, tileVertices[probe].position) < 0.0001f;
    });
    require(samePosition != mesh.vertices.end() &&
        glm::dot(samePosition->normal, tileVertices[probe].normal) > 0.99999f,
        "stitching replaced heightfield normals with triangle normals");
    std::cout << "PASS: " << scene->tileCount() << " tiles, continuous edges and corners, "
        << "peak repair, cliff smoothing, preserved slopes, consistent mesh heights/normals\n";
}

void checkStitchedChunkBounds() {
    Fixture fixture;
    fixture.create(false, true);
    const auto scene = VulkCore::TerrainScene::load(fixture.root / "MainField.tscb", fixture.root);
    const auto mesh = scene->buildMesh();
    require(mesh.chunks.size() == 2, "mixed LOD fixture must produce two chunks");
    require(mesh.chunks.front().boundsMaximum.y > scene->chunks().front().boundsMaximum().y,
        "stitched overlap must raise the coarse chunk's upper bound");
    for (const auto& chunk : mesh.chunks) {
        for (std::size_t i = chunk.indexOffset; i < chunk.indexOffset + chunk.indexCount; ++i) {
            const glm::vec3 position = mesh.vertices[mesh.indices[i]].position;
            require(glm::all(glm::greaterThanEqual(position, chunk.boundsMinimum)) &&
                glm::all(glm::lessThanEqual(position, chunk.boundsMaximum)),
                "chunk bounds omit a stitched mesh vertex");
        }
    }
}

void checkArchiveMaterialLayers() {
    const auto archivePath = std::filesystem::path(__FILE__).parent_path().parent_path() /
        "assets/terrain/source/Terrain.Tex.sbfres";
    const VulkCore::SbfresTextureArchive archive(archivePath);
    const auto entries = archive.entries();
    constexpr std::array<std::uint32_t, 88> expected{
         0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
        16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 17, 18,  0,
        29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44,
        45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59,  7,
        60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71,  0, 72, 73, 74,
        75, 76, 77, 78, 79, 80, 81, 82,
    };
    for (const std::string_view name : {"MaterialAlb", "MaterialCmb"}) {
        const auto entry = std::find_if(entries.begin(), entries.end(), [name](const auto& texture) {
            return texture.name == name;
        });
        require(entry != entries.end() && entry->layers == 83, "expected 83 terrain texture layers");
        const auto index = static_cast<std::size_t>(entry - entries.begin());
        const auto layers = archive.readMaterialLayerIndices(index);
        require(std::ranges::equal(layers, expected), "terrain archive material IDs must use array_index");
        const auto files = archive.readMaterialFileNames(index);
        require(files.size() == layers.size() && files[0] == "Plant_GreenGrassField_A" &&
            files[17] == "Rock_LargeCliff_A" && files[17] == files[29] &&
            files[87] == "Rock_RockBeachCoral_A",
            "terrain archive rock classification must follow file metadata in material ID order");
    }
}

void checkMaterialMaps() {
    using namespace VulkCore;
    Fixture fixture;
    fixture.create(false);
    const auto root = fixture.root / "terrain";
    const auto group = root / "5400000000.mate";
    std::filesystem::create_directories(group);
    const auto writeMap = [](const std::filesystem::path& path, const std::array<unsigned char, 4>& values) {
        std::ofstream file(path, std::ios::binary);
        for (std::size_t i = 0; i < kTerrainMaterialResolution * kTerrainMaterialResolution; ++i) {
            file.write(reinterpret_cast<const char*>(values.data()), values.size());
        }
    };
    writeMap(group / "5400000000.mate", {73, 3, 64, 17});
    writeMap(root / "5400000001.mate", {12, 3, 0, 0});
    {
        std::ofstream malformed(root / "5400000002.mate", std::ios::binary);
        malformed.put('\0');
    }
    const auto scene = TerrainScene::load(fixture.root / "MainField.tscb", fixture.root);
    require(scene->tileCount() == 4 && scene->materialTileCount() == 2,
        "height tiles must load independently of missing and malformed material grids");

    std::array<int, 256> albedos;
    albedos.fill(-1);
    albedos[3] = 0;
    const auto incomplete = scene->buildMaterialLayers(albedos);
    require(incomplete.count == 1 && incomplete.layerForTile == std::vector<int>{-1, 0} &&
        incomplete.missingIds == std::vector<std::uint8_t>{73},
        "missing active IDs must hide their tiles, while zero-weight IDs have no effect");
    albedos[73] = 1;
    const auto complete = scene->buildMaterialLayers(albedos);
    constexpr std::size_t padded = kTerrainMaterialPaddedResolution;
    const auto sample = [&](std::size_t layer, std::size_t x, std::size_t z) {
        return complete.pixels.begin() + (layer * padded * padded + z * padded + x) * 4;
    };
    const std::array<std::uint8_t, 4> firstSample{1, 0, 64, 17};
    const std::array<std::uint8_t, 4> secondSample{0, 0, 0, 0};
    require(complete.count == 2 && complete.missingIds.empty() &&
        std::equal(firstSample.begin(), firstSample.end(), sample(0, 1, 1)),
        "material data must preserve arbitrary IDs, blend weights and the unknown channel");
    require(std::equal(secondSample.begin(), secondSample.end(), sample(0, padded - 1, padded / 2)) &&
        std::equal(firstSample.begin(), firstSample.end(), sample(1, 0, padded / 2)),
        "both sides of a shared edge must include the neighboring sample");
    require(std::equal(firstSample.begin(), firstSample.end(), sample(0, 0, padded / 2)),
        "an outer edge must clamp to its nearest sample");

    std::array<glm::vec4, 256> palette;
    palette.fill(glm::vec4(0.0f));
    palette[0] = {0.0f, 0.0f, 1.0f, 0.0f};
    palette[1] = {1.0f, 0.0f, 0.0f, 1.0f};
    const auto macro = scene->bakeMacroAlbedo(complete, palette);
    const auto macroPixel = [&](std::uint32_t x, std::uint32_t z) {
        return macro.pixels.data() + (static_cast<std::size_t>(z) * macro.width + x) * 4;
    };
    require(macro.width == 313 && macro.height == 313, "macro map must cover the entire world extent");
    const auto* boundary = macroPixel(macro.width / 2, macro.height / 4);
    require(boundary[0] == 32 && boundary[2] == 223 && boundary[3] == 255,
        "global macro albedo must filter neighboring tiles in linear color");
    const auto* gap = macroPixel(macro.width / 4, 3 * macro.height / 4);
    require(gap[3] == 0, "missing tiles must have zero macro coverage");
    require(macro.rockCoverage.size() == macro.pixels.size(),
        "rock coverage must use the same grid as macro albedo");
    const auto edgeOffset = boundary - macro.pixels.data();
    const auto gapOffset = gap - macro.pixels.data();
    const auto plainOffset = macroPixel(3 * macro.width / 4, macro.height / 4) - macro.pixels.data();
    require(macro.rockCoverage[edgeOffset] == 32 && macro.rockCoverage[edgeOffset + 3] == 255 &&
        macro.rockCoverage[plainOffset] == 0 && macro.rockCoverage[plainOffset + 3] == 255 &&
        macro.rockCoverage[gapOffset] == 0 && macro.rockCoverage[gapOffset + 3] == 0,
        "rock coverage must filter material weights across tiles and retain distinct hole coverage");

    auto mesh = scene->buildMesh({8});
    const auto heightIndexCount = mesh.indices.size();
    mesh.selectMaterialTiles(incomplete.layerForTile);
    require(mesh.indices.size() * 4 == heightIndexCount && !mesh.chunks.empty(),
        "only the complete material tile may be submitted for rendering");
    require(std::all_of(mesh.indices.begin(), mesh.indices.end(), [&](std::uint32_t index) {
        return mesh.vertices[index].terrainMapping.x == 0.0f && mesh.vertices[index].position.x >= 625.0f;
    }), "filtered triangles must keep their remapped material layer");
}

void checkGrassCoverage() {
    using namespace VulkCore;
    Fixture fixture;
    fixture.create(false);
    const auto directory = fixture.root / "terrain_grass";
    const auto group = directory / "5400000000.grass.extm";
    std::filesystem::create_directories(group);
    const auto greenPath = group / "5400000000.grass.extm";
    const auto writeMap = [](const std::filesystem::path& path, std::array<unsigned char, 4> sample,
                             std::array<unsigned char, 4> edgeSample) {
        std::ofstream file(path, std::ios::binary);
        for (std::uint32_t z = 0; z < kTerrainGrassResolution; ++z) {
            for (std::uint32_t x = 0; x < kTerrainGrassResolution; ++x) {
                const auto& pixel = x < kTerrainGrassResolution / 2u ? sample : edgeSample;
                file.write(reinterpret_cast<const char*>(pixel.data()), pixel.size());
            }
        }
    };
    writeMap(greenPath, {128, 24, 150, 60}, {0, 255, 0, 0});
    writeMap(directory / "5400000001.grass.extm", {64, 180, 72, 16}, {1, 0, 0, 0});

    const auto scene = TerrainScene::load(fixture.root / "MainField.tscb", fixture.root);
    require(scene->grassTileCount() == 2, "flat and grouped grass maps must both load");
    const auto green = scene->grassAtWorld({96.0f, 96.0f});
    const auto interpolation = scene->grassAtWorld({312.5f, 96.0f});
    const auto bare = scene->grassAtWorld({400.0f, 96.0f});
    require(green && std::abs(green->height - 128.0f / 255.0f) < 0.00001f &&
        std::abs(green->color.g - 150.0f / 255.0f) < 0.00001f,
        "grass height and tint decode incorrectly");
    require(interpolation && std::abs(interpolation->height - 64.0f / 255.0f) < 0.00001f &&
        glm::distance(interpolation->color, green->color) < 0.00001f,
        "empty map cells must fade height without tinting nearby grass");
    require(bare && bare->height == 0.0f, "zero-height map cells must remain bare");
    require(!scene->grassAtWorld({96.0f, 900.0f}), "missing and empty grass data must differ");

    const auto surface = scene->bakeGrassSurface();
    const auto surfacePixel = [&](unsigned x, unsigned z) {
        const auto offset = (static_cast<std::size_t>(z) * surface.width + x) * 4;
        const auto* pixel = surface.pixels.data() + offset;
        return std::array{pixel[0], pixel[1], pixel[2], pixel[3]};
    };
    require(surface.width == 313 && surface.height == 313 &&
        surfacePixel(24, 24) == std::array<std::uint8_t, 4>{24, 150, 60, 255} &&
        surfacePixel(100, 24)[3] == 0 && surfacePixel(24, 225)[3] == 0 &&
        surfacePixel(295, 24)[3] == 0,
        "grass ground cover must keep planted tint and omit bare, missing and effectively empty grass");

    GrassCoverage grass;
    require(grass.update(*scene, {96.0f, 96.0f}), "initial grass coverage must populate");
    std::size_t cardsInCell = 0;
    for (const auto& range : grass.ranges()) {
        if (range.lod != GrassLod::CrossCard) continue;
        for (std::uint32_t i = 0; i < range.instanceCount; ++i) {
            const auto& root = grass.instances()[range.firstInstance + i].rootHeight;
            if (root.x >= 128.0f && root.x < 144.0f && root.z >= 96.0f && root.z < 112.0f) ++cardsInCell;
        }
    }
    require(cardsInCell == 2 * 40 * 40, "one fully covered cell must contain the dense LOD1 grid");
    const auto nearest = [&](glm::vec2 position, GrassLod lod) -> const GrassInstance* {
        const GrassInstance* bestInstance = nullptr;
        float distance = std::numeric_limits<float>::max();
        for (const auto& range : grass.ranges()) {
            if (range.lod != lod) continue;
            for (std::uint32_t i = 0; i < range.instanceCount; ++i) {
                const auto& instance = grass.instances()[range.firstInstance + i];
                const float next = glm::distance(
                    glm::vec2(instance.rootHeight.x, instance.rootHeight.z), position
                );
                if (next < distance) {
                    distance = next;
                    bestInstance = &instance;
                }
            }
        }
        return bestInstance;
    };
    const auto* near = nearest({102.0f, 94.0f}, GrassLod::NearMesh);
    require(near && near->rootHeight.w > 0.0f &&
        near->mapColor == std::array<std::uint8_t, 4>{24, 150, 60, 255},
        "mesh blades must carry both map height and RGB");
    const auto* mappedCard = nearest({132.0f, 96.0f}, GrassLod::CrossCard);
    require(mappedCard && mappedCard->mapColor == near->mapColor &&
        (mappedCard + 1)->mapColor == mappedCard->mapColor,
        "both cross cards must carry the same RGB as nearby mesh blades");
    const auto startingRoot = near->rootHeight;
    const auto* flower = nearest({96.0f, 96.0f}, GrassLod::YellowFlowerCard);
    require(flower && flower->normalWidth.w > 0.0f && flower->normalWidth.w <= 0.092f,
        "vegetated grass maps must support small yellow flowers");
    const auto* wildflower = nearest({96.0f, 96.0f}, GrassLod::FlowerCard);
    require(wildflower && wildflower->rootHeight.w > 0.0f,
        "both flower layers must be able to grow on vegetated grass maps");
    require(flower + 1 < grass.instances().data() + grass.instances().size(),
        "each flower must have a second card");
    const auto& otherCard = *(flower + 1);
    require(otherCard.rootHeight == flower->rootHeight &&
        std::abs(otherCard.motion.x - flower->motion.x - glm::half_pi<float>()) < 0.00001f &&
        otherCard.motion.z == flower->motion.z,
        "the two flower cards must share their root and wind phase");
    const auto nearFade = grassFadeDistances(GrassLod::NearMesh);
    const auto crossFade = grassFadeDistances(GrassLod::CrossCard);
    require(nearFade.z == 30.0f && nearFade.w == 40.0f &&
        crossFade.x == nearFade.z && crossFade.y == nearFade.w && crossFade.z > crossFade.y,
        "the grass cross cards must cover the mesh fade range");
    require(grassLodForLayer(1) == GrassLod::CrossCard && !grassLodForLayer(2) &&
        grassLodForLayer(3) == GrassLod::FlowerCard && grassLodForLayer(4) == GrassLod::YellowFlowerCard,
        "legacy and yellow flowers must use distinct, stable node IDs");
    const auto* expandedNear = nearest({124.0f, 96.0f}, GrassLod::NearMesh);
    require(expandedNear && std::abs(expandedNear->rootHeight.x - 124.0f) < 1.0f,
        "mesh blades must populate the start of the card handoff");
    require(!grass.update(*scene, {97.0f, 97.0f}), "movement within an anchor must retain buffers");
    require(grass.update(*scene, {107.0f, 96.0f}) &&
        nearest({102.0f, 94.0f}, GrassLod::NearMesh)->rootHeight == startingRoot,
        "retained grass roots must not jump when the coverage shifts");
    require(grass.update(*scene, {435.0f, 96.0f}) &&
        nearest({435.0f, 96.0f}, GrassLod::NearMesh) == nullptr &&
        nearest({435.0f, 96.0f}, GrassLod::FlowerCard) == nullptr &&
        nearest({435.0f, 96.0f}, GrassLod::YellowFlowerCard) == nullptr,
        "moving onto mapped bare ground must not generate local grass or flowers");
    const auto* distantCard = nearest({435.0f, 96.0f}, GrassLod::CrossCard);
    require(!distantCard || std::abs(distantCard->rootHeight.x - 435.0f) > 60.0f,
        "LOD1 roots must stay outside the bare patch even when its neighbors are in range");
    require(grass.update(*scene, {900.0f, 96.0f}), "long movement must update grass coverage");
    near = nearest({900.0f, 96.0f}, GrassLod::NearMesh);
    require(near && near->rootHeight.w < startingRoot.w &&
        std::abs(near->rootHeight.x - 900.0f) < 2.0f &&
        near->mapColor == std::array<std::uint8_t, 4>{180, 72, 16, 255},
        "grass coverage must follow long movement and adopt the new map");
    require(grass.update(*scene, {1180.0f, 96.0f}), "the short grass area must update coverage");
    const auto* shortGrass = nearest({1180.0f, 96.0f}, GrassLod::NearMesh);
    require(shortGrass && shortGrass->mapColor == std::array<std::uint8_t, 4>{0, 0, 0, 255} &&
        shortGrass->rootHeight.w > 0.0f && shortGrass->rootHeight.w < 0.01f,
        "near grass must retain black RGB and the smallest positive grass map height");
    const auto& root = shortGrass->rootHeight;
    const auto ground = scene->renderedHeightAtWorld({root.x, root.z});
    require(ground && root.y >= *ground && root.y - *ground < root.w * 0.1f,
        "tiny blades must sit on the rendered ground");
    require(!nearest({1180.0f, 96.0f}, GrassLod::CrossCard),
        "tufts too small to cover the LOD1 grid must not populate the card buffer");

    require(grass.update(*scene, {96.0f, 900.0f}) && grass.instances().empty() && grass.ranges().empty(),
        "terrain without a grass map must not spawn fallback grass or flowers");

    const GrassCoverMask cover(4, 2, {127, 255, 255, 127, 127, 255, 255, 127});
    require(cover.contains({6.0f, 6.0f}) && cover.contains({-6.0f, -6.0f}),
        "a bright block center must survive all rotations, including in negative coordinates");
    int coveredCount = 0;
    int otherBlockDifferences = 0;
    for (int z = 0; z < 24; ++z) {
        for (int x = 0; x < 24; ++x) {
            const glm::vec2 point = (glm::vec2(x, z) + 0.5f) * 0.5f;
            const bool covered = cover.contains(point);
            coveredCount += covered;
            otherBlockDifferences += covered != cover.contains(point - glm::vec2(24.0f, 12.0f));
        }
    }
    require(coveredCount > 24 * 24 * 3 / 4 && otherBlockDifferences > 0,
        "three independently rotated masks must expand coverage and vary between blocks");
    const GrassCoverMask dark(1, 1, {127});
    require(!dark.contains({1.0f, -6.0f}), "three below-threshold samples must stay uncovered");
    grass.setCoverMask(cover);
    require(grass.update(*scene, {96.0f, 620.0f}), "replacing the cover must rebuild cached grass roots");
    std::array<std::size_t, 2> coverageCounts{};
    for (const auto& range : grass.ranges()) {
        for (std::uint32_t i = 0; i < range.instanceCount; ++i) {
            const auto& root = grass.instances()[range.firstInstance + i].rootHeight;
            const auto map = scene->grassAtWorld({root.x, root.z});
            require(map && map->height > 0.0f, "no instance may cross onto unmapped or bare ground");
            if (!isGrassVegetation(range.lod)) continue;
            if (range.lod == GrassLod::NearMesh) {
                require(cover.contains({root.x, root.z}), "near grass must use the rotated mask union at every root");
            }
            ++coverageCounts[range.lod == GrassLod::NearMesh ? 0 : 1];
        }
    }
    require(coverageCounts[0] > 0 && coverageCounts[1] > 0, "the paths must contain mesh blades and cross cards");
    grass.setCoverMask(dark);
    require(grass.update(*scene, {96.0f, 620.0f}) &&
        std::ranges::none_of(grass.ranges(), [](const auto& range) { return range.lod == GrassLod::NearMesh; }) &&
        std::ranges::any_of(grass.ranges(), [](const auto& range) { return range.lod == GrassLod::CrossCard; }),
        "the fine rotated mask must not punch holes in the card canopy");
    grass.setCoverMask(cover);
    grass.update(*scene, {96.0f, 620.0f});
    const auto* cross = nearest({126.0f, 604.0f}, GrassLod::CrossCard);
    require(cross && (cross + 1)->rootHeight == cross->rootHeight &&
        std::abs((cross + 1)->motion.x - cross->motion.x - glm::half_pi<float>()) < 0.00001f,
        "LOD1 must draw two perpendicular cards at one root");
    const auto stableCardRoot = cross->rootHeight;
    require(grass.update(*scene, {96.0f, 900.0f}) && grass.instances().empty() &&
        grass.update(*scene, {96.0f, 620.0f}), "leaving the mask blocks must evict and rebuild them");
    cross = nearest({126.0f, 604.0f}, GrassLod::CrossCard);
    require(cross && cross->rootHeight == stableCardRoot,
        "rotated cover must make the same placement decisions after caches are rebuilt");

    std::filesystem::resize_file(greenPath, 100);
    bool rejected = false;
    try {
        TerrainScene::load(fixture.root / "MainField.tscb", fixture.root);
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find("grass map") != std::string::npos;
    }
    require(rejected, "a truncated grass map must fail with a clear error");
    std::cout << "PASS: grass map height/RGB, missing and bare ground, rotated cover and stable roots\n";
}

void checkFrustum() {
    using VulkCore::isOutsideVulkanFrustum;
    const glm::mat4 identity(1.0f);
    require(isOutsideVulkanFrustum(identity, {-0.2f, -0.2f, -0.8f}, {0.2f, 0.2f, -0.2f}),
        "Vulkan near clip boundary is z = 0");
    require(!isOutsideVulkanFrustum(identity, {-0.2f, -0.2f, -0.2f}, {0.2f, 0.2f, 0.2f}),
        "a chunk spanning the near plane must remain visible");
    require(!isOutsideVulkanFrustum(identity, {-2.0f, -2.0f, 0.5f}, {2.0f, 2.0f, 0.7f}),
        "a box covering the viewport is visible even with all corners outside");
    require(!isOutsideVulkanFrustum(identity, {1.0f, -0.2f, 0.1f}, {2.0f, 0.2f, 0.9f}),
        "a box touching a side plane must remain visible");

    const glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(45.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    const glm::vec3 eye(0.0f);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::mat4 forward = projection * glm::lookAt(eye, {0.0f, 0.0f, -1.0f}, up);
    const glm::mat4 backward = projection * glm::lookAt(eye, {0.0f, 0.0f, 1.0f}, up);
    const glm::vec3 minimum(-1.0f, -1.0f, -1.0f);
    const glm::vec3 maximum(1.0f);
    const auto terrainAt = [&](float distance) {
        return glm::translate(identity, {0.0f, 0.0f, distance});
    };
    const glm::mat4 distantTerrain = terrainAt(-95.0f);
    require(!isOutsideVulkanFrustum(forward * distantTerrain, minimum, maximum),
        "distant terrain inside the current far plane was rejected");
    require(isOutsideVulkanFrustum(backward * distantTerrain, minimum, maximum),
        "terrain behind the new view was retained");
    require(!isOutsideVulkanFrustum(backward * terrainAt(95.0f), minimum, maximum),
        "distant terrain on the other side was rejected after turning");
    require(!isOutsideVulkanFrustum(forward * terrainAt(-100.0f), minimum, maximum),
        "a chunk spanning the far plane must remain visible");
    require(isOutsideVulkanFrustum(forward * terrainAt(-110.0f), minimum, maximum),
        "a chunk entirely outside the far plane was retained");
    auto invalid = identity;
    invalid[0][0] = std::numeric_limits<float>::quiet_NaN();
    require(!isOutsideVulkanFrustum(invalid, minimum, maximum),
        "an invalid transform must conservatively retain the chunk");
}

} // namespace

int main() {
    try {
        checkFixture(false);
        checkFixture(true);
        checkStitchedChunkBounds();
        checkMaterialMaps();
        checkArchiveMaterialLayers();
        checkGrassCoverage();
        checkFrustum();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
