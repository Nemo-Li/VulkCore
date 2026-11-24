#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "vulkcore/Application.hpp"

#ifdef __APPLE__
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "vulkcore/Core/Config.hpp"
#include "vulkcore/Renderer/Renderer.hpp"

namespace VulkCore {
namespace {

std::filesystem::path executablePath() {
#ifdef __APPLE__
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> path(size);
    if (_NSGetExecutablePath(path.data(), &size) == 0) return std::filesystem::canonical(path.data());
#elif defined(_WIN32)
    std::wstring path(32768, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (size > 0 && size < path.size()) {
        path.resize(size);
        return path;
    }
#else
    return std::filesystem::read_symlink("/proc/self/exe");
#endif
    throw std::runtime_error("Could not locate the VulkCore executable.");
}

struct WindowBounds {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

GLFWmonitor* monitorForWindow(const WindowBounds& window) {
    GLFWmonitor* selected = glfwGetPrimaryMonitor();
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    int largestOverlap = 0;
    for (int index = 0; index < count; ++index) {
        GLFWmonitor* monitor = monitors[index];
        int x = 0, y = 0, width = 0, height = 0;
        glfwGetMonitorWorkarea(monitor, &x, &y, &width, &height);
        const int overlapWidth = std::max(0, std::min(window.x + window.width, x + width) - std::max(window.x, x));
        const int overlapHeight = std::max(0, std::min(window.y + window.height, y + height) - std::max(window.y, y));
        const int area = overlapWidth * overlapHeight;
        if (area > largestOverlap) {
            largestOverlap = area;
            selected = monitor;
        }
    }
    return selected;
}

} // namespace

Application::Application(
    std::filesystem::path startupScene, bool hideTools, bool terrainFrustumCullingEnabled
)
    : startupScene_(std::move(startupScene)), hideTools_(hideTools),
      terrainFrustumCullingEnabled_(terrainFrustumCullingEnabled) {}

Application::~Application() {
    renderer_.reset();
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
    }
    glfwTerminate();
}

void Application::run() {
    initWindow(kWindowWidth, kWindowHeight, false);
    renderer_ = std::make_unique<Renderer>(window_, std::filesystem::path{}, hideTools_);
    renderer_->setTerrainFrustumCullingEnabled(terrainFrustumCullingEnabled_);
    if (!startupScene_.empty()) renderer_->queueStartupScene(startupScene_);
    renderer_->initializeResourceBrowser(executablePath().parent_path() / "resource");
    glfwSetDropCallback(window_, [](GLFWwindow* window, int count, const char** paths) {
        auto* renderer = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
        if (!renderer || count <= 0) return;
        std::vector<std::filesystem::path> files;
        files.reserve(count);
        for (int i = 0; i < count; ++i) {
            const std::string_view path(paths[i]);
            files.emplace_back(std::u8string(path.begin(), path.end()));
        }
        double x = 0.0, y = 0.0;
        glfwGetCursorPos(window, &x, &y);
        renderer->queueEditorDrop(std::move(files), x, y);
    });
    mainLoop();
    glfwSetDropCallback(window_, nullptr);
}

void Application::captureFrame(float progress, float time, int width, int height,
                               const std::filesystem::path& output) {
    initWindow(width, height, true);
    renderer_ = std::make_unique<Renderer>(window_, startupScene_, true);
    renderer_->setTerrainFrustumCullingEnabled(terrainFrustumCullingEnabled_);
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
    WindowBounds windowedBounds;
    bool fullscreenKeyDown = false;
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
        if (glfwWindowShouldClose(window_)) {
            break;
        }

        const bool fullscreenPressed = glfwGetKey(window_, GLFW_KEY_F1) == GLFW_PRESS;
        const bool fullscreen = glfwGetWindowMonitor(window_) != nullptr;
        const bool toggleFullscreen = renderer_->hasPendingSceneClose() ? fullscreen :
            (fullscreenPressed && !fullscreenKeyDown);
        if (toggleFullscreen) {
            if (fullscreen) {
                glfwSetWindowMonitor(window_, nullptr, windowedBounds.x, windowedBounds.y,
                    windowedBounds.width, windowedBounds.height, GLFW_DONT_CARE);
            } else {
                glfwGetWindowPos(window_, &windowedBounds.x, &windowedBounds.y);
                glfwGetWindowSize(window_, &windowedBounds.width, &windowedBounds.height);
                GLFWmonitor* monitor = monitorForWindow(windowedBounds);
                if (const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr) {
                    glfwSetWindowMonitor(window_, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
                }
            }
        }
        fullscreenKeyDown = fullscreenPressed;
        renderer_->setFullscreen(glfwGetWindowMonitor(window_) != nullptr);
        renderer_->drawFrame();
    }
}

} // namespace VulkCore
