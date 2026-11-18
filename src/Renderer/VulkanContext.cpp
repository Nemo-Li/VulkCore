#include "vulkcore/Platform/Window.hpp"

#include "vulkcore/Renderer/VulkanContext.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {

const std::vector<const char*> kValidationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

const std::vector<const char*> kDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

#ifdef NDEBUG
constexpr bool kEnableValidationLayers = false;
#else
constexpr bool kEnableValidationLayers = true;
#endif

VkResult createDebugUtilsMessenger(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* createInfo,
    const VkAllocationCallbacks* allocator,
    VkDebugUtilsMessengerEXT* debugMessenger
) {
    const auto function = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT")
    );
    if (function == nullptr) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    return function(instance, createInfo, allocator, debugMessenger);
}

void destroyDebugUtilsMessenger(
    VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks* allocator
) {
    const auto function = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT")
    );
    if (function != nullptr) {
        function(instance, debugMessenger, allocator);
    }
}

} // namespace

namespace VulkCore {

bool QueueFamilyIndices::isComplete() const {
    return graphicsFamily.has_value() && presentFamily.has_value();
}

VulkanContext::VulkanContext(Window* window)
    : window_(window) {
    if (window_ == nullptr) {
        throw std::invalid_argument("VulkanContext requires a valid platform window!");
    }

    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createCommandPool();
}

VulkanContext::~VulkanContext() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
    if (commandPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
    }
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
    }
    if (debugMessenger_ != VK_NULL_HANDLE) {
        destroyDebugUtilsMessenger(instance_, debugMessenger_, nullptr);
    }
    if (surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
    }
}

VkInstance VulkanContext::instance() const {
    return instance_;
}

VkPhysicalDevice VulkanContext::physicalDevice() const {
    return physicalDevice_;
}

VkDevice VulkanContext::device() const {
    return device_;
}

VkSurfaceKHR VulkanContext::surface() const {
    return surface_;
}

VkQueue VulkanContext::graphicsQueue() const {
    return graphicsQueue_;
}

VkQueue VulkanContext::presentQueue() const {
    return presentQueue_;
}

VkCommandPool VulkanContext::commandPool() const {
    return commandPool_;
}

Window* VulkanContext::window() const {
    return window_;
}

void VulkanContext::waitIdle() const {
    if (device_ != VK_NULL_HANDLE) {
        if (vkDeviceWaitIdle(device_) != VK_SUCCESS) {
            throw std::runtime_error("failed to wait for the Vulkan device!");
        }
    }
}

void VulkanContext::createInstance() {
    if constexpr (kEnableValidationLayers) {
        validationLayersEnabled_ = checkValidationLayerSupport();
        if (!validationLayersEnabled_) {
            std::cerr << "warning: Vulkan validation layers are unavailable; continuing without validation.\n";
        }
    }

    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "VulkCore";
    applicationInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    applicationInfo.pEngineName = "VulkCore Engine";
    applicationInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    applicationInfo.apiVersion = VK_API_VERSION_1_0;

    const auto extensions = getRequiredExtensions();

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &applicationInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    const bool portabilityEnumerationEnabled = std::any_of(
        extensions.begin(),
        extensions.end(),
        [](const char* extension) {
            return std::strcmp(extension, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0;
        }
    );
    if (portabilityEnumerationEnabled) {
        createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (validationLayersEnabled_) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(kValidationLayers.size());
        createInfo.ppEnabledLayerNames = kValidationLayers.data();
        populateDebugMessengerCreateInfo(debugCreateInfo);
        createInfo.pNext = &debugCreateInfo;
    }

    if (vkCreateInstance(&createInfo, nullptr, &instance_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create Vulkan instance!");
    }
}

void VulkanContext::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
}

void VulkanContext::setupDebugMessenger() {
    if (!validationLayersEnabled_) {
        return;
    }

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    populateDebugMessengerCreateInfo(createInfo);
    if (createDebugUtilsMessenger(instance_, &createInfo, nullptr, &debugMessenger_) != VK_SUCCESS) {
        throw std::runtime_error("failed to set up Vulkan debug messenger!");
    }
}

void VulkanContext::createSurface() {
    if (createWindowSurface(instance_, window_, &surface_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface!");
    }
}

void VulkanContext::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
    if (deviceCount == 0) {
        throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());

    for (const VkPhysicalDevice device : devices) {
        if (isDeviceSuitable(device)) {
            physicalDevice_ = device;
            break;
        }
    }

    if (physicalDevice_ == VK_NULL_HANDLE) {
        throw std::runtime_error("failed to find a suitable GPU!");
    }
}

void VulkanContext::createLogicalDevice() {
    const QueueFamilyIndices indices = findQueueFamilies(physicalDevice_);
    const std::set<uint32_t> uniqueQueueFamilies = {
        indices.graphicsFamily.value(),
        indices.presentFamily.value()
    };

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    constexpr float queuePriority = 1.0f;
    for (const uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};
    VkPhysicalDeviceFeatures supportedFeatures{};
    vkGetPhysicalDeviceFeatures(physicalDevice_, &supportedFeatures);
    // Texture samplers use anisotropy when the selected device supports it.
    // Keeping this optional preserves compatibility with software Vulkan
    // implementations and older integrated GPUs.
    deviceFeatures.samplerAnisotropy = supportedFeatures.samplerAnisotropy;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;

    const auto extensions = getRequiredDeviceExtensions(physicalDevice_);
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create logical device!");
    }

    vkGetDeviceQueue(device_, indices.graphicsFamily.value(), 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, indices.presentFamily.value(), 0, &presentQueue_);
}

void VulkanContext::createCommandPool() {
    const QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice_);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

    if (vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool!");
    }
}

bool VulkanContext::isDeviceSuitable(VkPhysicalDevice device) const {
    const QueueFamilyIndices indices = findQueueFamilies(device);
    const bool extensionsSupported = checkDeviceExtensionSupport(device);

    bool swapChainAdequate = false;
    if (extensionsSupported) {
        const SwapChainSupportDetails support = querySwapChainSupport(device);
        swapChainAdequate = !support.formats.empty() && !support.presentModes.empty();
    }

    return indices.isComplete() && extensionsSupported && swapChainAdequate;
}

bool VulkanContext::checkDeviceExtensionSupport(VkPhysicalDevice device) const {
    const auto requiredExtensions = getRequiredDeviceExtensions(device);

    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> required(requiredExtensions.begin(), requiredExtensions.end());
    for (const auto& extension : availableExtensions) {
        required.erase(extension.extensionName);
    }
    return required.empty();
}

std::vector<const char*> VulkanContext::getRequiredExtensions() const {
    uint32_t windowExtensionCount = 0;
    const char* const* windowExtensions = requiredWindowExtensions(&windowExtensionCount);
    if (windowExtensions == nullptr) {
        throw std::runtime_error("Window platform failed to provide required Vulkan extensions!");
    }

    std::vector<const char*> extensions(windowExtensions, windowExtensions + windowExtensionCount);

    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());

    const auto isAvailable = [&availableExtensions](const char* extensionName) {
        return std::any_of(
            availableExtensions.begin(),
            availableExtensions.end(),
            [extensionName](const VkExtensionProperties& extension) {
                return std::strcmp(extension.extensionName, extensionName) == 0;
            }
        );
    };

    if (validationLayersEnabled_) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    if (isAvailable(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    }
    if (isAvailable(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    }

    return extensions;
}

std::vector<const char*> VulkanContext::getRequiredDeviceExtensions(VkPhysicalDevice device) const {
    std::vector<const char*> extensions = kDeviceExtensions;

    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    const auto portabilityExtension = std::find_if(
        availableExtensions.begin(),
        availableExtensions.end(),
        [](const VkExtensionProperties& extension) {
            return std::strcmp(extension.extensionName, "VK_KHR_portability_subset") == 0;
        }
    );
    if (portabilityExtension != availableExtensions.end()) {
        extensions.push_back("VK_KHR_portability_subset");
    }

    return extensions;
}

bool VulkanContext::checkValidationLayerSupport() const {
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    return std::all_of(
        kValidationLayers.begin(),
        kValidationLayers.end(),
        [&availableLayers](const char* layerName) {
            return std::any_of(
                availableLayers.begin(),
                availableLayers.end(),
                [layerName](const VkLayerProperties& layer) {
                    return std::strcmp(layerName, layer.layerName) == 0;
                }
            );
        }
    );
}

QueueFamilyIndices VulkanContext::findQueueFamilies(VkPhysicalDevice device) const {
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &presentSupport);
        if (presentSupport == VK_TRUE) {
            indices.presentFamily = i;
        }

        if (indices.isComplete()) {
            break;
        }
    }

    return indices;
}

SwapChainSupportDetails VulkanContext::querySwapChainSupport(VkPhysicalDevice device) const {
    SwapChainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface_, &details.capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, nullptr);
    if (formatCount > 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &presentModeCount, nullptr);
    if (presentModeCount > 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &presentModeCount, details.presentModes.data());
    }

    return details;
}

uint32_t VulkanContext::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) != 0 &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable Vulkan memory type!");
}

VkCommandBuffer VulkanContext::beginSingleTimeCommands() const {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool_;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device_, &allocInfo, &commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate temporary command buffer!");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
        throw std::runtime_error("failed to begin temporary command buffer!");
    }

    return commandBuffer;
}

void VulkanContext::endSingleTimeCommands(VkCommandBuffer commandBuffer) const {
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
        throw std::runtime_error("failed to record temporary command buffer!");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    if (vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
        throw std::runtime_error("failed to submit temporary command buffer!");
    }
    if (vkQueueWaitIdle(graphicsQueue_) != VK_SUCCESS) {
        vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
        throw std::runtime_error("failed to wait for temporary command buffer!");
    }

    vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
}

void VulkanContext::copyBuffer(VkBuffer sourceBuffer, VkBuffer destinationBuffer, VkDeviceSize size) const {
    const VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, sourceBuffer, destinationBuffer, 1, &copyRegion);

    endSingleTimeCommands(commandBuffer);
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanContext::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* userData
) {
    (void)messageSeverity;
    (void)messageType;
    (void)userData;
    std::cerr << "validation layer: " << callbackData->pMessage << '\n';
    return VK_FALSE;
}

} // namespace VulkCore
