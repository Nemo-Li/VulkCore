#include "vulkcore/Terrain/Terrain.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
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

    void create(bool missingCorner) const {
        const std::size_t count = missingCorner ? 3 : 4;
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
            f32(entry, 1.0f + tileX * 2.0f);
            f32(entry + 4, 1.0f + tileZ * 2.0f);
            f32(entry + 8, 2.0f);
            f32(entry + 0x10, 1.0f);
            u32(entry + 0x20, 0x10);
            const std::string name = "540000000" + std::to_string(tile);
            std::copy(name.begin(), name.end(), tscb.begin() + entry + 0x30);
            std::ofstream heightmap(directory / (name + ".hght"), std::ios::binary);
            for (int z = 0; z < 256; ++z) {
                for (int x = 0; x < 256; ++x) {
                    int value = 8000 + (tileX * 255 + x) * 3 +
                        (tileZ * 255 + z) * 5 + tileX * 40000;
                    if (tile == 0 && x == 100 && z == 100) {
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

} // namespace

int main() {
    try {
        checkFixture(false);
        checkFixture(true);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
