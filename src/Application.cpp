#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "vulkcore/Application.hpp"

#include <stdexcept>

#include "vulkcore/Core/Config.hpp"
#include "vulkcore/Renderer/Renderer.hpp"

namespace VulkCore {

Application::Application() = default;

Application::~Application() {
    renderer_.reset();
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
    }
    glfwTerminate();
}

void Application::run() {
    initWindow();
    renderer_ = std::make_unique<Renderer>(window_);
    mainLoop();
}

void Application::initWindow() {
    if (glfwInit() != GLFW_TRUE) {
        throw std::runtime_error("failed to initialize GLFW!");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window_ = glfwCreateWindow(
        static_cast<int>(kWindowWidth),
        static_cast<int>(kWindowHeight),
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
        renderer_->drawFrame();
    }
}

} // namespace VulkCore
