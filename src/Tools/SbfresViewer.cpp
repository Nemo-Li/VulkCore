#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "vulkcore/Renderer/Renderer.hpp"

namespace {

struct GlfwSession {
    GlfwSession() {
        if (glfwInit() != GLFW_TRUE) throw std::runtime_error("failed to initialize GLFW");
    }
    ~GlfwSession() { glfwTerminate(); }
};

void queueOpenFiles(VulkCore::Renderer& renderer, int count, const char* const* filenames) {
    std::vector<std::filesystem::path> paths;
    paths.reserve(count);
    for (int i = 0; i < count; ++i) {
        const std::string_view utf8(filenames[i]);
        paths.emplace_back(std::u8string(utf8.begin(), utf8.end()));
    }
    renderer.queueModelViewerDrop(std::move(paths));
}

void onDrop(GLFWwindow* window, int count, const char** filenames) {
    auto* renderer = static_cast<VulkCore::Renderer*>(glfwGetWindowUserPointer(window));
    queueOpenFiles(*renderer, count, filenames);
}

void printUsage(std::ostream& out) {
    out << "Usage: sbfres_viewer [model.sbfres [model.Tex.sbfres]]\n"
        << "Drop a model onto the window, drag with the left mouse button to orbit,\n"
        << "scroll to zoom, or press F to fit the model.\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        printUsage(std::cout);
        return EXIT_SUCCESS;
    }
    if (argc > 3) {
        printUsage(std::cerr);
        return EXIT_FAILURE;
    }
    try {
        const GlfwSession glfw;
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        const std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window(
            glfwCreateWindow(1100, 760, "SBFRES Viewer", nullptr, nullptr), glfwDestroyWindow);
        if (!window) throw std::runtime_error("failed to create GLFW window");
        glfwSetWindowSizeLimits(window.get(), 420, 300, GLFW_DONT_CARE, GLFW_DONT_CARE);

        VulkCore::Renderer renderer(window.get(), {}, false, {}, VulkCore::Renderer::Mode::ModelViewer);
        glfwSetDropCallback(window.get(), onDrop);
        if (argc > 1) queueOpenFiles(renderer, argc - 1, argv + 1);
        while (!glfwWindowShouldClose(window.get())) {
            glfwPollEvents();
            if (!glfwWindowShouldClose(window.get())) renderer.drawFrame();
        }
        glfwSetDropCallback(window.get(), nullptr);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "sbfres_viewer: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
