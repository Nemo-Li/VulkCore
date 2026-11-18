#include "vulkcore/Platform/OhosWindow.hpp"

#include <imgui.h>
#include <native_window/external_window.h>
#include <algorithm>
#include <stdexcept>

namespace VulkCore {

bool keyPressed(Window* window, Key key) {
    if (!window) return false;
    std::lock_guard lock(window->inputMutex);
    return window->keys[static_cast<unsigned>(key)];
}
MovementInput analogMovement(Window* window) {
    if (!window) return {};
    std::lock_guard lock(window->inputMutex);
    return window->movement;
}
bool lookPressed(Window* window) {
    if (!window) return false;
    std::lock_guard lock(window->inputMutex);
    return window->touchLook.active();
}
void cursorPosition(Window* window, double* x, double* y) {
    std::lock_guard lock(window->inputMutex);
    *x = window->touchLook.x();
    *y = window->touchLook.y();
}
void framebufferSize(Window* window, int* width, int* height) {
    *width = window->width.load();
    *height = window->height.load();
}
void setWindowUserPointer(Window* window, void* pointer) { window->userPointer = pointer; }
void* windowUserPointer(Window* window) { return window->userPointer; }
void setResizeCallback(Window* window, ResizeCallback callback) { window->resizeCallback = callback; }
void waitWindowEvents() {}
void prepareWindowSurface(Window* window) {
    int width = 0, height = 0;
    framebufferSize(window, &width, &height);
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("Cannot create a swapchain for an empty XComponent viewport.");
    }
    // OHOS reports currentExtent from NativeWindow buffer geometry, which may
    // still contain the previous portrait size after the component has resized.
    if (OH_NativeWindow_NativeWindowHandleOpt(window->native, SET_BUFFER_GEOMETRY, width, height) != 0) {
        throw std::runtime_error("Failed to match the native buffer to the XComponent viewport.");
    }
}
void configureWindowPresentation(Window* window) {
    // OHOS WSI defaults to SCALE_TO_WINDOW at swapchain creation. Fit protects
    // the content's proportions while an asynchronous window resize completes.
    if (OH_NativeWindow_NativeWindowSetScalingModeV2(window->native, OH_SCALING_MODE_SCALE_FIT_V2) != 0) {
        throw std::runtime_error("Failed to configure aspect-preserving surface presentation.");
    }
}
VkResult createWindowSurface(VkInstance instance, Window* window, VkSurfaceKHR* surface) {
    const auto createSurface = reinterpret_cast<PFN_vkCreateSurfaceOHOS>(
        vkGetInstanceProcAddr(instance, "vkCreateSurfaceOHOS"));
    if (!createSurface) return VK_ERROR_EXTENSION_NOT_PRESENT;
    VkSurfaceCreateInfoOHOS info{};
    info.sType = VK_STRUCTURE_TYPE_SURFACE_CREATE_INFO_OHOS;
    info.window = window->native;
    return createSurface(instance, &info, nullptr, surface);
}
const char* const* requiredWindowExtensions(std::uint32_t* count) {
    static const char* extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_OHOS_SURFACE_EXTENSION_NAME};
    *count = 2;
    return extensions;
}
bool initializeWindowImGui(Window* window) {
    auto& io = ImGui::GetIO();
    io.BackendPlatformName = "vulkcore_ohos";
    io.IniFilename = nullptr;
    window->lastFrame = std::chrono::steady_clock::now();
    return true;
}
void shutdownWindowImGui() { ImGui::GetIO().BackendPlatformName = nullptr; }
void newWindowImGuiFrame(Window* window, VkExtent2D extent) {
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(extent.width), static_cast<float>(extent.height));
    const auto now = std::chrono::steady_clock::now();
    io.DeltaTime = std::clamp(std::chrono::duration<float>(now - window->lastFrame).count(), 0.001f, 0.1f);
    window->lastFrame = now;
}

} // namespace VulkCore
