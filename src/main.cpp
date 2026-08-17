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

const std::vector<const char *> validationLayers = {
    "VK_LAYER_KHRONOS_validation"};

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDebugUtilsMessengerEXT *pDebugMessenger) {
  auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      instance, "vkCreateDebugUtilsMessengerEXT");
  if (func != nullptr) {
    return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
  } else {
    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance,
                                   VkDebugUtilsMessengerEXT debugMessenger,
                                   const VkAllocationCallbacks *pAllocator) {
  auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      instance, "vkDestroyDebugUtilsMessengerEXT");
  if (func != nullptr) {
    func(instance, debugMessenger, pAllocator);
  }
}

struct QueueFamilyIndices {
  std::optional<uint32_t> graphicsFamily;

  bool isComplete() { return graphicsFamily.has_value(); }
};

void checkVkResult(VkResult result, const char *operation) {
  if (result != VK_SUCCESS) {
    throw std::runtime_error(std::string(operation) + " failed with VkResult " +
                             std::to_string(result));
  }
}

bool hasExtension(const std::vector<VkExtensionProperties> &extensions,
                  const char *extensionName) {
  for (const auto &extension : extensions) {
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
      "vkEnumerateInstanceExtensionProperties");

  std::vector<VkExtensionProperties> extensions(extensionCount);
  if (extensionCount > 0) {
    checkVkResult(vkEnumerateInstanceExtensionProperties(
                      nullptr, &extensionCount, extensions.data()),
                  "vkEnumerateInstanceExtensionProperties");
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
  GLFWwindow *window_ = nullptr;
  bool glfwInitialized_ = false;
  VkInstance instance_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;

  void initWindow() {
    if (glfwInit() != GLFW_TRUE) {
      throw std::runtime_error("glfwInit failed");
    }
    glfwInitialized_ = true;

    if (glfwVulkanSupported() != GLFW_TRUE) {
      throw std::runtime_error(
          "GLFW reports that Vulkan is not supported by the current driver");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window_ = glfwCreateWindow(kWindowWidth, kWindowHeight,
                               "VulkCore - Vulkan Starter", nullptr, nullptr);
    if (window_ == nullptr) {
      throw std::runtime_error("glfwCreateWindow failed");
    }
  }

  void initVulkan() {
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();

    // GLM is header-only; this small calculation confirms that it is wired in.
    const glm::vec3 sampleVector{1.0F, 2.0F, 3.0F};
    const glm::mat4 identity{1.0F};
    std::cout << "GLM ready: vec3(" << sampleVector.x << ", " << sampleVector.y
              << ", " << sampleVector.z
              << "), identity[0][0]=" << identity[0][0] << '\n';
  }

  void createInstance() {
    if (enableValidationLayers && !checkValidationLayerSupport()) {
      throw std::runtime_error(
          "validation layers requested, but not available!");
    }

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
    const char **glfwExtensions =
        glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    if (glfwExtensions == nullptr || glfwExtensionCount == 0) {
      throw std::runtime_error(
          "glfwGetRequiredInstanceExtensions returned no extensions");
    }

    std::vector<const char *> extensions(glfwExtensions,
                                         glfwExtensions + glfwExtensionCount);

    VkInstanceCreateFlags instanceFlags = 0;
    const auto availableExtensions = getAvailableInstanceExtensions();
    if (enableValidationLayers) {
      if (!hasExtension(availableExtensions,
                        VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
        throw std::runtime_error("VK_EXT_debug_utils is not available");
      }

      extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    if (hasExtension(availableExtensions,
                     VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
      extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
      instanceFlags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    std::cout << "available extensions:" << std::endl;
    for (const auto &extension : extensions) {
      std::cout << "\t" << extension << std::endl;
    }

    const VkInstanceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = instanceFlags,
        .pApplicationInfo = &applicationInfo,
        .enabledLayerCount =
            enableValidationLayers
                ? static_cast<uint32_t>(validationLayers.size())
                : 0,
        .ppEnabledLayerNames =
            enableValidationLayers ? validationLayers.data() : nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };

    checkVkResult(vkCreateInstance(&createInfo, nullptr, &instance_),
                  "vkCreateInstance");
  }

  void populateDebugMessengerCreateInfo(
      VkDebugUtilsMessengerCreateInfoEXT &createInfo) {
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback_;
  }

  void setupDebugMessenger() {
    if (!enableValidationLayers)
      return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo;
    populateDebugMessengerCreateInfo(createInfo);

    const VkResult result = CreateDebugUtilsMessengerEXT(
        instance_, &createInfo, nullptr, &debugMessenger_);

    checkVkResult(result, "vkCreateDebugUtilsMessengerEXT");
  }

  void createSurface() {
    checkVkResult(
        glfwCreateWindowSurface(instance_, window_, nullptr, &surface_),
        "glfwCreateWindowSurface");
  }

  void pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    checkVkResult(vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr),
                  "vkEnumeratePhysicalDevices");
    if (deviceCount == 0) {
      throw std::runtime_error("No Vulkan-capable physical device was found");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    checkVkResult(
        vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data()),
        "vkEnumeratePhysicalDevices");

    for (const auto& device : devices) {
        if (isDeviceSuitable(device)) {
            physicalDevice_ = device;
            break;
        }
    }

    // physicalDevice_ = devices.front();
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
    if (enableValidationLayers) {
      DestroyDebugUtilsMessengerEXT(instance_, debugMessenger_, nullptr);
    }

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

  bool checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char *layerName : validationLayers) {
      bool layerFound = false;

      for (const auto &layerProperties : availableLayers) {
        if (strcmp(layerName, layerProperties.layerName) == 0) {
          layerFound = true;
          break;
        }
      }

      if (!layerFound) {
        return false;
      }
    }

    return true;
  }

  static VKAPI_ATTR VkBool32 VKAPI_CALL
  debugCallback_(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                 VkDebugUtilsMessageTypeFlagsEXT messageType,
                 const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                 void *pUserData) {
    std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
  }

  bool isDeviceSuitable(VkPhysicalDevice device) {
    QueueFamilyIndices indices = findQueueFamilies(device);

    return indices.isComplete();
  }

  QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount,
                                             nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount,
                                             queueFamilies.data());

    int i = 0;
    for (const auto &queueFamily : queueFamilies) {
      if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        indices.graphicsFamily = i;
      }

      if (indices.isComplete()) {
        break;
      }

      i++;
    }

    return indices;
  }
};

} // namespace

int main() {
  try {
    Application application;
    application.run();
  } catch (const std::exception &error) {
    std::cerr << "VulkCore error: " << error.what() << '\n';
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
