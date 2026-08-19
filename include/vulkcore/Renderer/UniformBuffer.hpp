#pragma once

#include <glm/glm.hpp>

namespace VulkCore {

struct UniformBufferObject {
    glm::mat4 model{1.0f};
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
};

} // namespace VulkCore
