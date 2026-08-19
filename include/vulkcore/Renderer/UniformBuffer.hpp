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
};

} // namespace VulkCore
