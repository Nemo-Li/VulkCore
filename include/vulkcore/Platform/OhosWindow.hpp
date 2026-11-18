#pragma once

#include "vulkcore/Platform/Window.hpp"
#include "vulkcore/Platform/TouchLook.hpp"

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
    TouchLook touchLook;
    void* userPointer = nullptr;
    ResizeCallback resizeCallback = nullptr;
    std::chrono::steady_clock::time_point lastFrame;
};

} // namespace VulkCore
