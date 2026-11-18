#pragma once

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vulkan/vulkan.h>

namespace VulkCore {

struct SurfacePresentation {
    VkExtent2D extent;
    VkSurfaceTransformFlagBitsKHR transform;
};

inline SurfacePresentation chooseSurfacePresentation(
    const VkSurfaceCapabilitiesKHR& capabilities,
    VkExtent2D framebuffer,
    bool renderInWindowCoordinates
) {
    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == std::numeric_limits<uint32_t>::max() ||
        extent.width == 0 || extent.height == 0) {
        extent = framebuffer;
    }
    extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    auto transform = capabilities.currentTransform;
    if (renderInWindowCoordinates) {
        // The scene, ImGui and touch input share XComponent's top-left origin,
        // +X right, +Y down. No pass pre-rotates its output. Declaring the
        // display rotation as preTransform would incorrectly claim it does.
        if ((capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) == 0) {
            throw std::runtime_error("This surface cannot present content in window coordinates.");
        }
        transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    }
    return {extent, transform};
}

} // namespace VulkCore
