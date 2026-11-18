#include "vulkcore/Renderer/SurfacePresentation.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        VkSurfaceCapabilitiesKHR capabilities{};
        capabilities.minImageExtent = {1, 1};
        capabilities.maxImageExtent = {4096, 4096};
        capabilities.supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR |
            VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR | VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR |
            VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR;
        const std::array rotations{VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
            VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR, VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR,
            VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR};
        const std::array<VkExtent2D, 3> sizes{{{1920, 1080}, {2560, 1440}, {1080, 1920}}};
        for (const auto size : sizes) {
            capabilities.currentExtent = size;
            for (const auto rotation : rotations) {
                capabilities.currentTransform = rotation;
                const auto presentation = VulkCore::chooseSurfacePresentation(capabilities, size, true);
                require(presentation.transform == VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
                    "Window-coordinate content incorrectly declared itself pre-rotated");
                require(presentation.extent.width == size.width && presentation.extent.height == size.height,
                    "Display rotation swapped component pixel dimensions");
                // Equal lengths in view space must remain equal in framebuffer pixels.
                const auto projection = glm::perspective(glm::radians(45.0f),
                    static_cast<float>(presentation.extent.width) / presentation.extent.height, 0.01f, 100.0f);
                const auto origin = projection * glm::vec4(0, 0, -4, 1);
                const auto right = projection * glm::vec4(1, 0, -4, 1);
                const auto up = projection * glm::vec4(0, 1, -4, 1);
                const float pixelWidth = std::abs(right.x / right.w - origin.x / origin.w) * presentation.extent.width;
                const float pixelHeight = std::abs(up.y / up.w - origin.y / origin.w) * presentation.extent.height;
                require(std::abs(pixelWidth - pixelHeight) < 0.001f, "Projection stretched the scene");
                const auto desktop = VulkCore::chooseSurfacePresentation(capabilities, size, false);
                require(desktop.transform == rotation, "Desktop presentation policy changed");
            }
        }
        capabilities.currentExtent = {std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max()};
        const auto variable = VulkCore::chooseSurfacePresentation(capabilities, {1600, 900}, true);
        require(variable.extent.width == 1600 && variable.extent.height == 900,
            "Variable surface ignored the framebuffer viewport");
        capabilities.currentExtent = {0, 0};
        const auto empty = VulkCore::chooseSurfacePresentation(capabilities, {1280, 720}, true);
        require(empty.extent.width == 1280 && empty.extent.height == 720,
            "Uninitialized surface geometry discarded the component size");
        capabilities.supportedTransforms = VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR;
        bool rejected = false;
        try { VulkCore::chooseSurfacePresentation(capabilities, {1280, 720}, true); }
        catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "Unsupported window-coordinate presentation was silently accepted");
        std::cout << "Surface orientation and aspect checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
