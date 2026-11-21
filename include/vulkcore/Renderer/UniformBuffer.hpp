#pragma once

#include <array>

#include <glm/glm.hpp>

namespace VulkCore {

struct UniformBufferObject {
    glm::mat4 model{1.0f};
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    // Elapsed seconds, transition time, HDR exposure, and vegetation wind time.
    // All four values share a 16-byte std140 slot.
    glm::vec4 time{0.0f};
    glm::vec4 viewport{0.0f}; // width, height, 1/width, 1/height
    glm::vec4 ambientLight{0.18f, 0.21f, 0.24f, 1.0f}; // rgb color, intensity
    glm::vec4 sunLightDirection{0.45f, 0.55f, 0.75f, 1.0f}; // xyz direction, intensity
    glm::vec4 sunLightColor{0.82f, 0.86f, 0.9f, 1.0f}; // rgb color, reserved alpha
    // xyz is the camera position in normalized scene space; w is the scene
    // normalization scale used to convert terrain LOD distances to world units.
    glm::vec4 cameraPosition{0.0f};
    // xyz is the normalized player foot position. w is one while the player
    // is active, allowing vegetation shaders to disable interaction in editor
    // scenes without a separate descriptor layout.
    glm::vec4 playerPosition{0.0f};
    // xyz remains in terrain-world units because grass only needs its
    // direction and relative speed when choosing a trampling direction.
    glm::vec4 playerVelocity{0.0f};
    glm::vec4 distanceFogParams{0.0f}; // start, half distance, max opacity, enabled
    glm::vec4 distanceFogHorizon{0.0f}; // linear RGB
    glm::vec4 distanceFogUpper{0.0f}; // linear RGB
    glm::vec4 daylight{0.0f}; // enabled, cloud time in seconds, reserved, reserved
    static constexpr std::size_t kMaxFloatingMistPuffs = 16;
    glm::vec4 floatingMist{0.0f}; // count, opacity, time, reserved
    std::array<glm::vec4, kMaxFloatingMistPuffs> mistCenters{}; // world center, density
    std::array<glm::vec4, kMaxFloatingMistPuffs> mistInverseRadii{}; // xyz, noise seed
};

} // namespace VulkCore
