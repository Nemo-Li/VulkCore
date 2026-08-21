#pragma once

#include <glm/glm.hpp>

namespace VulkCore {

struct UniformBufferObject {
    glm::mat4 model{1.0f};
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    // x is elapsed time in seconds. The remaining components keep the
    // value 16-byte aligned for GLSL std140 uniform-buffer layout.
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
};

} // namespace VulkCore
