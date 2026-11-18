#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>

#include "vulkcore/Platform/Window.hpp"

namespace VulkCore {

bool keyPressed(Window* window, Key key) {
    constexpr int keys[] = {GLFW_KEY_A, GLFW_KEY_D, GLFW_KEY_W, GLFW_KEY_S,
        GLFW_KEY_LEFT, GLFW_KEY_RIGHT, GLFW_KEY_UP, GLFW_KEY_DOWN, GLFW_KEY_SPACE,
        GLFW_KEY_Q, GLFW_KEY_E, GLFW_KEY_R, GLFW_KEY_F12};
    return window && glfwGetKey(window, keys[static_cast<unsigned>(key)]) == GLFW_PRESS;
}
MovementInput analogMovement(Window*) { return {}; }
bool lookPressed(Window* window) {
    return window && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
}
void cursorPosition(Window* window, double* x, double* y) { glfwGetCursorPos(window, x, y); }
void framebufferSize(Window* window, int* width, int* height) {
    glfwGetFramebufferSize(window, width, height);
}
void setWindowUserPointer(Window* window, void* pointer) { glfwSetWindowUserPointer(window, pointer); }
void* windowUserPointer(Window* window) { return glfwGetWindowUserPointer(window); }
void setResizeCallback(Window* window, ResizeCallback callback) { glfwSetFramebufferSizeCallback(window, callback); }
bool windowShouldClose(Window* window) { return glfwWindowShouldClose(window) != 0; }
void waitWindowEvents() { glfwWaitEvents(); }
void prepareWindowSurface(Window*) {}
void configureWindowPresentation(Window*) {}
VkResult createWindowSurface(VkInstance instance, Window* window, VkSurfaceKHR* surface) {
    return glfwCreateWindowSurface(instance, window, nullptr, surface);
}
const char* const* requiredWindowExtensions(std::uint32_t* count) {
    return glfwGetRequiredInstanceExtensions(count);
}
bool initializeWindowImGui(Window* window) { return ImGui_ImplGlfw_InitForVulkan(window, true); }
void shutdownWindowImGui() { ImGui_ImplGlfw_Shutdown(); }
void newWindowImGuiFrame(Window*, VkExtent2D) { ImGui_ImplGlfw_NewFrame(); }

} // namespace VulkCore
