#pragma once

#include "vulkcore/Core/Config.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

namespace VulkCore {

inline glm::mat4 sceneProjection(float aspectRatio, float nearPlane, float farPlane, bool coverReferenceViewport) {
    if (!std::isfinite(aspectRatio) || aspectRatio <= 0) {
        throw std::invalid_argument("Scene viewport requires a positive aspect ratio.");
    }
    float verticalFov = glm::radians(45.0f);
    if (coverReferenceViewport) {
        constexpr float referenceAspect = static_cast<float>(kWindowWidth) / kWindowHeight;
        // Cover the screen with the original viewport at one uniform scale.
        // Wide screens crop vertically; narrow screens crop horizontally.
        // Render directly at screen size so overlays and touch coordinates
        // retain their full-screen positions and no oversized surface is needed.
        const float heightFraction = std::min(1.0f, referenceAspect / aspectRatio);
        verticalFov = 2.0f * std::atan(std::tan(verticalFov * 0.5f) * heightFraction);
    }
    auto projection = glm::perspective(verticalFov, aspectRatio, nearPlane, farPlane);
    // GLM follows OpenGL's Y direction; Vulkan's viewport uses the opposite one.
    projection[1][1] *= -1.0f;
    return projection;
}

} // namespace VulkCore
