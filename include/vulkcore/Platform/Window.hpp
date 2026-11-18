#pragma once

#include "vulkcore/Platform/MovementInput.hpp"

#include <cstdint>
#include <vulkan/vulkan.h>

#ifndef VULKCORE_OHOS
struct GLFWwindow;
#endif

namespace VulkCore {

#ifdef VULKCORE_OHOS
struct Window;
#else
using Window = GLFWwindow;
#endif

enum class Key { A, D, W, S, Left, Right, Up, Down, Jump, Lightning, Flame, Tornado, Screenshot };
using ResizeCallback = void (*)(Window*, int, int);

bool keyPressed(Window* window, Key key);
MovementInput analogMovement(Window* window);
bool lookPressed(Window* window);
void cursorPosition(Window* window, double* x, double* y);
void framebufferSize(Window* window, int* width, int* height);
void setWindowUserPointer(Window* window, void* pointer);
void* windowUserPointer(Window* window);
void setResizeCallback(Window* window, ResizeCallback callback);
#ifndef VULKCORE_OHOS
bool windowShouldClose(Window* window);
#endif
void waitWindowEvents();
// Match the native buffer to the component's pixel dimensions before querying WSI.
void prepareWindowSurface(Window* window);
// Apply the platform's aspect-preserving presentation policy after swapchain creation.
void configureWindowPresentation(Window* window);
VkResult createWindowSurface(VkInstance instance, Window* window, VkSurfaceKHR* surface);
const char* const* requiredWindowExtensions(std::uint32_t* count);
bool initializeWindowImGui(Window* window);
void shutdownWindowImGui();
void newWindowImGuiFrame(Window* window, VkExtent2D extent);

} // namespace VulkCore
