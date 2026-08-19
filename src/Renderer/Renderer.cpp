#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "vulkcore/Renderer/Renderer.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <stdexcept>
#include <string_view>

#include "vulkcore/Core/Config.hpp"
#include "vulkcore/Renderer/Buffer.hpp"
#include "vulkcore/Renderer/SwapChain.hpp"
#include "vulkcore/Renderer/Texture.hpp"
#include "vulkcore/Renderer/UniformBuffer.hpp"
#include "vulkcore/Renderer/Vertex.hpp"
#include "vulkcore/Renderer/VulkanContext.hpp"
#include "vulkcore/Utils/FileUtils.hpp"
#include "vulkcore/Utils/ModelLoader.hpp"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace VulkCore {

namespace {

struct MaterialPushConstants {
    glm::mat4 nodeTransform{1.0f};
    glm::vec4 baseColor;
    std::uint32_t hasTexture = 0;
    std::array<std::uint32_t, 3> padding{};
};

constexpr float kMinimumModelScaleFactor = 0.1f;
constexpr float kMaximumModelScaleFactor = 3.0f;
constexpr char kDefaultVertexShader[] = "shader_base.vert";
constexpr char kDefaultFragmentShader[] = "shader_base.frag";
constexpr std::array<std::uint32_t, 24> kBoundingBoxEdges = {
    0, 1, 1, 2, 2, 3, 3, 0,
    4, 5, 5, 6, 6, 7, 7, 4,
    0, 4, 1, 5, 2, 6, 3, 7
};

const char* shaderStageLabel(ShaderStage stage) {
    switch (stage) {
    case ShaderStage::Vertex:
        return "Vertex shader";
    case ShaderStage::TessellationControl:
        return "Tessellation control";
    case ShaderStage::TessellationEvaluation:
        return "Tessellation evaluation";
    case ShaderStage::Geometry:
        return "Geometry shader";
    case ShaderStage::Fragment:
        return "Fragment shader";
    case ShaderStage::Compute:
        return "Compute shader";
    }
    return "Shader";
}

const char* shaderStageExtension(ShaderStage stage) {
    switch (stage) {
    case ShaderStage::Vertex:
        return ".vert";
    case ShaderStage::TessellationControl:
        return ".tesc";
    case ShaderStage::TessellationEvaluation:
        return ".tese";
    case ShaderStage::Geometry:
        return ".geom";
    case ShaderStage::Fragment:
        return ".frag";
    case ShaderStage::Compute:
        return ".comp";
    }
    return "";
}

VkShaderStageFlagBits vkShaderStage(ShaderStage stage) {
    switch (stage) {
    case ShaderStage::Vertex:
        return VK_SHADER_STAGE_VERTEX_BIT;
    case ShaderStage::TessellationControl:
        return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    case ShaderStage::TessellationEvaluation:
        return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    case ShaderStage::Geometry:
        return VK_SHADER_STAGE_GEOMETRY_BIT;
    case ShaderStage::Fragment:
        return VK_SHADER_STAGE_FRAGMENT_BIT;
    case ShaderStage::Compute:
        return VK_SHADER_STAGE_COMPUTE_BIT;
    }
    return VK_SHADER_STAGE_VERTEX_BIT;
}

bool shaderStageForExtension(const std::string& extension, ShaderStage& stage) {
    for (std::size_t index = 0; index < kShaderStageCount; ++index) {
        const auto candidate = static_cast<ShaderStage>(index);
        if (extension == shaderStageExtension(candidate)) {
            stage = candidate;
            return true;
        }
    }
    return false;
}

const char* defaultShaderForStage(ShaderStage stage) {
    switch (stage) {
    case ShaderStage::Vertex:
        return kDefaultVertexShader;
    case ShaderStage::Fragment:
        return kDefaultFragmentShader;
    default:
        return "";
    }
}

ModelNode* findModelNodeById(ModelNode& node, std::uint32_t nodeId) {
    if (node.nodeId == nodeId) {
        return &node;
    }
    for (ModelNode& child : node.children) {
        if (ModelNode* result = findModelNodeById(child, nodeId); result != nullptr) {
            return result;
        }
    }
    return nullptr;
}

bool hasNodeAnimationSettings(const ModelNode& node) {
    return node.rotationAnimationEnabled ||
        node.rotationAnimationInitialDegrees != glm::vec3(0.0f) ||
        node.rotationAnimationSpeedDegrees != glm::vec3(0.0f);
}

bool hasNodeShaderSettings(const ModelNode& node) {
    return !node.shaderConfiguration.empty();
}

void saveNodeAnimationSettings(std::ostream& file, const ModelNode& node) {
    if (hasNodeAnimationSettings(node)) {
        file << "node_animation " << node.nodeId << ' '
             << (node.rotationAnimationEnabled ? 1 : 0) << ' '
             << node.rotationAnimationInitialDegrees.x << ' '
             << node.rotationAnimationInitialDegrees.y << ' '
             << node.rotationAnimationInitialDegrees.z << ' '
             << node.rotationAnimationSpeedDegrees.x << ' '
             << node.rotationAnimationSpeedDegrees.y << ' '
             << node.rotationAnimationSpeedDegrees.z << "\n";
    }
    for (const ModelNode& child : node.children) {
        saveNodeAnimationSettings(file, child);
    }
}

void saveNodeShaderSettings(std::ostream& file, const ModelNode& node) {
    if (hasNodeShaderSettings(node)) {
        file << "node_shaders " << node.nodeId;
        for (const std::string& shaderName : node.shaderConfiguration.shaderNames) {
            file << ' ' << (shaderName.empty() ? "-" : shaderName);
        }
        file << "\n";
    }
    for (const ModelNode& child : node.children) {
        saveNodeShaderSettings(file, child);
    }
}

std::string shaderConfigToken(const std::string& token) {
    return token == "-" ? std::string{} : token;
}

} // namespace

void Renderer::selectModelNode(ModelNode& node) {
    selectedModelNode_ = &node;
    updateBoundingBoxVertices();
}

glm::mat4 Renderer::nodeAnimationTransform(
    const ModelNode& node,
    float elapsedSeconds
) const {
    if (!node.rotationAnimationEnabled) {
        return glm::mat4(1.0f);
    }

    const glm::vec3 rotationDegrees = node.rotationAnimationInitialDegrees +
        node.rotationAnimationSpeedDegrees * elapsedSeconds;
    const glm::mat4 rotation =
        glm::rotate(glm::mat4(1.0f), glm::radians(rotationDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f)) *
        glm::rotate(glm::mat4(1.0f), glm::radians(rotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f)) *
        glm::rotate(glm::mat4(1.0f), glm::radians(rotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));

    // The flattened vertex buffer is in model-root coordinates. Conjugating
    // the local rotation by the node's global transform makes the selected
    // node's own coordinate system the animation frame.
    return node.globalTransform * rotation * glm::inverse(node.globalTransform);
}

glm::mat4 Renderer::animationTransformForNode(
    const ModelNode& node,
    std::uint32_t targetNodeId,
    float elapsedSeconds,
    const glm::mat4& parentAnimation,
    bool& found
) const {
    glm::mat4 accumulatedAnimation = parentAnimation;
    if (node.rotationAnimationEnabled) {
        // Parent animation is applied after child-local animation, so the
        // matrices are accumulated in hierarchy order from root to leaf.
        accumulatedAnimation *= nodeAnimationTransform(node, elapsedSeconds);
    }

    if (node.nodeId == targetNodeId) {
        found = true;
        return accumulatedAnimation;
    }

    for (const ModelNode& child : node.children) {
        const glm::mat4 result = animationTransformForNode(
            child,
            targetNodeId,
            elapsedSeconds,
            accumulatedAnimation,
            found
        );
        if (found) {
            return result;
        }
    }
    return glm::mat4(1.0f);
}

void Renderer::updateBoundingBoxVertices() {
    boundingBoxVertices_.fill(Vertex{});
    if (selectedModelNode_ == nullptr || !selectedModelNode_->hasBounds) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const float elapsedSeconds = std::chrono::duration<float>(now - startTime_).count();
    bool found = false;
    const glm::mat4 nodeTransform = animationTransformForNode(
        meshData_.hierarchy,
        selectedModelNode_->nodeId,
        elapsedSeconds,
        glm::mat4(1.0f),
        found
    );

    const glm::vec3& minimum = selectedModelNode_->boundsMinimum;
    const glm::vec3& maximum = selectedModelNode_->boundsMaximum;
    const std::array<glm::vec3, 8> corners = {
        glm::vec3(minimum.x, minimum.y, minimum.z),
        glm::vec3(maximum.x, minimum.y, minimum.z),
        glm::vec3(maximum.x, maximum.y, minimum.z),
        glm::vec3(minimum.x, maximum.y, minimum.z),
        glm::vec3(minimum.x, minimum.y, maximum.z),
        glm::vec3(maximum.x, minimum.y, maximum.z),
        glm::vec3(maximum.x, maximum.y, maximum.z),
        glm::vec3(minimum.x, maximum.y, maximum.z)
    };

    for (std::size_t vertexIndex = 0; vertexIndex < kBoundingBoxEdges.size(); ++vertexIndex) {
        boundingBoxVertices_[vertexIndex].position = glm::vec3(
            nodeTransform * glm::vec4(corners[kBoundingBoxEdges[vertexIndex]], 1.0f)
        );
    }
}

void Renderer::drawModelHierarchy(ModelNode& node) {
    const bool hasChildren = !node.children.empty();
    const bool isRootNode = &node == &meshData_.hierarchy;
    const bool nodeSelected = selectedModelNode_ == &node;
    std::string label = node.name.empty() ? "<unnamed>" : node.name;
    if (!isRootNode && node.meshCount > 0) {
        label += "  [" + std::to_string(node.meshCount) +
            (node.meshCount == 1 ? " mesh]" : " meshes]");
    }

    ImGui::PushID(&node);
    if (!hasChildren) {
        ImGui::TreeNodeEx(
            label.c_str(),
            ImGuiTreeNodeFlags_Leaf |
                ImGuiTreeNodeFlags_NoTreePushOnOpen |
                ImGuiTreeNodeFlags_SpanAvailWidth |
                (nodeSelected ? ImGuiTreeNodeFlags_Selected : 0)
        );
        if (ImGui::IsItemClicked()) {
            selectModelNode(node);
        }
    } else {
        const bool isOpen = ImGui::TreeNodeEx(
            label.c_str(),
            ImGuiTreeNodeFlags_SpanAvailWidth |
                (nodeSelected ? ImGuiTreeNodeFlags_Selected : 0)
        );
        if (ImGui::IsItemClicked()) {
            selectModelNode(node);
        }
        if (!isOpen) {
            ImGui::PopID();
            return;
        }

        for (ModelNode& child : node.children) {
            drawModelHierarchy(child);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void Renderer::drawNodeShaderControls(ModelNode& node, bool& shouldSaveSettings) {
    ImGui::SeparatorText("Node Shaders");
    ImGui::TextDisabled("Empty stages fall back to the default shader.");

    const std::array<ShaderStage, kShaderStageCount> stages = {
        ShaderStage::Vertex,
        ShaderStage::TessellationControl,
        ShaderStage::TessellationEvaluation,
        ShaderStage::Geometry,
        ShaderStage::Fragment,
        ShaderStage::Compute
    };
    for (const ShaderStage stage : stages) {
        std::string preview = node.shaderConfiguration.shader(stage).empty()
            ? "<Default>"
            : node.shaderConfiguration.shader(stage);
        ImGui::PushID(static_cast<int>(stage));
        if (ImGui::BeginCombo(shaderStageLabel(stage), preview.c_str())) {
            const bool defaultSelected = node.shaderConfiguration.shader(stage).empty();
            if (ImGui::Selectable("<Default>", defaultSelected)) {
                node.shaderConfiguration.shader(stage).clear();
                shouldSaveSettings = true;
            }
            if (defaultSelected) {
                ImGui::SetItemDefaultFocus();
            }

            const auto& shaders = availableShaders_[static_cast<std::size_t>(stage)];
            for (const std::string& shaderName : shaders) {
                const bool selected = node.shaderConfiguration.shader(stage) == shaderName;
                if (ImGui::Selectable(shaderName.c_str(), selected)) {
                    node.shaderConfiguration.shader(stage) = shaderName;
                    shouldSaveSettings = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }

    if (ImGui::Button("Apply water shader")) {
        node.shaderConfiguration = NodeShaderConfiguration{};
        node.shaderConfiguration.shader(ShaderStage::Vertex) = "shader_water.vert";
        node.shaderConfiguration.shader(ShaderStage::Fragment) = "shader_water.frag";
        shouldSaveSettings = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply flowing water")) {
        node.shaderConfiguration = NodeShaderConfiguration{};
        node.shaderConfiguration.shader(ShaderStage::Vertex) = "shader_water.vert";
        node.shaderConfiguration.shader(ShaderStage::Fragment) = "shader_water_flow.frag";
        shouldSaveSettings = true;
    }

    if (node.shaderConfiguration.hasComputeOverride()) {
        if (computePipelineForNode(node) != VK_NULL_HANDLE) {
            ImGui::TextDisabled("Compute pipeline ready (not dispatched by the mesh pass).");
        } else {
            ImGui::TextColored(
                ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
                "Compute shader pipeline could not be created."
            );
        }
    }
    if (ImGui::Button("Reset node shaders")) {
        node.shaderConfiguration = NodeShaderConfiguration{};
        shouldSaveSettings = true;
    }
}

Renderer::Renderer(GLFWwindow* window)
    : startTime_(std::chrono::steady_clock::now()) {
    if (window == nullptr) {
        throw std::invalid_argument("Renderer requires a valid GLFW window!");
    }

    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);

    context_ = std::make_unique<VulkanContext>(window);
    swapChain_ = std::make_unique<SwapChain>(*context_);

    createRenderPass();
    createDescriptorSetLayout();
    createGraphicsPipeline();
    createBoundingBoxPipeline();
    swapChain_->createFramebuffers(renderPass_);
    createMeshData();
    loadTransformSettings();
    createVertexBuffer();
    createIndexBuffer();
    createBoundingBoxBuffers();
    createMaterialTextures();
    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();
    createCommandBuffers();
    createSyncObjects();
    initializeImGui();
}

Renderer::~Renderer() {
    if (!context_) {
        return;
    }

    saveTransformSettings();
    context_->waitIdle();
    shutdownImGui();
    swapChain_->cleanupFramebuffers();
    uniformBuffers_.clear();
    materialTextures_.clear();
    fallbackTexture_.reset();
    indexBuffer_.reset();
    vertexBuffer_.reset();
    boundingBoxVertexBuffers_.clear();

    for (size_t i = 0; i < imageAvailableSemaphores_.size(); ++i) {
        vkDestroySemaphore(context_->device(), imageAvailableSemaphores_[i], nullptr);
        vkDestroySemaphore(context_->device(), renderFinishedSemaphores_[i], nullptr);
        vkDestroyFence(context_->device(), inFlightFences_[i], nullptr);
    }

    for (const auto& [key, pipeline] : nodeGraphicsPipelines_) {
        (void)key;
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(context_->device(), pipeline, nullptr);
        }
    }
    for (const auto& [key, pipeline] : nodeComputePipelines_) {
        (void)key;
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(context_->device(), pipeline, nullptr);
        }
    }
    if (graphicsPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(context_->device(), graphicsPipeline_, nullptr);
    }
    if (boundingBoxPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(context_->device(), boundingBoxPipeline_, nullptr);
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(context_->device(), pipelineLayout_, nullptr);
    }
    for (const auto& [shaderName, shaderModuleHandle] : shaderModules_) {
        (void)shaderName;
        if (shaderModuleHandle != VK_NULL_HANDLE) {
            vkDestroyShaderModule(context_->device(), shaderModuleHandle, nullptr);
        }
    }
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(context_->device(), descriptorPool_, nullptr);
    }
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(context_->device(), descriptorSetLayout_, nullptr);
    }
    if (renderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(context_->device(), renderPass_, nullptr);
    }

    swapChain_.reset();
    context_.reset();
}

void Renderer::initializeImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    if (!ImGui_ImplGlfw_InitForVulkan(context_->window(), true)) {
        ImGui::DestroyContext();
        throw std::runtime_error("failed to initialize Dear ImGui GLFW backend!");
    }

    const QueueFamilyIndices queueFamilies =
        context_->findQueueFamilies(context_->physicalDevice());
    const uint32_t imageCount = static_cast<uint32_t>(swapChain_->framebuffers().size());

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_0;
    initInfo.Instance = context_->instance();
    initInfo.PhysicalDevice = context_->physicalDevice();
    initInfo.Device = context_->device();
    initInfo.QueueFamily = queueFamilies.graphicsFamily.value();
    initInfo.Queue = context_->graphicsQueue();
    // Let the backend create a private pool with the flags it needs for
    // texture descriptor updates. The engine's material pool stays isolated.
    initInfo.DescriptorPoolSize = 1000;
    initInfo.MinImageCount = std::max(2u, imageCount);
    initInfo.ImageCount = std::max(2u, imageCount);
    initInfo.PipelineInfoMain.RenderPass = renderPass_;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        throw std::runtime_error("failed to initialize Dear ImGui Vulkan backend!");
    }

    imguiInitialized_ = true;
}

void Renderer::shutdownImGui() {
    if (!imguiInitialized_) {
        return;
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    imguiInitialized_ = false;
}

void Renderer::loadTransformSettings() {
    const std::filesystem::path configPath(kConfigPath);
    std::ifstream file(configPath);
    if (!file.is_open()) {
        return;
    }

    glm::vec3 translation = modelTranslation_;
    glm::vec3 rotationDegrees = modelRotationDegrees_;
    float scaleFactor = modelScaleFactor_;
    struct PendingNodeAnimation {
        std::uint32_t nodeId = 0;
        bool enabled = false;
        glm::vec3 initialDegrees{0.0f};
        glm::vec3 speedDegrees{0.0f};
    };
    struct PendingNodeShaders {
        std::uint32_t nodeId = 0;
        NodeShaderConfiguration configuration;
    };
    std::vector<PendingNodeAnimation> pendingNodeAnimations;
    std::vector<PendingNodeShaders> pendingNodeShaders;
    bool valid = true;
    std::string key;
    while (file >> key) {
        if (key == "position") {
            if (!(file >> translation.x >> translation.y >> translation.z)) {
                valid = false;
                break;
            }
        } else if (key == "rotation_degrees") {
            if (!(file >> rotationDegrees.x >> rotationDegrees.y >> rotationDegrees.z)) {
                valid = false;
                break;
            }
        } else if (key == "scale") {
            if (!(file >> scaleFactor)) {
                valid = false;
                break;
            }
        } else if (key == "node_animation") {
            PendingNodeAnimation animation;
            int enabled = 0;
            if (!(file >> animation.nodeId >> enabled >>
                  animation.initialDegrees.x >> animation.initialDegrees.y >>
                  animation.initialDegrees.z >> animation.speedDegrees.x >>
                  animation.speedDegrees.y >> animation.speedDegrees.z)) {
                valid = false;
                break;
            }
            if (enabled != 0 && enabled != 1) {
                valid = false;
                break;
            }
            animation.enabled = enabled != 0;
            pendingNodeAnimations.push_back(animation);
        } else if (key == "node_shaders") {
            PendingNodeShaders shaders;
            if (!(file >> shaders.nodeId)) {
                valid = false;
                break;
            }
            for (std::string& shaderName : shaders.configuration.shaderNames) {
                std::string token;
                if (!(file >> token)) {
                    valid = false;
                    break;
                }
                shaderName = shaderConfigToken(token);
            }
            if (!valid) {
                break;
            }
            pendingNodeShaders.push_back(std::move(shaders));
        } else {
            std::string ignoredLine;
            std::getline(file, ignoredLine);
        }
    }

    const bool finiteValues =
        std::isfinite(translation.x) && std::isfinite(translation.y) &&
        std::isfinite(translation.z) && std::isfinite(rotationDegrees.x) &&
        std::isfinite(rotationDegrees.y) && std::isfinite(rotationDegrees.z) &&
        std::isfinite(scaleFactor);
    const bool finiteNodeValues = std::all_of(
        pendingNodeAnimations.begin(),
        pendingNodeAnimations.end(),
        [](const PendingNodeAnimation& animation) {
            return std::isfinite(animation.initialDegrees.x) &&
                std::isfinite(animation.initialDegrees.y) &&
                std::isfinite(animation.initialDegrees.z) &&
                std::isfinite(animation.speedDegrees.x) &&
                std::isfinite(animation.speedDegrees.y) &&
                std::isfinite(animation.speedDegrees.z);
        }
    );
    if (!valid || !finiteValues || !finiteNodeValues) {
        std::cerr << "warning: ignoring invalid VulkCore transform settings in '"
                  << configPath.string() << "'\n";
        return;
    }

    modelTranslation_ = translation;
    modelRotationDegrees_ = rotationDegrees;
    modelScaleFactor_ = std::clamp(
        scaleFactor,
        kMinimumModelScaleFactor,
        kMaximumModelScaleFactor
    );
    for (const PendingNodeAnimation& animation : pendingNodeAnimations) {
        ModelNode* node = findModelNodeById(meshData_.hierarchy, animation.nodeId);
        if (node == nullptr) {
            continue;
        }
        node->rotationAnimationEnabled = animation.enabled;
        node->rotationAnimationInitialDegrees = animation.initialDegrees;
        node->rotationAnimationSpeedDegrees = animation.speedDegrees;
    }
    for (const PendingNodeShaders& shaders : pendingNodeShaders) {
        ModelNode* node = findModelNodeById(meshData_.hierarchy, shaders.nodeId);
        if (node == nullptr) {
            continue;
        }
        node->shaderConfiguration = shaders.configuration;
    }
}

void Renderer::saveTransformSettings() const {
    const std::filesystem::path configPath(kConfigPath);
    std::ofstream file(configPath, std::ios::trunc);
    if (!file.is_open()) {
        std::cerr << "warning: failed to save VulkCore transform settings to '"
                  << configPath.string() << "'\n";
        return;
    }

    file << "# VulkCore transform settings\n"
         << std::setprecision(9)
         << "position " << modelTranslation_.x << ' '
         << modelTranslation_.y << ' ' << modelTranslation_.z << "\n"
         << "rotation_degrees " << modelRotationDegrees_.x << ' '
         << modelRotationDegrees_.y << ' ' << modelRotationDegrees_.z << "\n"
         << "scale " << modelScaleFactor_ << "\n";
    saveNodeAnimationSettings(file, meshData_.hierarchy);
    saveNodeShaderSettings(file, meshData_.hierarchy);
}

void Renderer::buildImGuiFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    constexpr ImGuiWindowFlags kMainWindowFlags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;

    ImGui::Begin("VulkCore Main Screen", nullptr, kMainWindowFlags);
    constexpr float kPanelMargin = 16.0f;
    constexpr float kLeftPanelWidth = 300.0f;
    constexpr float kRightPanelWidth = 360.0f;

    ImGui::SetCursorPos(ImVec2(kPanelMargin, kPanelMargin));
    ImGui::BeginChild("SceneControls", ImVec2(kLeftPanelWidth, 0.0f), true);
    ImGui::TextUnformatted("VulkCore");
    ImGui::TextDisabled("Vulkan scene rendered with Dear ImGui UI");
    ImGui::Separator();
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Text("Frame time: %.2f ms", 1000.0f / std::max(ImGui::GetIO().Framerate, 0.001f));
    ImGui::SeparatorText("Transform Debug");
    bool shouldSaveTransformSettings = false;
    ImGui::DragFloat3(
        "Position (X/Y/Z)",
        &modelTranslation_.x,
        0.01f,
        -10.0f,
        10.0f,
        "%.2f"
    );
    shouldSaveTransformSettings |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::DragFloat3(
        "Rotation (X/Y/Z)",
        &modelRotationDegrees_.x,
        1.0f,
        -360.0f,
        360.0f,
        "%.1f deg"
    );
    shouldSaveTransformSettings |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat(
        "Model Scale",
        &modelScaleFactor_,
        kMinimumModelScaleFactor,
        kMaximumModelScaleFactor,
        "%.2fx"
    );
    shouldSaveTransformSettings |= ImGui::IsItemDeactivatedAfterEdit();
    if (ImGui::Button("Reset transform")) {
        modelTranslation_ = glm::vec3(0.0f);
        modelRotationDegrees_ = glm::vec3(0.0f);
        modelScaleFactor_ = 1.0f;
        shouldSaveTransformSettings = true;
    }
    if (selectedModelNode_ != nullptr) {
        ImGui::SeparatorText("Selected Node");
        ImGui::Text("Node: %s", selectedModelNode_->name.c_str());
        ImGui::TextDisabled("Rotation uses the node's local model axes.");

        ImGui::Checkbox(
            "Enable rotation animation",
            &selectedModelNode_->rotationAnimationEnabled
        );
        shouldSaveTransformSettings |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat3(
            "Initial rotation (X/Y/Z)",
            &selectedModelNode_->rotationAnimationInitialDegrees.x,
            1.0f,
            -360.0f,
            360.0f,
            "%.1f deg"
        );
        shouldSaveTransformSettings |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat3(
            "Rotation speed (X/Y/Z)",
            &selectedModelNode_->rotationAnimationSpeedDegrees.x,
            1.0f,
            -360.0f,
            360.0f,
            "%.1f deg/s"
        );
        shouldSaveTransformSettings |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::Button("Reset node animation")) {
            selectedModelNode_->rotationAnimationEnabled = false;
            selectedModelNode_->rotationAnimationInitialDegrees = glm::vec3(0.0f);
            selectedModelNode_->rotationAnimationSpeedDegrees = glm::vec3(0.0f);
            shouldSaveTransformSettings = true;
        }
        drawNodeShaderControls(*selectedModelNode_, shouldSaveTransformSettings);
    }
    if (shouldSaveTransformSettings) {
        saveTransformSettings();
    }
    // ImGui::Checkbox("Auto rotate Z", &autoRotate_);
    // ImGui::DragFloat("Auto rotation speed", &rotationSpeedDegrees_, 0.1f, 0.0f, 60.0f, "%.1f deg/s");
    // ImGui::Checkbox("ImGui demo window", &showImGuiDemo_);
    ImGui::Spacing();
    ImGui::TextWrapped("The 3D model remains the main viewport and is rendered before this transparent ImGui layer.");
    const bool leftPanelHovered = ImGui::IsWindowHovered();
    ImGui::EndChild();

    ImGui::SetCursorPos(ImVec2(
        std::max(kPanelMargin, ImGui::GetWindowWidth() - kRightPanelWidth - kPanelMargin),
        kPanelMargin
    ));
    ImGui::BeginChild("ModelHierarchy", ImVec2(kRightPanelWidth, 0.0f), true);
    ImGui::TextUnformatted("Model Hierarchy");
    ImGui::TextDisabled("FBX internal node tree");
    ImGui::Separator();
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (!meshData_.hierarchy.name.empty()) {
        drawModelHierarchy(meshData_.hierarchy);
    } else {
        ImGui::TextDisabled("No model hierarchy available");
    }
    const bool rightPanelHovered = ImGui::IsWindowHovered();
    ImGui::EndChild();

    if (selectedModelNode_ != nullptr &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !leftPanelHovered &&
        !rightPanelHovered &&
        ImGui::IsWindowHovered()) {
        selectedModelNode_ = nullptr;
        updateBoundingBoxVertices();
    }
    ImGui::End();

    // if (showImGuiDemo_) {
    //     ImGui::ShowDemoWindow(&showImGuiDemo_);
    // }

    ImGui::Render();
}

void Renderer::createDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT |
        VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
        VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT |
        VK_SHADER_STAGE_GEOMETRY_BIT |
        VK_SHADER_STAGE_FRAGMENT_BIT |
        VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT |
        VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
        VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT |
        VK_SHADER_STAGE_GEOMETRY_BIT |
        VK_SHADER_STAGE_FRAGMENT_BIT |
        VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(
            context_->device(),
            &layoutInfo,
            nullptr,
            &descriptorSetLayout_
        ) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout!");
    }
}

void Renderer::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapChain_->imageFormat();
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentReference{};
    colorAttachmentReference.attachment = 0;
    colorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = swapChain_->depthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentReference{};
    depthAttachmentReference.attachment = 1;
    depthAttachmentReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentReference;
    subpass.pDepthStencilAttachment = &depthAttachmentReference;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    const std::array<VkAttachmentDescription, 2> attachments = {
        colorAttachment,
        depthAttachment
    };

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(context_->device(), &renderPassInfo, nullptr, &renderPass_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass!");
    }
}

void Renderer::discoverShaderBinaries() {
    for (auto& shaders : availableShaders_) {
        shaders.clear();
    }

    const std::filesystem::path shaderDirectory(kShaderBinaryDirectory);
    if (!std::filesystem::exists(shaderDirectory)) {
        throw std::runtime_error("shader binary directory does not exist: " + shaderDirectory.string());
    }

    for (const auto& entry : std::filesystem::directory_iterator(shaderDirectory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".spv") {
            continue;
        }

        std::string shaderName = entry.path().filename().string();
        constexpr std::string_view kSpirvSuffix = ".spv";
        if (!shaderName.ends_with(kSpirvSuffix)) {
            continue;
        }
        shaderName.erase(shaderName.size() - kSpirvSuffix.size());

        ShaderStage stage = ShaderStage::Vertex;
        if (!shaderStageForExtension(std::filesystem::path(shaderName).extension().string(), stage)) {
            continue;
        }
        availableShaders_[static_cast<std::size_t>(stage)].push_back(std::move(shaderName));
    }

    for (auto& shaders : availableShaders_) {
        std::sort(shaders.begin(), shaders.end());
    }
}

VkShaderModule Renderer::shaderModule(const std::string& shaderName) {
    const auto existing = shaderModules_.find(shaderName);
    if (existing != shaderModules_.end()) {
        return existing->second;
    }

    const std::filesystem::path shaderPath =
        std::filesystem::path(kShaderBinaryDirectory) / (shaderName + ".spv");
    const VkShaderModule module = createShaderModule(readBinaryFile(shaderPath));
    shaderModules_.emplace(shaderName, module);
    return module;
}

std::string Renderer::graphicsPipelineKey(const NodeShaderConfiguration& configuration) const {
    std::string key;
    for (const ShaderStage stage : {
             ShaderStage::Vertex,
             ShaderStage::TessellationControl,
             ShaderStage::TessellationEvaluation,
             ShaderStage::Geometry,
             ShaderStage::Fragment
         }) {
        key += configuration.shader(stage);
        key += '\n';
    }
    return key;
}

VkPipeline Renderer::createNodeGraphicsPipeline(const NodeShaderConfiguration& configuration) {
    const bool hasTessellationControl =
        !configuration.shader(ShaderStage::TessellationControl).empty();
    const bool hasTessellationEvaluation =
        !configuration.shader(ShaderStage::TessellationEvaluation).empty();
    if (hasTessellationControl != hasTessellationEvaluation) {
        throw std::runtime_error(
            "tessellation control and evaluation shaders must be configured together"
        );
    }
    const std::string effectiveFragmentShader =
        configuration.shader(ShaderStage::Fragment).empty()
        ? defaultShaderForStage(ShaderStage::Fragment)
        : configuration.shader(ShaderStage::Fragment);
    const bool isWaterShader =
        effectiveFragmentShader == "shader_water.frag" ||
        effectiveFragmentShader == "shader_water_flow.frag";

    const std::array<ShaderStage, 5> graphicsStages = {
        ShaderStage::Vertex,
        ShaderStage::TessellationControl,
        ShaderStage::TessellationEvaluation,
        ShaderStage::Geometry,
        ShaderStage::Fragment
    };
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    shaderStages.reserve(graphicsStages.size());
    for (const ShaderStage stage : graphicsStages) {
        std::string shaderName = configuration.shader(stage);
        if (shaderName.empty()) {
            shaderName = defaultShaderForStage(stage);
        }
        if (shaderName.empty()) {
            continue;
        }

        const auto& stageShaders = availableShaders_[static_cast<std::size_t>(stage)];
        if (std::find(stageShaders.begin(), stageShaders.end(), shaderName) == stageShaders.end()) {
            throw std::runtime_error(
                std::string("shader is not available for ") + shaderStageLabel(stage) + ": " + shaderName
            );
        }

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = vkShaderStage(stage);
        stageInfo.module = shaderModule(shaderName);
        stageInfo.pName = "main";
        shaderStages.push_back(stageInfo);
    }

    const auto bindingDescription = Vertex::bindingDescription();
    const auto attributeDescriptions = Vertex::attributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = hasTessellationControl
        ? VK_PRIMITIVE_TOPOLOGY_PATCH_LIST
        : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = isWaterShader ? VK_FALSE : VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = isWaterShader ? VK_TRUE : VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    const std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineTessellationStateCreateInfo tessellationState{};
    tessellationState.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
    tessellationState.patchControlPoints = 3;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pTessellationState = hasTessellationControl ? &tessellationState : nullptr;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(
            context_->device(),
            VK_NULL_HANDLE,
            1,
            &pipelineInfo,
            nullptr,
            &pipeline
        ) != VK_SUCCESS) {
        throw std::runtime_error("failed to create node graphics pipeline");
    }
    return pipeline;
}

VkPipeline Renderer::graphicsPipelineForNode(const ModelNode& node) {
    if (!node.shaderConfiguration.hasGraphicsOverride()) {
        return graphicsPipeline_;
    }

    const std::string key = graphicsPipelineKey(node.shaderConfiguration);
    const auto existing = nodeGraphicsPipelines_.find(key);
    if (existing != nodeGraphicsPipelines_.end()) {
        return existing->second;
    }
    if (failedNodeGraphicsPipelineKeys_.contains(key)) {
        return graphicsPipeline_;
    }

    try {
        const VkPipeline pipeline = createNodeGraphicsPipeline(node.shaderConfiguration);
        nodeGraphicsPipelines_.emplace(key, pipeline);
        return pipeline;
    } catch (const std::exception& error) {
        failedNodeGraphicsPipelineKeys_.insert(key);
        std::cerr << "warning: node " << node.nodeId
                  << " shader pipeline failed, using default shaders: " << error.what() << '\n';
        return graphicsPipeline_;
    }
}

VkPipeline Renderer::computePipelineForNode(const ModelNode& node) {
    const std::string& computeShaderName = node.shaderConfiguration.shader(ShaderStage::Compute);
    if (computeShaderName.empty()) {
        return VK_NULL_HANDLE;
    }
    const auto existing = nodeComputePipelines_.find(computeShaderName);
    if (existing != nodeComputePipelines_.end()) {
        return existing->second;
    }
    if (failedNodeComputePipelineKeys_.contains(computeShaderName)) {
        return VK_NULL_HANDLE;
    }

    try {
        const auto& computeShaders = availableShaders_[static_cast<std::size_t>(ShaderStage::Compute)];
        if (std::find(computeShaders.begin(), computeShaders.end(), computeShaderName) == computeShaders.end()) {
            throw std::runtime_error("compute shader is not available: " + computeShaderName);
        }

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule(computeShaderName);
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = pipelineLayout_;

        VkPipeline pipeline = VK_NULL_HANDLE;
        if (vkCreateComputePipelines(
                context_->device(),
                VK_NULL_HANDLE,
                1,
                &pipelineInfo,
                nullptr,
                &pipeline
            ) != VK_SUCCESS) {
            throw std::runtime_error("failed to create node compute pipeline");
        }
        nodeComputePipelines_.emplace(computeShaderName, pipeline);
        return pipeline;
    } catch (const std::exception& error) {
        failedNodeComputePipelineKeys_.insert(computeShaderName);
        std::cerr << "warning: node " << node.nodeId
                  << " compute shader pipeline failed: " << error.what() << '\n';
        return VK_NULL_HANDLE;
    }
}

void Renderer::createGraphicsPipeline() {
    discoverShaderBinaries();

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout_;
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT |
        VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
        VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT |
        VK_SHADER_STAGE_GEOMETRY_BIT |
        VK_SHADER_STAGE_FRAGMENT_BIT |
        VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(MaterialPushConstants);
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    if (vkCreatePipelineLayout(context_->device(), &pipelineLayoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout!");
    }

    graphicsPipeline_ = createNodeGraphicsPipeline(NodeShaderConfiguration{});
}

void Renderer::createBoundingBoxPipeline() {
    const std::filesystem::path shaderDirectory(kShaderBinaryDirectory);
    const auto vertShaderCode = readBinaryFile(shaderDirectory / "shader_bounding_box.vert.spv");
    const auto fragShaderCode = readBinaryFile(shaderDirectory / "shader_bounding_box.frag.spv");

    const VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    const VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    const VkPipelineShaderStageCreateInfo shaderStages[] = {
        vertShaderStageInfo,
        fragShaderStageInfo
    };

    const auto bindingDescription = Vertex::bindingDescription();
    const auto attributeDescriptions = Vertex::attributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    const std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0;

    const VkResult result = vkCreateGraphicsPipelines(
        context_->device(),
        VK_NULL_HANDLE,
        1,
        &pipelineInfo,
        nullptr,
        &boundingBoxPipeline_
    );

    vkDestroyShaderModule(context_->device(), fragShaderModule, nullptr);
    vkDestroyShaderModule(context_->device(), vertShaderModule, nullptr);

    if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to create bounding box pipeline!");
    }
}

void Renderer::createBoundingBoxBuffers() {
    boundingBoxVertexBuffers_.resize(kMaxFramesInFlight);
    const VkDeviceSize bufferSize = sizeof(boundingBoxVertices_);
    for (auto& boundingBoxVertexBuffer : boundingBoxVertexBuffers_) {
        boundingBoxVertexBuffer = std::make_unique<Buffer>(
            *context_,
            bufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
    }
}

void Renderer::createMeshData() {
    if (std::string_view(kModelPath).empty()) {
        meshData_.vertices.assign(kRectangleVertices.begin(), kRectangleVertices.end());
        meshData_.indices.assign(kRectangleIndices.begin(), kRectangleIndices.end());
        meshData_.materials.emplace_back();
        meshData_.parts.push_back({0, static_cast<uint32_t>(meshData_.indices.size()), 0});
        meshData_.hierarchy.name = "Rectangle";
        meshData_.hierarchy.meshCount = 1;
        meshData_.hierarchy.meshPartIndices.push_back(0);
        meshData_.hierarchy.hasBounds = true;
        meshData_.hierarchy.boundsMinimum = glm::vec3(-0.5f, -0.5f, 0.0f);
        meshData_.hierarchy.boundsMaximum = glm::vec3(0.5f, 0.5f, 0.0f);
        modelCenter_ = glm::vec3(0.0f);
        modelScale_ = 1.0f;
        return;
    }

    meshData_ = loadModelMesh(std::filesystem::path(kModelPath));

    glm::vec3 minimum(std::numeric_limits<float>::max());
    glm::vec3 maximum(std::numeric_limits<float>::lowest());
    for (const Vertex& vertex : meshData_.vertices) {
        minimum = glm::min(minimum, vertex.position);
        maximum = glm::max(maximum, vertex.position);
    }

    const glm::vec3 extent = maximum - minimum;
    const float largestExtent = std::max(extent.x, std::max(extent.y, extent.z));
    if (largestExtent <= std::numeric_limits<float>::epsilon()) {
        throw std::runtime_error("model has no measurable bounds");
    }

    modelCenter_ = (minimum + maximum) * 0.5f;
    modelScale_ = 1.8f / largestExtent;
}

void Renderer::createVertexBuffer() {
    if (meshData_.vertices.empty()) {
        throw std::runtime_error("mesh has no vertices");
    }

    const VkDeviceSize bufferSize = sizeof(meshData_.vertices[0]) * meshData_.vertices.size();

    Buffer stagingBuffer(
        *context_,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    stagingBuffer.upload(meshData_.vertices.data(), bufferSize);

    vertexBuffer_ = std::make_unique<Buffer>(
        *context_,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    context_->copyBuffer(stagingBuffer.handle(), vertexBuffer_->handle(), bufferSize);
}

void Renderer::createIndexBuffer() {
    if (meshData_.indices.empty()) {
        throw std::runtime_error("mesh has no indices");
    }

    const VkDeviceSize bufferSize = sizeof(meshData_.indices[0]) * meshData_.indices.size();
    Buffer indexStagingBuffer(
        *context_,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    indexStagingBuffer.upload(meshData_.indices.data(), bufferSize);

    indexBuffer_ = std::make_unique<Buffer>(
        *context_,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    context_->copyBuffer(indexStagingBuffer.handle(), indexBuffer_->handle(), bufferSize);
}

void Renderer::createMaterialTextures() {
    materialTextures_.resize(meshData_.materials.size());
    bool needsFallbackTexture = false;

    for (std::size_t materialIndex = 0; materialIndex < meshData_.materials.size(); ++materialIndex) {
        const MaterialData& material = meshData_.materials[materialIndex];
        try {
            if (!material.embeddedTexture.empty()) {
                if (material.embeddedTexture.compressed) {
                    materialTextures_[materialIndex] = std::make_unique<Texture>(
                        *context_,
                        material.embeddedTexture.data
                    );
                } else {
                    materialTextures_[materialIndex] = std::make_unique<Texture>(
                        *context_,
                        material.embeddedTexture.data,
                        material.embeddedTexture.width,
                        material.embeddedTexture.height
                    );
                }
            } else if (!material.texturePath.empty()) {
                materialTextures_[materialIndex] = std::make_unique<Texture>(
                    *context_,
                    material.texturePath
                );
            }
        } catch (const std::exception& error) {
            std::cerr << "warning: failed to load FBX material texture for material "
                      << materialIndex << ": " << error.what() << '\n';
            materialTextures_[materialIndex].reset();
        }

        if (!materialTextures_[materialIndex]) {
            needsFallbackTexture = true;
        }
    }

    if (needsFallbackTexture || materialTextures_.empty()) {
        const std::vector<std::uint8_t> whitePixel = {255, 255, 255, 255};
        fallbackTexture_ = std::make_unique<Texture>(*context_, whitePixel, 1, 1);
    }
}

void Renderer::createUniformBuffers() {
    const VkDeviceSize bufferSize = sizeof(UniformBufferObject);
    uniformBuffers_.resize(kMaxFramesInFlight);

    for (auto& uniformBuffer : uniformBuffers_) {
        uniformBuffer = std::make_unique<Buffer>(
            *context_,
            bufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
    }
}

void Renderer::createDescriptorPool() {
    const uint32_t materialCount = static_cast<uint32_t>(meshData_.materials.size());
    const uint32_t descriptorCount = kMaxFramesInFlight * materialCount;
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = descriptorCount;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = descriptorCount;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = descriptorCount;

    if (vkCreateDescriptorPool(context_->device(), &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool!");
    }
}

void Renderer::createDescriptorSets() {
    const uint32_t materialCount = static_cast<uint32_t>(meshData_.materials.size());
    const uint32_t descriptorSetCount = kMaxFramesInFlight * materialCount;
    std::vector<VkDescriptorSetLayout> layouts(descriptorSetCount, descriptorSetLayout_);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = descriptorSetCount;
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets_.resize(descriptorSetCount);
    if (vkAllocateDescriptorSets(context_->device(), &allocInfo, descriptorSets_.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor sets!");
    }

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        for (uint32_t materialIndex = 0; materialIndex < materialCount; ++materialIndex) {
            const std::size_t descriptorSetIndex =
                static_cast<std::size_t>(i) * materialCount + materialIndex;
            const Texture* texture = materialTextures_[materialIndex].get();
            if (texture == nullptr) {
                texture = fallbackTexture_.get();
            }
            if (texture == nullptr) {
                throw std::runtime_error("FBX material has no usable texture or fallback texture");
            }

            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = uniformBuffers_[i]->handle();
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(UniformBufferObject);

            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = texture->imageView();
            imageInfo.sampler = texture->sampler();

            std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
            descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[0].dstSet = descriptorSets_[descriptorSetIndex];
            descriptorWrites[0].dstBinding = 0;
            descriptorWrites[0].dstArrayElement = 0;
            descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrites[0].descriptorCount = 1;
            descriptorWrites[0].pBufferInfo = &bufferInfo;

            descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[1].dstSet = descriptorSets_[descriptorSetIndex];
            descriptorWrites[1].dstBinding = 1;
            descriptorWrites[1].dstArrayElement = 0;
            descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[1].descriptorCount = 1;
            descriptorWrites[1].pImageInfo = &imageInfo;

            vkUpdateDescriptorSets(
                context_->device(),
                static_cast<uint32_t>(descriptorWrites.size()),
                descriptorWrites.data(),
                0,
                nullptr
            );
        }
    }
}

void Renderer::updateUniformBuffer() {
    const auto now = std::chrono::steady_clock::now();
    const float elapsedSeconds = std::chrono::duration<float>(now - startTime_).count();

    UniformBufferObject ubo{};
    // GLM post-multiplies transforms. Build S * T so the model is translated
    // to the origin first and then scaled; T * S would leave the center in
    // model units and move this scene outside the camera frustum.
    const glm::mat4 modelNormalization =
        glm::scale(
            glm::mat4(1.0f),
            glm::vec3(modelScale_ * std::max(modelScaleFactor_, kMinimumModelScaleFactor))
        ) *
        glm::translate(glm::mat4(1.0f), -modelCenter_);

    glm::vec3 rotationDegrees = modelRotationDegrees_;
    if (autoRotate_) {
        rotationDegrees.z += elapsedSeconds * rotationSpeedDegrees_;
    }

    const glm::mat4 rotation =
        glm::rotate(glm::mat4(1.0f), glm::radians(rotationDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f)) *
        glm::rotate(glm::mat4(1.0f), glm::radians(rotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f)) *
        glm::rotate(glm::mat4(1.0f), glm::radians(rotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));

    ubo.model = glm::translate(glm::mat4(1.0f), modelTranslation_) *
        rotation *
        modelNormalization;
    ubo.time = glm::vec4(elapsedSeconds, 0.0f, 0.0f, 0.0f);
    ubo.view = glm::lookAt(
        glm::vec3(3.5f, 3.5f, 2.8f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f)
    );

    const VkExtent2D extent = swapChain_->extent();
    if (extent.height == 0) {
        return;
    }
    ubo.projection = glm::perspective(
        glm::radians(45.0f),
        static_cast<float>(extent.width) / static_cast<float>(extent.height),
        0.01f,
        100.0f
    );
    // GLM follows OpenGL's Y direction; Vulkan's viewport uses the opposite one.
    ubo.projection[1][1] *= -1.0f;

    uniformBuffers_[currentFrame_]->upload(&ubo, sizeof(ubo));
}

void Renderer::updateBoundingBoxBuffer() {
    if (currentFrame_ >= boundingBoxVertexBuffers_.size()) {
        return;
    }

    boundingBoxVertexBuffers_[currentFrame_]->upload(
        boundingBoxVertices_.data(),
        sizeof(boundingBoxVertices_)
    );
}

void Renderer::createCommandBuffers() {
    commandBuffers_.resize(kMaxFramesInFlight);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = context_->commandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());

    if (vkAllocateCommandBuffers(context_->device(), &allocInfo, commandBuffers_.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers!");
    }
}

void Renderer::createSyncObjects() {
    imageAvailableSemaphores_.resize(kMaxFramesInFlight);
    renderFinishedSemaphores_.resize(kMaxFramesInFlight);
    inFlightFences_.resize(kMaxFramesInFlight);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (vkCreateSemaphore(context_->device(), &semaphoreInfo, nullptr, &imageAvailableSemaphores_[i]) != VK_SUCCESS ||
            vkCreateSemaphore(context_->device(), &semaphoreInfo, nullptr, &renderFinishedSemaphores_[i]) != VK_SUCCESS ||
            vkCreateFence(context_->device(), &fenceInfo, nullptr, &inFlightFences_[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create synchronization objects for a frame!");
        }
    }
}

void Renderer::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass_;
    renderPassInfo.framebuffer = swapChain_->framebuffers()[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapChain_->extent();

    const std::array<VkClearValue, 2> clearValues = {{
        {{{0.015f, 0.02f, 0.03f, 1.0f}}},
        {{{1.0f, 0}}}
    }};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_);

    const VkExtent2D extent = swapChain_->extent();
    VkViewport viewport{};
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = extent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    const VkBuffer buffers[] = {vertexBuffer_->handle()};
    const VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer_->handle(), 0, VK_INDEX_TYPE_UINT32);

    const uint32_t materialCount = static_cast<uint32_t>(meshData_.materials.size());
    const auto now = std::chrono::steady_clock::now();
    const float elapsedSeconds = std::chrono::duration<float>(now - startTime_).count();
    for (std::size_t partIndex = 0; partIndex < meshData_.parts.size(); ++partIndex) {
        const MeshPart& part = meshData_.parts[partIndex];
        if (part.materialIndex >= materialCount || part.indexCount == 0) {
            continue;
        }

        const ModelNode* node = findModelNodeById(meshData_.hierarchy, part.nodeId);
        const VkPipeline nodePipeline = node != nullptr
            ? graphicsPipelineForNode(*node)
            : graphicsPipeline_;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, nodePipeline);

        const std::size_t descriptorSetIndex =
            static_cast<std::size_t>(currentFrame_) * materialCount + part.materialIndex;
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout_,
            0,
            1,
            &descriptorSets_[descriptorSetIndex],
            0,
            nullptr
        );

        MaterialPushConstants materialPushConstants{};
        bool found = false;
        materialPushConstants.nodeTransform = animationTransformForNode(
            meshData_.hierarchy,
            part.nodeId,
            elapsedSeconds,
            glm::mat4(1.0f),
            found
        );
        materialPushConstants.baseColor = meshData_.materials[part.materialIndex].baseColor;
        materialPushConstants.hasTexture = materialTextures_[part.materialIndex] != nullptr ? 1u : 0u;
        vkCmdPushConstants(
            commandBuffer,
            pipelineLayout_,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(MaterialPushConstants),
            &materialPushConstants
        );
        vkCmdDrawIndexed(commandBuffer, part.indexCount, 1, part.firstIndex, 0, 0);
    }

    if (selectedModelNode_ != nullptr && selectedModelNode_->hasBounds) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, boundingBoxPipeline_);

        const VkBuffer boundingBoxBuffer = boundingBoxVertexBuffers_[currentFrame_]->handle();
        const VkDeviceSize boundingBoxOffset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &boundingBoxBuffer, &boundingBoxOffset);

        const VkDescriptorSet& uniformDescriptorSet = descriptorSets_[currentFrame_ * materialCount];
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout_,
            0,
            1,
            &uniformDescriptorSet,
            0,
            nullptr
        );
        MaterialPushConstants boundingBoxPushConstants{};
        vkCmdPushConstants(
            commandBuffer,
            pipelineLayout_,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(MaterialPushConstants),
            &boundingBoxPushConstants
        );
        vkCmdDraw(commandBuffer, static_cast<uint32_t>(boundingBoxVertices_.size()), 1, 0, 0);
    }

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

    vkCmdEndRenderPass(commandBuffer);
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }
}

void Renderer::drawFrame() {
    if (vkWaitForFences(
            context_->device(),
            1,
            &inFlightFences_[currentFrame_],
            VK_TRUE,
            UINT64_MAX
        ) != VK_SUCCESS) {
        throw std::runtime_error("failed to wait for in-flight frame!");
    }

    uint32_t imageIndex = 0;
    const VkResult acquireResult = vkAcquireNextImageKHR(
        context_->device(),
        swapChain_->handle(),
        UINT64_MAX,
        imageAvailableSemaphores_[currentFrame_],
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
        return;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    buildImGuiFrame();
    updateBoundingBoxVertices();

    if (vkResetFences(context_->device(), 1, &inFlightFences_[currentFrame_]) != VK_SUCCESS) {
        throw std::runtime_error("failed to reset in-flight frame fence!");
    }
    updateUniformBuffer();
    updateBoundingBoxBuffer();
    if (vkResetCommandBuffer(commandBuffers_[currentFrame_], 0) != VK_SUCCESS) {
        throw std::runtime_error("failed to reset command buffer!");
    }
    recordCommandBuffer(commandBuffers_[currentFrame_], imageIndex);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    const VkSemaphore waitSemaphores[] = {imageAvailableSemaphores_[currentFrame_]};
    const VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers_[currentFrame_];
    const VkSemaphore signalSemaphores[] = {renderFinishedSemaphores_[currentFrame_]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(context_->graphicsQueue(), 1, &submitInfo, inFlightFences_[currentFrame_]) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit draw command buffer!");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    const VkSwapchainKHR swapChains[] = {swapChain_->handle()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    const VkResult presentResult = vkQueuePresentKHR(context_->presentQueue(), &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
        presentResult == VK_SUBOPTIMAL_KHR ||
        framebufferResized_) {
        framebufferResized_ = false;
        recreateSwapChain();
    } else if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("failed to present swap chain image!");
    }

    currentFrame_ = (currentFrame_ + 1) % kMaxFramesInFlight;
}

void Renderer::recreateSwapChain() {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(context_->window(), &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(context_->window(), &width, &height);
        glfwWaitEvents();
    }

    context_->waitIdle();
    swapChain_->recreate(renderPass_);
    ImGui_ImplVulkan_SetMinImageCount(
        std::max(2u, static_cast<uint32_t>(swapChain_->framebuffers().size()))
    );
}

VkShaderModule Renderer::createShaderModule(const std::vector<char>& code) const {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(context_->device(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module!");
    }
    return shaderModule;
}

void Renderer::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    (void)width;
    (void)height;
    auto* renderer = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
    if (renderer != nullptr) {
        renderer->framebufferResized_ = true;
    }
}

} // namespace VulkCore
