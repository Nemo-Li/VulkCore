#include "vulkcore/Platform/OhosWindow.hpp"
#include "vulkcore/Renderer/Renderer.hpp"

#include <ace/xcomponent/native_interface_xcomponent.h>
#include <hilog/log.h>
#include <napi/native_api.h>
#include <rawfile/raw_file.h>
#include <rawfile/raw_file_manager.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
namespace fs = std::filesystem;

// Keep these stage boundaries in sync with the NAPI declaration.
constexpr int kAssetsReady = 75;
constexpr int kLoadingScene = 88;
constexpr int kWaitingForFrame = 97;
constexpr int kFirstFramePresented = 100;

struct Runtime {
    VulkCore::Window window;
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<bool> paused{false};
    std::mutex waitMutex, errorMutex;
    std::condition_variable wake;
    fs::path startup;
    std::string error;
    std::atomic<int> loadingProgress{0};
    std::atomic<bool> prepared{false};
    bool extracting = false; // Accessed only from the NAPI thread.

    ~Runtime() { stop(); }
    void clearInput() {
        std::lock_guard lock(window.inputMutex);
        window.keys.fill(false);
        window.movement = {};
        window.touchLook.reset();
    }
    void stop() {
        running = false;
        wake.notify_all();
        if (worker.joinable()) worker.join();
        clearInput();
    }
    void fail(const std::string& message) {
        std::lock_guard lock(errorMutex);
        error = message;
        OH_LOG_Print(LOG_APP, LOG_ERROR, 0x3200, "VulkCore", "%{public}s", message.c_str());
    }
    void start() {
        if (!prepared || !window.native || worker.joinable()) return;
        running = true;
        worker = std::thread([this] {
            try {
                {
                    std::unique_lock lock(waitMutex);
                    while (running && (window.width <= 0 || window.height <= 0)) {
                        wake.wait_for(lock, std::chrono::milliseconds(50));
                    }
                }
                if (!running) return;
                // Retain the dimensions from before initialization so a layout
                // change during resource loading still rebuilds the swapchain.
                int width = window.width.load(), height = window.height.load();
                bool presented = false;
                VulkCore::Renderer renderer(&window, startup, true, [this] {
                    loadingProgress = kLoadingScene;
                });
                loadingProgress = kWaitingForFrame;
                while (running) {
                    if (paused || window.width <= 0 || window.height <= 0) {
                        std::unique_lock lock(waitMutex);
                        wake.wait_for(lock, std::chrono::milliseconds(50));
                        continue;
                    }
                    const int nextWidth = window.width.load(), nextHeight = window.height.load();
                    if ((nextWidth != width || nextHeight != height) && window.resizeCallback) {
                        width = nextWidth;
                        height = nextHeight;
                        window.resizeCallback(&window, width, height);
                    }
                    if (renderer.drawFrame() && !presented) {
                        presented = true;
                        loadingProgress = kFirstFramePresented;
                    }
                }
            } catch (const std::exception& exception) {
                fail(exception.what());
            }
            running = false;
            window.userPointer = nullptr;
            window.resizeCallback = nullptr;
        });
    }
};
Runtime runtime;

fs::path safeRelativePath(const std::string& name) {
    const fs::path path(name);
    if (path.empty() || path.is_absolute()) throw std::runtime_error("Invalid runtime resource path.");
    for (const auto& part : path) {
        if (part == ".." || part == ".") throw std::runtime_error("Invalid runtime resource path: " + name);
    }
    return path;
}

using RawFilePtr = std::unique_ptr<RawFile, decltype(&OH_ResourceManager_CloseRawFile)>;
RawFilePtr openRaw(NativeResourceManager* manager, const std::string& name) {
    RawFilePtr file(OH_ResourceManager_OpenRawFile(manager, ("vulkcore/" + name).c_str()),
        OH_ResourceManager_CloseRawFile);
    if (!file) throw std::runtime_error("Missing packaged resource: " + name);
    return file;
}

std::string readRawText(NativeResourceManager* manager, const std::string& name) {
    auto file = openRaw(manager, name);
    const auto size = OH_ResourceManager_GetRawFileSize(file.get());
    if (size < 0 || size > 16 * 1024 * 1024) throw std::runtime_error("Invalid manifest size.");
    std::string result(static_cast<std::size_t>(size), '\0');
    std::size_t offset = 0;
    while (offset < result.size()) {
        const int read = OH_ResourceManager_ReadRawFile(file.get(), result.data() + offset, result.size() - offset);
        if (read <= 0) throw std::runtime_error("Cannot read packaged resource: " + name);
        offset += static_cast<std::size_t>(read);
    }
    return result;
}

std::string stringArgument(napi_env env, napi_value value) {
    std::size_t size = 0;
    if (napi_get_value_string_utf8(env, value, nullptr, 0, &size) != napi_ok) {
        throw std::runtime_error("Expected a string argument.");
    }
    std::string result(size + 1, '\0');
    if (napi_get_value_string_utf8(env, value, result.data(), result.size(), &size) != napi_ok) {
        throw std::runtime_error("Cannot read string argument.");
    }
    result.resize(size);
    return result;
}

using ResourceManagerPtr = std::unique_ptr<NativeResourceManager, decltype(&OH_ResourceManager_ReleaseNativeResourceManager)>;

struct ExtractionWork {
    ResourceManagerPtr manager{nullptr, OH_ResourceManager_ReleaseNativeResourceManager};
    napi_async_work handle = nullptr;
    fs::path root;
    fs::path startup;
    std::string error;
};

void extractResources(napi_env, void* data) {
    auto& work = *static_cast<ExtractionWork*>(data);
    try {
        std::istringstream manifest(readRawText(work.manager.get(), "manifest.txt"));
        std::vector<std::string> names;
        std::string name;
        while (std::getline(manifest, name)) {
            if (!name.empty()) names.push_back(std::move(name));
        }
        if (names.empty()) throw std::runtime_error("Packaged resource manifest is empty.");

        std::array<char, 64 * 1024> buffer{};
        fs::create_directories(work.root);
        for (std::size_t index = 0; index < names.size(); ++index) {
            const auto& resource = names[index];
            const auto target = work.root / safeRelativePath(resource);
            fs::create_directories(target.parent_path());
            auto file = openRaw(work.manager.get(), resource);
            std::ofstream output(target, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("Cannot open runtime resource: " + target.string());
            const long size = OH_ResourceManager_GetRawFileSize(file.get());
            if (size < 0) throw std::runtime_error("Invalid resource size: " + resource);
            long remaining = size;
            while (remaining > 0) {
                const int read = OH_ResourceManager_ReadRawFile(file.get(), buffer.data(),
                    std::min<std::size_t>(buffer.size(), static_cast<std::size_t>(remaining)));
                if (read <= 0) throw std::runtime_error("Cannot extract resource: " + resource);
                output.write(buffer.data(), read);
                if (!output) throw std::runtime_error("Cannot write runtime resource: " + target.string());
                remaining -= read;
                const double fileFraction = static_cast<double>(size - remaining) / size;
                runtime.loadingProgress = static_cast<int>((index + fileFraction) * (kAssetsReady - 1) / names.size());
            }
            output.close();
            if (!output) throw std::runtime_error("Cannot close runtime resource: " + target.string());
            runtime.loadingProgress = static_cast<int>((index + 1) * (kAssetsReady - 1) / names.size());
        }
        std::ifstream startup(work.root / "startup.txt");
        if (!std::getline(startup, name)) throw std::runtime_error("Packaged startup scene is missing.");
        work.startup = work.root / safeRelativePath(name);
        if (!fs::is_regular_file(work.startup)) throw std::runtime_error("Packaged startup scene was not found.");
    } catch (const std::exception& exception) {
        work.error = exception.what();
    }
}

void completeExtraction(napi_env env, napi_status status, void* data) {
    std::unique_ptr<ExtractionWork> work(static_cast<ExtractionWork*>(data));
    napi_delete_async_work(env, work->handle);
    runtime.extracting = false;
    try {
        if (!work->error.empty()) {
            runtime.fail(work->error);
            return;
        }
        if (status != napi_ok) throw std::runtime_error("Could not finish extracting application resources.");
        fs::current_path(work->root);
        runtime.startup = std::move(work->startup);
        runtime.prepared = true;
        // ArkUI mounts the surface after observing this milestone.
        runtime.loadingProgress = kAssetsReady;
    } catch (const std::exception& exception) {
        runtime.fail(exception.what());
    }
}

napi_value prepare(napi_env env, napi_callback_info info) {
    if (runtime.extracting) return nullptr;
    try {
        std::size_t argc = 2;
        napi_value args[2]{};
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
        if (argc != 2) throw std::runtime_error("prepare requires ResourceManager and filesDir.");
        runtime.stop();
        runtime.prepared = false;
        runtime.loadingProgress = 0;
        {
            std::lock_guard lock(runtime.errorMutex);
            runtime.error.clear();
        }
        auto work = std::make_unique<ExtractionWork>();
        work->root = fs::path(stringArgument(env, args[1])) / "vulkcore";
        work->manager.reset(OH_ResourceManager_InitNativeResourceManager(env, args[0]));
        if (!work->manager) throw std::runtime_error("Cannot access application resources.");
        napi_value label{};
        if (napi_create_string_utf8(env, "VulkCore resource extraction", NAPI_AUTO_LENGTH, &label) != napi_ok ||
            napi_create_async_work(env, nullptr, label, extractResources, completeExtraction, work.get(), &work->handle) != napi_ok) {
            throw std::runtime_error("Cannot create resource extraction job.");
        }
        if (napi_queue_async_work(env, work->handle) != napi_ok) {
            napi_delete_async_work(env, work->handle);
            throw std::runtime_error("Cannot queue resource extraction job.");
        }
        runtime.extracting = true;
        work.release();
    } catch (const std::exception& exception) {
        runtime.fail(exception.what());
        napi_throw_error(env, nullptr, exception.what());
    }
    return nullptr;
}

napi_value getLoadingProgress(napi_env env, napi_callback_info) {
    napi_value result{};
    napi_create_int32(env, runtime.loadingProgress.load(), &result);
    return result;
}

napi_value setKey(napi_env env, napi_callback_info info) {
    std::size_t argc = 2;
    napi_value args[2]{};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    std::int32_t key = -1;
    bool pressed = false;
    if (argc == 2 && napi_get_value_int32(env, args[0], &key) == napi_ok &&
        napi_get_value_bool(env, args[1], &pressed) == napi_ok && key >= 0 &&
        key < static_cast<std::int32_t>(VulkCore::Key::Count)) {
        std::lock_guard lock(runtime.window.inputMutex);
        runtime.window.keys[static_cast<unsigned>(key)] = pressed;
    }
    return nullptr;
}
napi_value setMovement(napi_env env, napi_callback_info info) {
    std::size_t argc = 2;
    napi_value args[2]{};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    double right = 0, forward = 0;
    VulkCore::MovementInput movement;
    if (argc == 2 && napi_get_value_double(env, args[0], &right) == napi_ok &&
        napi_get_value_double(env, args[1], &forward) == napi_ok) {
        movement = VulkCore::clampMovementInput(right, forward);
    }
    std::lock_guard lock(runtime.window.inputMutex);
    runtime.window.movement = runtime.paused ? VulkCore::MovementInput{} : movement;
    return nullptr;
}
napi_value setPaused(napi_env env, napi_callback_info info) {
    std::size_t argc = 1;
    napi_value arg{};
    bool paused = true;
    napi_get_cb_info(env, info, &argc, &arg, nullptr, nullptr);
    if (argc == 1 && napi_get_value_bool(env, arg, &paused) == napi_ok) {
        runtime.paused = paused;
        if (paused) runtime.clearInput();
        runtime.wake.notify_all();
    }
    return nullptr;
}
napi_value getError(napi_env env, napi_callback_info) {
    std::lock_guard lock(runtime.errorMutex);
    napi_value result{};
    napi_create_string_utf8(env, runtime.error.c_str(), runtime.error.size(), &result);
    return result;
}

void surfaceChanged(OH_NativeXComponent* component, void* native) {
    std::uint64_t width = 0, height = 0;
    if (OH_NativeXComponent_GetXComponentSize(component, native, &width, &height) == OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
        runtime.window.width = static_cast<int>(std::min<std::uint64_t>(width, std::numeric_limits<int>::max()));
        runtime.window.height = static_cast<int>(std::min<std::uint64_t>(height, std::numeric_limits<int>::max()));
        runtime.wake.notify_all();
    }
}
void surfaceCreated(OH_NativeXComponent* component, void* native) {
    runtime.stop();
    runtime.window.native = static_cast<NativeWindow*>(native);
    surfaceChanged(component, native);
    try {
        runtime.start();
    } catch (const std::exception& exception) {
        runtime.stop();
        runtime.fail(exception.what());
    }
}
void surfaceDestroyed(OH_NativeXComponent*, void*) {
    // The renderer and VkSurface must be destroyed before XComponent releases its window.
    runtime.stop();
    if (runtime.prepared) runtime.loadingProgress = kAssetsReady;
    runtime.window.native = nullptr;
    runtime.window.width = 0;
    runtime.window.height = 0;
}
void touch(OH_NativeXComponent* component, void* native) {
    if (runtime.paused || runtime.loadingProgress != kFirstFramePresented) return;
    OH_NativeXComponent_TouchEvent event{};
    if (OH_NativeXComponent_GetTouchEvent(component, native, &event) != OH_NATIVEXCOMPONENT_RESULT_SUCCESS) return;
    VulkCore::TouchPhase phase;
    switch (event.type) {
    case OH_NATIVEXCOMPONENT_DOWN: phase = VulkCore::TouchPhase::Down; break;
    case OH_NATIVEXCOMPONENT_UP: phase = VulkCore::TouchPhase::Up; break;
    case OH_NATIVEXCOMPONENT_MOVE: phase = VulkCore::TouchPhase::Move; break;
    case OH_NATIVEXCOMPONENT_CANCEL: phase = VulkCore::TouchPhase::Cancel; break;
    default: return;
    }
    std::lock_guard lock(runtime.window.inputMutex);
    runtime.window.touchLook.handle(phase, event, runtime.window.width.load());
}

napi_value initialize(napi_env env, napi_value exports) {
    const napi_property_descriptor methods[] = {
        {"prepare", nullptr, prepare, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"getLoadingProgress", nullptr, getLoadingProgress, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"setKey", nullptr, setKey, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"setMovement", nullptr, setMovement, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"setPaused", nullptr, setPaused, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"getError", nullptr, getError, nullptr, nullptr, nullptr, napi_default, nullptr}
    };
    napi_define_properties(env, exports, std::size(methods), methods);
    napi_value object{};
    bool hasComponent = false;
    napi_has_named_property(env, exports, OH_NATIVE_XCOMPONENT_OBJ, &hasComponent);
    if (hasComponent && napi_get_named_property(env, exports, OH_NATIVE_XCOMPONENT_OBJ, &object) == napi_ok) {
        OH_NativeXComponent* component = nullptr;
        if (napi_unwrap(env, object, reinterpret_cast<void**>(&component)) == napi_ok && component) {
            static OH_NativeXComponent_Callback callbacks{surfaceCreated, surfaceChanged, surfaceDestroyed, touch};
            OH_NativeXComponent_RegisterCallback(component, &callbacks);
        }
    }
    return exports;
}

napi_module module = {1, 0, nullptr, initialize, "vulkcore", nullptr, {0}};
__attribute__((constructor)) void registerModule() { napi_module_register(&module); }
} // namespace
