#pragma once

#include <coroutine>
#include <exception>
#include <utility>

namespace VulkCore {

// A renderer-owned sequence. Each yield lets the desktop event loop present
// a loading frame before the next step touches live scene or Vulkan state.
class SceneLoadTask {
public:
    struct promise_type;
    using Handle = std::coroutine_handle<promise_type>;

    struct promise_type {
        int progress = 0;
        bool succeeded = false;
        std::exception_ptr error;

        SceneLoadTask get_return_object() { return SceneLoadTask(Handle::from_promise(*this)); }
        std::suspend_always initial_suspend() const noexcept { return {}; }
        std::suspend_always final_suspend() const noexcept { return {}; }
        std::suspend_always yield_value(int value) noexcept {
            progress = value;
            return {};
        }
        void return_value(bool success) noexcept { succeeded = success; }
        void unhandled_exception() noexcept { error = std::current_exception(); }
    };

    SceneLoadTask(SceneLoadTask&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    SceneLoadTask& operator=(SceneLoadTask&&) = delete;
    ~SceneLoadTask() { if (handle_) handle_.destroy(); }

    bool advance() {
        if (!handle_.done()) handle_.resume();
        if (handle_.promise().error) std::rethrow_exception(handle_.promise().error);
        return !handle_.done();
    }
    bool succeeded() const { return handle_.promise().succeeded; }
    int progress() const { return handle_.promise().progress; }

private:
    explicit SceneLoadTask(Handle handle) : handle_(handle) {}
    Handle handle_;
};

} // namespace VulkCore
