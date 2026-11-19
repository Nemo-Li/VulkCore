#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <initializer_list>

#include <glm/glm.hpp>

namespace VulkCore {

inline bool isOutsideVulkanFrustum(
    const glm::mat4& clipFromLocal,
    const glm::vec3& minimum,
    const glm::vec3& maximum
) {
    // A box that straddles a plane may still generate visible fragments.
    // Reject it only when every corner is outside the same clip plane.
    std::array<bool, 6> outside;
    outside.fill(true);
    for (const float x : {minimum.x, maximum.x}) {
        for (const float y : {minimum.y, maximum.y}) {
            for (const float z : {minimum.z, maximum.z}) {
                const glm::vec4 clip = clipFromLocal * glm::vec4(x, y, z, 1.0f);
                if (!std::isfinite(clip.x) || !std::isfinite(clip.y) ||
                    !std::isfinite(clip.z) || !std::isfinite(clip.w)) {
                    return false;
                }
                const std::array<float, 6> planeDistances{
                    clip.w + clip.x, clip.w - clip.x,
                    clip.w + clip.y, clip.w - clip.y,
                    clip.z, clip.w - clip.z
                };
                const float tolerance = 1.0e-5f * std::max(1.0f, std::abs(clip.w));
                for (std::size_t plane = 0; plane < outside.size(); ++plane) {
                    outside[plane] &= planeDistances[plane] < -tolerance;
                }
            }
        }
    }

    return std::any_of(outside.begin(), outside.end(), [](bool beyond) { return beyond; });
}

} // namespace VulkCore
