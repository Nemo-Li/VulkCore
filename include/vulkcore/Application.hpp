#pragma once

#include <memory>

struct GLFWwindow;

namespace VulkCore {

class Renderer;

class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void run();

private:
    void initWindow();
    void mainLoop();

    GLFWwindow* window_ = nullptr;
    std::unique_ptr<Renderer> renderer_;
};

} // namespace VulkCore
