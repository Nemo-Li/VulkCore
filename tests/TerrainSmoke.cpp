#include "vulkcore/Renderer/Frustum.hpp"
#include "vulkcore/Terrain/Grass.hpp"
#include "vulkcore/Terrain/Terrain.hpp"

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

void checkGrassCoverage() {
    using namespace VulkCore;
    Fixture fixture;
    fixture.create(false);
    const auto directory = fixture.root / "terrain_grass";
    const auto group = directory / "5400000000.grass.extm";
    std::filesystem::create_directories(group);
    const auto greenPath = group / "5400000000.grass.extm";
    const auto writeMap = [](const std::filesystem::path& path,
                             std::array<unsigned char, 4> sample, bool split) {
        std::ofstream file(path, std::ios::binary);
        for (std::uint32_t z = 0; z < kTerrainGrassResolution; ++z) {
            for (std::uint32_t x = 0; x < kTerrainGrassResolution; ++x) {
                auto pixel = sample;
                if (split && x >= kTerrainGrassResolution / 2u) pixel = {0, 255, 0, 0};
                file.write(reinterpret_cast<const char*>(pixel.data()), pixel.size());
            }
        }
    };
    writeMap(greenPath, {128, 24, 150, 60}, true);
    writeMap(directory / "5400000001.grass.extm", {64, 180, 72, 16}, false);

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

    GrassCoverage grass;
    require(grass.update(*scene, {96.0f, 96.0f}), "initial grass coverage must populate");
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
    require(near && near->rootHeight.w > 0.0f, "nonzero grass map heights must produce mesh blades");
    const auto startingRoot = near->rootHeight;
    const auto* flower = nearest({96.0f, 96.0f}, GrassLod::YellowFlowerCard);
    require(flower && flower->normalWidth.w > 0.0f && flower->normalWidth.w <= 0.092f,
        "vegetated grass maps must support small yellow flowers");
    require(nearest({96.0f, 96.0f}, GrassLod::FlowerCard) == nullptr,
        "legacy flower placement remains limited to unmapped grass");
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
        std::abs(near->rootHeight.x - 900.0f) < 2.0f,
        "grass coverage must follow long movement and adopt the new map");

    require(grass.update(*scene, {96.0f, 900.0f}), "unmapped ground must update coverage");
    const auto* legacyFlower = nearest({96.0f, 900.0f}, GrassLod::FlowerCard);
    const auto* yellowFlower = nearest({96.0f, 900.0f}, GrassLod::YellowFlowerCard);
    require(legacyFlower && yellowFlower && legacyFlower->rootHeight != yellowFlower->rootHeight &&
        legacyFlower->normalWidth.w == 0.42f && yellowFlower->normalWidth.w <= 0.092f,
        "legacy and small yellow flowers must coexist with distinct scattering and sizes");
    float canopyHeight = 0.0f;
    std::uint32_t bladeCount = 0;
    for (const auto& range : grass.ranges()) {
        if (range.lod != GrassLod::NearMesh) continue;
        for (std::uint32_t i = 0; i < range.instanceCount; ++i) {
            canopyHeight += grass.instances()[range.firstInstance + i].rootHeight.w;
            ++bladeCount;
        }
    }
    require(bladeCount > 0 && canopyHeight / bladeCount > 1.07f && canopyHeight / bladeCount < 1.22f,
        "procedural grass must stand around chest height on the 1.58-unit character");

    const GrassCoverMask cover(4, 2, {127, 255, 255, 127, 127, 255, 255, 127});
    require(cover.contains({6.0f, 2.0f}) && cover.contains({-6.0f, -2.0f}) &&
        !cover.contains({0.5f, 2.0f}) && !cover.contains({-0.5f, -2.0f}),
        "grass coverage must follow bright paths and repeat across negative coordinates");
    grass.setCoverMask(cover);
    require(grass.update(*scene, {96.0f, 900.0f}), "replacing the cover must rebuild cached grass roots");
    std::array<std::size_t, 2> coverageCounts{};
    for (const auto& range : grass.ranges()) {
        if (!isGrassVegetation(range.lod)) continue;
        for (std::uint32_t i = 0; i < range.instanceCount; ++i) {
            const auto& root = grass.instances()[range.firstInstance + i].rootHeight;
            require(cover.contains({root.x, root.z}), "both grass LODs must obey the source mask at every root");
            ++coverageCounts[range.lod == GrassLod::NearMesh ? 0 : 1];
        }
    }
    require(coverageCounts[0] > 0 && coverageCounts[1] > 0, "the paths must contain mesh blades and cross cards");
    const auto* cross = nearest({126.0f, 900.0f}, GrassLod::CrossCard);
    require(cross && (cross + 1)->rootHeight == cross->rootHeight &&
        std::abs((cross + 1)->motion.x - cross->motion.x - glm::half_pi<float>()) < 0.00001f,
        "LOD1 must draw two perpendicular cards at one root");

    std::filesystem::resize_file(greenPath, 100);
    bool rejected = false;
    try {
        TerrainScene::load(fixture.root / "MainField.tscb", fixture.root);
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find("grass map") != std::string::npos;
    }
    require(rejected, "a truncated grass map must fail with a clear error");
    std::cout << "PASS: grass map height/RGB, bare cells, near canopy height and movement\n";
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
        checkGrassCoverage();
        checkFrustum();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
