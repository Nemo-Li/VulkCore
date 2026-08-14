#include <vulkan/vulkan.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;

void checkVkResult(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(
            std::string(operation) + " failed with VkResult " + std::to_string(result)
        );
    }
}

bool hasExtension(
    const std::vector<VkExtensionProperties>& extensions,
    const char* extensionName
) {
    for (const auto& extension : extensions) {
        if (std::strcmp(extension.extensionName, extensionName) == 0) {
            return true;
        }
    }
    return false;
}

std::vector<VkExtensionProperties> getAvailableInstanceExtensions() {
    uint32_t extensionCount = 0;
    checkVkResult(
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr),
        "vkEnumerateInstanceExtensionProperties"
    );

    std::vector<VkExtensionProperties> extensions(extensionCount);
    if (extensionCount > 0) {
        checkVkResult(
            vkEnumerateInstanceExtensionProperties(
                nullptr,
                &extensionCount,
                extensions.data()
            ),
            "vkEnumerateInstanceExtensionProperties"
        );
    }
    return extensions;
}

class Application {
public:
    void run() {
        try {
            initWindow();
            initVulkan();
            mainLoop();
        } catch (...) {
            cleanup();
            throw;
        }

        cleanup();
    }

private:
    GLFWwindow* window_ = nullptr;
    bool glfwInitialized_ = false;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;

    void initWindow() {
        if (glfwInit() != GLFW_TRUE) {
            throw std::runtime_error("glfwInit failed");
        }
        glfwInitialized_ = true;

        if (glfwVulkanSupported() != GLFW_TRUE) {
            throw std::runtime_error(
                "GLFW reports that Vulkan is not supported by the current driver"
            );
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        window_ = glfwCreateWindow(
            kWindowWidth,
            kWindowHeight,
            "VulkCore - Vulkan Starter",
            nullptr,
            nullptr
        );
        if (window_ == nullptr) {
            throw std::runtime_error("glfwCreateWindow failed");
        }
    }

    void initVulkan() {
        createInstance();
        createSurface();
        pickPhysicalDevice();

        // GLM is header-only; this small calculation confirms that it is wired in.
        const glm::vec3 sampleVector{1.0F, 2.0F, 3.0F};
        const glm::mat4 identity{1.0F};
        std::cout << "GLM ready: vec3(" << sampleVector.x << ", "
                  << sampleVector.y << ", " << sampleVector.z
                  << "), identity[0][0]=" << identity[0][0] << '\n';
    }

    void createInstance() {
        const VkApplicationInfo applicationInfo{
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pNext = nullptr,
            .pApplicationName = "VulkCore",
            .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
            .pEngineName = "VulkCore",
            .engineVersion = VK_MAKE_VERSION(0, 1, 0),
            .apiVersion = VK_API_VERSION_1_0,
        };

        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        if (glfwExtensions == nullptr || glfwExtensionCount == 0) {
            throw std::runtime_error(
                "glfwGetRequiredInstanceExtensions returned no extensions"
            );
        }

        std::vector<const char*> extensions(
            glfwExtensions,
            glfwExtensions + glfwExtensionCount
        );

        VkInstanceCreateFlags instanceFlags = 0;
        const auto availableExtensions = getAvailableInstanceExtensions();
        if (hasExtension(
                availableExtensions,
                VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
            )) {
            extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            instanceFlags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }

        const VkInstanceCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = instanceFlags,
            .pApplicationInfo = &applicationInfo,
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
            .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data(),
        };

        checkVkResult(vkCreateInstance(&createInfo, nullptr, &instance_), "vkCreateInstance");
    }

    void createSurface() {
        checkVkResult(
            glfwCreateWindowSurface(instance_, window_, nullptr, &surface_),
            "glfwCreateWindowSurface"
        );
    }

    void pickPhysicalDevice() {
        uint32_t deviceCount = 0;
        checkVkResult(
            vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr),
            "vkEnumeratePhysicalDevices"
        );
        if (deviceCount == 0) {
            throw std::runtime_error("No Vulkan-capable physical device was found");
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        checkVkResult(
            vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data()),
            "vkEnumeratePhysicalDevices"
        );

        physicalDevice_ = devices.front();
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
        std::cout << "Vulkan physical device: " << properties.deviceName << '\n';
    }

    void mainLoop() {
        std::cout << "Vulkan starter is running. Close the window to exit.\n";
        while (glfwWindowShouldClose(window_) == GLFW_FALSE) {
            glfwPollEvents();
        }
    }

    void cleanup() {
        if (surface_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
            surface_ = VK_NULL_HANDLE;
        }

        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }

        if (window_ != nullptr) {
            glfwDestroyWindow(window_);
            window_ = nullptr;
        }

        if (glfwInitialized_) {
            glfwTerminate();
            glfwInitialized_ = false;
        }
    }
};

} // namespace

int main() {
    try {
        Application application;
        application.run();
    } catch (const std::exception& error) {
        std::cerr << "VulkCore error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
