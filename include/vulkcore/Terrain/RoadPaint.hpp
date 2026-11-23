#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <vector>

#include <glm/vec2.hpp>

namespace VulkCore {

enum class RoadSurface : std::uint32_t { Erase, Mud, Gravel, Stone };

class RoadPaint {
public:
    static constexpr float kMinimumRadius = 0.5f;
    static constexpr float kMaximumRadius = 32.0f;
    static constexpr float kDefaultRadius = 3.75f;

    static float radiusAt(RoadSurface surface, float radius, glm::vec2 center);

    bool paint(RoadSurface surface, float radius, glm::vec2 point, bool continueStroke);
    bool readStroke(std::istream& input);
    void writeStrokes(std::ostream& output) const;
    void clear();

    std::uint64_t revision() const { return revision_; }
    bool full() const { return pointCount_ >= kMaximumPoints; }

    // Packed index consumed by terrain_road_weights.glsl.
    std::vector<std::uint32_t> buildGpuIndex() const;

private:
    static constexpr std::size_t kMaximumPoints = 32768;
    struct Stroke {
        RoadSurface surface;
        float radius;
        std::vector<glm::vec2> points;
    };

    std::vector<Stroke> strokes_;
    std::size_t pointCount_ = 0;
    std::uint64_t revision_ = 1;
};

} // namespace VulkCore
