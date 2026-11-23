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

// Place the complete preview frustum in the usable viewport, using one pixel scale on both axes.
// The swapchain viewport remains full-screen so depth sampling and screen-space passes agree.
inline glm::mat4 fittedSceneProjection(float verticalFov, float sourceAspect, float framebufferAspect,
                                       glm::vec4 area, float nearPlane, float farPlane) {
    const float width = area.z - area.x;
    const float height = area.w - area.y;
    const float fittedHeight = std::min(height, width * framebufferAspect / sourceAspect);
    auto projection = glm::perspectiveRH_ZO(glm::radians(verticalFov), framebufferAspect, nearPlane, farPlane);
    projection[0][0] *= fittedHeight;
    projection[1][1] *= -fittedHeight;
    projection[2][0] = 1.0f - (area.x + area.z);
    projection[2][1] = 1.0f - (area.y + area.w);
    return projection;
}

} // namespace VulkCore
