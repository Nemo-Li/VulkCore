#pragma once

#include "vulkcore/Platform/Window.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <mutex>

struct NativeWindow;

namespace VulkCore {

struct Window {
    NativeWindow* native = nullptr;
    std::atomic<int> width{0};
    std::atomic<int> height{0};
    std::mutex inputMutex;
    std::array<bool, 13> keys{};
    bool looking = false;
    double cursorX = 0;
    double cursorY = 0;
    void* userPointer = nullptr;
    ResizeCallback resizeCallback = nullptr;
    std::chrono::steady_clock::time_point lastFrame;
};

} // namespace VulkCore
