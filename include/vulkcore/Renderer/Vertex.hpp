#pragma once

#include <array>
#include <cstddef>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

namespace VulkCore {

struct Vertex {
    glm::vec2 position;
    glm::vec3 color;

    static VkVertexInputBindingDescription bindingDescription() {
        VkVertexInputBindingDescription description{};
        description.binding = 0;
        description.stride = sizeof(Vertex);
        description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return description;
    }

    static std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 2> descriptions{};

        descriptions[0].binding = 0;
        descriptions[0].location = 0;
        descriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
        descriptions[0].offset = offsetof(Vertex, position);

        descriptions[1].binding = 0;
        descriptions[1].location = 1;
        descriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        descriptions[1].offset = offsetof(Vertex, color);

        return descriptions;
    }
};

inline const std::array<Vertex, 3> kTriangleVertices = {{
    {{0.0f, -0.5f}, {1.0f, 1.0f, 0.0f}},
    {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
    {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}
}};

} // namespace VulkCore
