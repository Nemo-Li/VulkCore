#include "vulkcore/Terrain/RoadLayout.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace VulkCore {
namespace {

constexpr float kDefaultRoadWidth = 4.8f;
constexpr float kMinimumPointSpacing = 0.05f;
constexpr float kCurveSampleSpacing = 3.0f;
constexpr std::size_t kMaxRoadMeshVertices = 2'000'000;

bool isCancelled(const std::atomic<bool>* cancelled) {
    return cancelled && cancelled->load(std::memory_order_relaxed);
}

glm::vec2 readXZ(const YAML::Node& value) {
    if (!value.IsSequence() || value.size() != 3) {
        throw std::runtime_error("expected a coordinate vector [x, y, z]");
    }
    const glm::vec2 point(value[0].as<float>(), value[2].as<float>());
    if (!(std::abs(point.x) <= 10000.0f && std::abs(point.y) <= 10000.0f)) {
        throw std::runtime_error("road coordinate is outside the supported world bounds");
    }
    return point;
}

struct RoadPoint {
    glm::vec2 position;
    glm::vec2 incoming{0.0f};
    glm::vec2 outgoing{0.0f};
};

TerrainRoadPath readRoad(const YAML::Node& road, std::size_t& meshVertexBudget) {
    const auto widthNode = road["Width"];
    const float width = widthNode ? widthNode.as<float>() : kDefaultRoadWidth;
    if (!(width >= 0.5f && width <= 24.0f)) {
        throw std::runtime_error("road width must be between 0.5 and 24 m");
    }
    const auto typeNode = road["RailType"];
    const auto type = typeNode ? typeNode.as<std::string>() : "Linear";
    if (type != "Linear" && type != "Bezier") {
        throw std::runtime_error("unsupported road RailType: " + type);
    }
    const auto closedNode = road["IsClosed"];
    const bool closed = closedNode && closedNode.as<bool>();
    const auto points = road["RailPoints"];
    if (!points.IsSequence() || points.size() < (closed ? 3u : 2u) || points.size() > 4096) {
        throw std::runtime_error("invalid road RailPoints sequence");
    }

    std::vector<RoadPoint> knots;
    knots.reserve(points.size());
    for (const auto& point : points) {
        auto& knot = knots.emplace_back(RoadPoint{readXZ(point["Translate"])});
        if (type != "Bezier") continue;
        const auto handles = point["ControlPoints"];
        if (!handles) continue;
        if (!handles.IsSequence() || handles.size() != 2) {
            throw std::runtime_error("Bezier road requires [incoming, outgoing] control offsets");
        }
        knot.incoming = readXZ(handles[0]);
        knot.outgoing = readXZ(handles[1]);
    }
    // Some exporters include the first knot twice for a closed rail.
    if (closed && knots.front().position == knots.back().position) knots.pop_back();
    if (closed && knots.size() < 3) throw std::runtime_error("closed road needs three knots");

    TerrainRoadPath path{width, {knots.front().position}};
    const auto rowVertices = static_cast<std::size_t>(
        std::max(2.0f, std::ceil(width / kTerrainRoadSampleSpacing))) + 1;
    const auto consumeRows = [&](std::size_t rows) {
        const auto vertices = rows * rowVertices;
        if (vertices > meshVertexBudget) throw std::runtime_error("road layout exceeds the mesh size limit");
        meshVertexBudget -= vertices;
    };
    consumeRows(1);
    const auto append = [&](glm::vec2 point) {
        const float span = glm::length(point - path.points.back());
        if (span < kMinimumPointSpacing) return;
        consumeRows(static_cast<std::size_t>(std::ceil(span / kTerrainRoadSampleSpacing)));
        path.points.push_back(point);
    };

    const std::size_t segmentCount = closed ? knots.size() : knots.size() - 1;
    for (std::size_t i = 0; i < segmentCount; ++i) {
        const auto& from = knots[i];
        const auto& to = knots[(i + 1) % knots.size()];
        if (from.outgoing != glm::vec2(0.0f) || to.incoming != glm::vec2(0.0f)) {
            const glm::vec2 a = from.position;
            const glm::vec2 b = a + from.outgoing;
            const glm::vec2 d = to.position;
            const glm::vec2 c = d + to.incoming;
            const float controlLength = glm::length(b - a) + glm::length(c - b) + glm::length(d - c);
            const int steps = static_cast<int>(std::ceil(controlLength / kCurveSampleSpacing));
            for (int step = 1; step < steps; ++step) {
                const float t = static_cast<float>(step) / steps;
                const auto ab = glm::mix(a, b, t);
                const auto bc = glm::mix(b, c, t);
                const auto cd = glm::mix(c, d, t);
                append(glm::mix(glm::mix(ab, bc, t), glm::mix(bc, cd, t), t));
            }
        }
        if (closed && i + 1 == segmentCount &&
            glm::length(to.position - path.points.back()) < kMinimumPointSpacing) {
            path.points.back() = to.position;
        } else {
            append(to.position);
        }
    }
    if (path.points.size() < (closed ? 4u : 2u)) {
        throw std::runtime_error("road has too few distinct points");
    }
    return path;
}

} // namespace

std::vector<TerrainRoadPath> loadTerrainRoadLayout(
    const std::filesystem::path& source, const std::atomic<bool>* cancelled
) {
    std::vector<std::filesystem::path> files;
    if (std::filesystem::is_directory(source)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(source)) {
            if (isCancelled(cancelled)) return {};
            const auto extension = entry.path().extension();
            if ((extension == ".yml" || extension == ".yaml") && entry.is_regular_file()) {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());
    } else {
        files.push_back(source);
    }

    std::vector<TerrainRoadPath> paths;
    auto meshVertexBudget = kMaxRoadMeshVertices;
    for (const auto& file : files) {
        if (isCancelled(cancelled)) return {};
        try {
            std::ifstream input(file);
            if (!input) throw std::runtime_error("cannot open road layout");
            const auto root = YAML::Load(input);
            if (!root.IsMap()) continue;
            const auto rails = root["Rails"];
            if (!rails) continue;
            if (!rails.IsSequence()) throw std::runtime_error("Rails must be a sequence");
            for (const auto& rail : rails) {
                if (isCancelled(cancelled)) return {};
                if (rail.IsMap() && rail["UnitConfigName"].as<std::string>("") == "NavigationMeshRoad") {
                    paths.push_back(readRoad(rail, meshVertexBudget));
                }
            }
        } catch (const std::exception& error) {
            throw std::runtime_error(file.string() + ": " + error.what());
        }
    }
    return paths;
}

} // namespace VulkCore
