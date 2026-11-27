#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>

#include <iterator>

#include "vulkcore/Platform/Window.hpp"

namespace VulkCore {

bool keyPressed(Window* window, Key key) {
    constexpr int keys[] = {GLFW_KEY_A, GLFW_KEY_D, GLFW_KEY_W, GLFW_KEY_S,
        GLFW_KEY_LEFT, GLFW_KEY_RIGHT, GLFW_KEY_UP, GLFW_KEY_DOWN, GLFW_KEY_SPACE,
        GLFW_KEY_Q, GLFW_KEY_E, GLFW_KEY_R, GLFW_KEY_F12, GLFW_KEY_W};
    static_assert(std::size(keys) == static_cast<unsigned>(Key::Count));
    return window && glfwGetKey(window, keys[static_cast<unsigned>(key)]) == GLFW_PRESS;
}
MovementInput analogMovement(Window*) { return {}; }
GamepadInput readGamepad(Window* window) {
    if (!window || !glfwGetWindowAttrib(window, GLFW_FOCUSED)) return {};

    for (int joystick = GLFW_JOYSTICK_1; joystick <= GLFW_JOYSTICK_LAST; ++joystick) {
        GLFWgamepadstate state{};
        if (glfwGetGamepadState(joystick, &state) != GLFW_TRUE) continue;

        GamepadInput input;
        input.movement = gamepadStickInput(state.axes[GLFW_GAMEPAD_AXIS_LEFT_X],
            -state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y]);
        const auto look = gamepadStickInput(state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X],
            -state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y]);
        input.lookRight = look.right;
        input.lookUp = look.forward;
        const auto mapButton = [&](Key key, int button) {
            input.buttons[static_cast<unsigned>(key)] = state.buttons[button] == GLFW_PRESS;
        };
        mapButton(Key::Jump, GLFW_GAMEPAD_BUTTON_A);
        mapButton(Key::SkillQ, GLFW_GAMEPAD_BUTTON_X);
        mapButton(Key::SkillW, GLFW_GAMEPAD_BUTTON_LEFT_BUMPER);
        mapButton(Key::SkillE, GLFW_GAMEPAD_BUTTON_B);
        mapButton(Key::SkillR, GLFW_GAMEPAD_BUTTON_Y);
        return input;
    }
    return {};
}
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
