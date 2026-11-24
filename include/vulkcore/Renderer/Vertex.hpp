#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

namespace VulkCore {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::vec4 color;
    // Up to four normalized skinning influences. Static meshes leave both
    // fields at zero and therefore continue to use the bind-pose position.
    glm::ivec4 boneIndices{0};
    glm::vec4 boneWeights{0.0f};
    // Terrain vertices store the material map layer in x. Other meshes may use
    // their existing mapping or leave it at zero.
    glm::vec4 terrainMapping{0.0f};

    static VkVertexInputBindingDescription bindingDescription() {
        VkVertexInputBindingDescription description{};
        description.binding = 0;
        description.stride = sizeof(Vertex);
        description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return description;
    }

    static std::array<VkVertexInputAttributeDescription, 7> attributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 7> descriptions{};

        descriptions[0].binding = 0;
        descriptions[0].location = 0;
        descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        descriptions[0].offset = offsetof(Vertex, position);

        descriptions[1].binding = 0;
        descriptions[1].location = 1;
        descriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        descriptions[1].offset = offsetof(Vertex, normal);

        descriptions[2].binding = 0;
        descriptions[2].location = 2;
        descriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        descriptions[2].offset = offsetof(Vertex, texCoord);

        descriptions[3].binding = 0;
        descriptions[3].location = 3;
        descriptions[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        descriptions[3].offset = offsetof(Vertex, color);

        descriptions[4].binding = 0;
        descriptions[4].location = 4;
        descriptions[4].format = VK_FORMAT_R32G32B32A32_SINT;
        descriptions[4].offset = offsetof(Vertex, boneIndices);

        descriptions[5].binding = 0;
        descriptions[5].location = 5;
        descriptions[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        descriptions[5].offset = offsetof(Vertex, boneWeights);

        descriptions[6].binding = 0;
        descriptions[6].location = 6;
        descriptions[6].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        descriptions[6].offset = offsetof(Vertex, terrainMapping);

        return descriptions;
    }
};

inline const std::array<Vertex, 4> kRectangleVertices = {{
    {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    {{0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}}
}};

inline const std::array<uint32_t, 6> kRectangleIndices = {{
    0, 1, 2,
    2, 3, 0
}};

} // namespace VulkCore
