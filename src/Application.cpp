#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "vulkcore/Application.hpp"

#include <stdexcept>
#include <utility>

#include "vulkcore/Core/Config.hpp"
#include "vulkcore/Renderer/Renderer.hpp"

namespace VulkCore {

Application::Application(std::filesystem::path startupScene, bool hideTools)
    : startupScene_(std::move(startupScene)), hideTools_(hideTools) {}

Application::~Application() {
    renderer_.reset();
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
    }
    glfwTerminate();
}

void Application::run() {
    initWindow(kWindowWidth, kWindowHeight, false);
    renderer_ = std::make_unique<Renderer>(window_, startupScene_, hideTools_);
    mainLoop();
}

void Application::captureFrame(float progress, float time, int width, int height,
                               const std::filesystem::path& output) {
    initWindow(width, height, true);
    renderer_ = std::make_unique<Renderer>(window_, startupScene_, true);
    renderer_->captureTransitionFrame(progress, time, output);
}

void Application::initWindow(int width, int height, bool hidden) {
    if (glfwInit() != GLFW_TRUE) {
        throw std::runtime_error("failed to initialize GLFW!");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, hidden ? GLFW_FALSE : GLFW_TRUE);
#ifdef GLFW_COCOA_RETINA_FRAMEBUFFER
    if (hidden) glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
#endif
    window_ = glfwCreateWindow(
        width,
        height,
        "VulkCore",
        nullptr,
        nullptr
    );
    if (window_ == nullptr) {
        throw std::runtime_error("failed to create GLFW window!");
    }
}

void Application::mainLoop() {
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
        if (glfwWindowShouldClose(window_)) {
            break;
        }
        renderer_->drawFrame();
    }
}

} // namespace VulkCore
