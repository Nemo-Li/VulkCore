#include "vulkcore/Renderer/SurfacePresentation.hpp"
#include "vulkcore/Renderer/SceneProjection.hpp"

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
        const auto reference = VulkCore::sceneProjection(16.0f / 9.0f, 0.01f, 100.0f, false);
        const std::array<VkExtent2D, 7> screens{{{1920, 1080}, {2400, 1080}, {2340, 1080},
            {2560, 1600}, {1280, 960}, {1440, 3200}, {3200, 1440}}};
        for (const auto screen : screens) {
            const float aspect = static_cast<float>(screen.width) / screen.height;
            const auto cover = VulkCore::sceneProjection(aspect, 0.01f, 100.0f, true);
            const float scale = std::max(screen.width / 1920.0f, screen.height / 1080.0f);
            const float imageWidth = std::abs(cover[0][0] / reference[0][0]) * screen.width;
            const float imageHeight = std::abs(cover[1][1] / reference[1][1]) * screen.height;
            require(std::abs(imageWidth - 1920.0f * scale) < 0.01f &&
                std::abs(imageHeight - 1080.0f * scale) < 0.01f,
                "Full-screen projection did not uniformly enlarge the reference frame");
            require(imageWidth + 0.01f >= screen.width && imageHeight + 0.01f >= screen.height,
                "Cover projection left uncovered screen edges");
            require(std::abs(std::abs(cover[0][0]) * screen.width -
                std::abs(cover[1][1]) * screen.height) < 0.01f,
                "Full-screen projection stretched world-space dimensions");
            require(cover[2][0] == 0 && cover[2][1] == 0, "Full-screen crop was not centered");
            auto legacy = glm::perspective(glm::radians(45.0f), aspect, 0.01f, 100.0f);
            legacy[1][1] *= -1.0f;
            const auto desktop = VulkCore::sceneProjection(aspect, 0.01f, 100.0f, false);
            for (int column = 0; column < 4; ++column) {
                for (int row = 0; row < 4; ++row) {
                    require(std::abs(desktop[column][row] - legacy[column][row]) < 0.000001f,
                        "Desktop camera projection changed");
                }
            }
        }
        std::cout << "Surface orientation, full-screen cover and aspect checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
