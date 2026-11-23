#include "vulkcore/Terrain/RoadPaint.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <istream>
#include <ostream>
#include <utility>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace VulkCore {
namespace {

constexpr float kMaximumCoordinate = 1'000'000.0f;
// Keeps each segment's index footprint bounded even when two mouse samples are far apart.
constexpr float kMaximumSegmentLength = 128.0f;
constexpr float kMinimumCellSize = 32.0f;
constexpr int kMaximumCellsPerAxis = 256;
constexpr std::uint32_t kHeaderSize = 5;
// Keep in sync with the width profile in terrain_road_weights.glsl.
constexpr float kMaximumDirtRadiusScale = 1.22f;

float indexRadius(RoadSurface surface, float radius) {
    return surface == RoadSurface::Mud ? radius * kMaximumDirtRadiusScale : radius;
}

bool validBrush(RoadSurface surface, float radius) {
    return surface <= RoadSurface::Stone && radius >= RoadPaint::kMinimumRadius &&
        radius <= RoadPaint::kMaximumRadius;
}

bool validPoint(glm::vec2 point) {
    return std::abs(point.x) <= kMaximumCoordinate && std::abs(point.y) <= kMaximumCoordinate;
}

std::uint32_t floatBits(float value) { return std::bit_cast<std::uint32_t>(value); }

} // namespace

float RoadPaint::radiusAt(RoadSurface surface, float radius, glm::vec2 center) {
    if (surface != RoadSurface::Mud) return radius;
    const glm::vec2 p = center / std::max(1.0f, 0.65f * radius);
    const float scale = 1.0f + 0.12f * std::sin(glm::dot(p, glm::vec2(0.44f, 0.29f)))
        + 0.07f * std::sin(glm::dot(p, glm::vec2(-0.79f, 0.91f)) + 1.4f)
        + 0.03f * std::sin(glm::dot(p, glm::vec2(0.19f, -0.16f)) + 2.8f);
    return radius * scale;
}

bool RoadPaint::paint(RoadSurface surface, float radius, glm::vec2 point, bool continueStroke) {
    if (!validBrush(surface, radius) || !validPoint(point) || full()) return false;
    if (continueStroke && !strokes_.empty() && strokes_.back().surface == surface &&
        strokes_.back().radius == radius) {
        auto& points = strokes_.back().points;
        const glm::vec2 start = points.back();
        const float length = glm::length(point - start);
        if (length < 0.01f) return false;
        const auto pieces = static_cast<std::size_t>(std::ceil(length / kMaximumSegmentLength));
        if (pieces > kMaximumPoints - pointCount_) return false;
        for (std::size_t i = 1; i < pieces; ++i) {
            points.push_back(start + (point - start) * (static_cast<float>(i) / static_cast<float>(pieces)));
        }
        points.push_back(point);
        pointCount_ += pieces;
    } else {
        strokes_.push_back({surface, radius, {point}});
        ++pointCount_;
    }
    ++revision_;
    return true;
}

bool RoadPaint::readStroke(std::istream& input) {
    std::uint32_t surface;
    float radius;
    std::size_t count;
    if (!(input >> surface >> radius >> count) || !validBrush(static_cast<RoadSurface>(surface), radius) ||
        count == 0 || count > kMaximumPoints - pointCount_) {
        return false;
    }
    Stroke stroke{static_cast<RoadSurface>(surface), radius, {}};
    stroke.points.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        glm::vec2 point;
        if (!(input >> point.x >> point.y) || !validPoint(point) ||
            (!stroke.points.empty() && glm::length(point - stroke.points.back()) > kMaximumSegmentLength + 0.25f)) {
            return false;
        }
        stroke.points.push_back(point);
    }
    pointCount_ += count;
    strokes_.push_back(std::move(stroke));
    ++revision_;
    return true;
}

void RoadPaint::writeStrokes(std::ostream& output) const {
    for (const auto& stroke : strokes_) {
        output << "terrain_road " << static_cast<std::uint32_t>(stroke.surface) << ' '
               << stroke.radius << ' ' << stroke.points.size();
        for (const auto& point : stroke.points) output << ' ' << point.x << ' ' << point.y;
        output << '\n';
    }
}

void RoadPaint::clear() {
    strokes_.clear();
    pointCount_ = 0;
    ++revision_;
}

std::vector<std::uint32_t> RoadPaint::buildGpuIndex() const {
    if (strokes_.empty()) return std::vector<std::uint32_t>(kHeaderSize, 0);
    glm::vec2 minimum(kMaximumCoordinate + kMaximumRadius * kMaximumDirtRadiusScale);
    glm::vec2 maximum(-minimum);
    for (const auto& stroke : strokes_) {
        const glm::vec2 radius(indexRadius(stroke.surface, stroke.radius));
        for (const auto& point : stroke.points) {
            minimum = glm::min(minimum, point - radius);
            maximum = glm::max(maximum, point + radius);
        }
    }
    const glm::vec2 extent = maximum - minimum;
    const float cellSize = std::max(kMinimumCellSize, std::max(extent.x, extent.y) / kMaximumCellsPerAxis);
    const glm::ivec2 dimensions = glm::clamp(glm::ivec2(glm::ceil(extent / cellSize)),
        glm::ivec2(1), glm::ivec2(kMaximumCellsPerAxis));
    const auto cellAt = [&](glm::vec2 point) {
        return glm::clamp(glm::ivec2(glm::floor((point - minimum) / cellSize)),
            glm::ivec2(0), dimensions - 1);
    };
    const auto cellCount = static_cast<std::size_t>(dimensions.x * dimensions.y);
    std::vector<std::vector<std::uint32_t>> cells(cellCount);
    std::vector<std::uint32_t> data(kHeaderSize + cellCount * 2);
    data[0] = dimensions.x;
    data[1] = dimensions.y;
    data[2] = floatBits(minimum.x);
    data[3] = floatBits(minimum.y);
    data[4] = floatBits(cellSize);

    for (std::uint32_t id = 0; id < strokes_.size(); ++id) {
        const auto& stroke = strokes_[id];
        const glm::vec2 radius(indexRadius(stroke.surface, stroke.radius));
        const std::size_t segmentCount = std::max<std::size_t>(1, stroke.points.size() - 1);
        for (std::size_t i = 0; i < segmentCount; ++i) {
            const glm::vec2 a = stroke.points[i];
            const glm::vec2 b = stroke.points[std::min(i + 1, stroke.points.size() - 1)];
            const auto start = cellAt(glm::min(a, b) - radius);
            const auto end = cellAt(glm::max(a, b) + radius);
            const auto offset = static_cast<std::uint32_t>(data.size());
            data.insert(data.end(), {floatBits(a.x), floatBits(a.y), floatBits(b.x), floatBits(b.y),
                floatBits(stroke.radius), static_cast<std::uint32_t>(stroke.surface), id});
            for (int z = start.y; z <= end.y; ++z) {
                for (int x = start.x; x <= end.x; ++x) cells[z * dimensions.x + x].push_back(offset);
            }
        }
    }
    for (std::size_t i = 0; i < cells.size(); ++i) {
        data[kHeaderSize + i * 2] = static_cast<std::uint32_t>(data.size());
        data[kHeaderSize + i * 2 + 1] = static_cast<std::uint32_t>(cells[i].size());
        data.insert(data.end(), cells[i].rbegin(), cells[i].rend());
    }
    return data;
}

} // namespace VulkCore
