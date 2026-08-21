#pragma once

#include <filesystem>
#include <memory>

struct GLFWwindow;

namespace VulkCore {

class Renderer;

class Application {
public:
    explicit Application(
        std::filesystem::path startupScene = {},
        bool hideTools = false
    );
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void run();

private:
    void initWindow();
    void mainLoop();

    GLFWwindow* window_ = nullptr;
    std::unique_ptr<Renderer> renderer_;
    std::filesystem::path startupScene_;
    bool hideTools_ = false;
};

} // namespace VulkCore
