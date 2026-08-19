#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "vulkcore/Renderer/Renderer.hpp"

#include <array>
#include <algorithm>
#include <filesystem>
#include <limits>
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

#include <glm/gtc/matrix_transform.hpp>

namespace VulkCore {

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
    swapChain_->createFramebuffers(renderPass_);
    createMeshData();
    createVertexBuffer();
    createIndexBuffer();
    createTexture();
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

    context_->waitIdle();
    shutdownImGui();
    swapChain_->cleanupFramebuffers();
    uniformBuffers_.clear();
    texture_.reset();
    indexBuffer_.reset();
    vertexBuffer_.reset();

    for (size_t i = 0; i < imageAvailableSemaphores_.size(); ++i) {
        vkDestroySemaphore(context_->device(), imageAvailableSemaphores_[i], nullptr);
        vkDestroySemaphore(context_->device(), renderFinishedSemaphores_[i], nullptr);
        vkDestroyFence(context_->device(), inFlightFences_[i], nullptr);
    }

    if (graphicsPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(context_->device(), graphicsPipeline_, nullptr);
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(context_->device(), pipelineLayout_, nullptr);
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
    ImGui::SetCursorPos(ImVec2(16.0f, 16.0f));
    ImGui::BeginChild("SceneControls", ImVec2(280.0f, 0.0f), true);
    ImGui::TextUnformatted("VulkCore");
    ImGui::TextDisabled("Vulkan scene rendered with Dear ImGui UI");
    ImGui::Separator();
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Text("Frame time: %.2f ms", 1000.0f / std::max(ImGui::GetIO().Framerate, 0.001f));
    ImGui::SliderFloat("Rotation speed", &rotationSpeedDegrees_, 0.0f, 60.0f, "%.1f deg/s");
    ImGui::Checkbox("ImGui demo window", &showImGuiDemo_);
    ImGui::Spacing();
    ImGui::TextWrapped("The 3D model remains the main viewport and is rendered before this transparent ImGui layer.");
    ImGui::EndChild();
    ImGui::End();

    if (showImGuiDemo_) {
        ImGui::ShowDemoWindow(&showImGuiDemo_);
    }

    ImGui::Render();
}

void Renderer::createDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

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

void Renderer::createGraphicsPipeline() {
    const std::filesystem::path shaderDirectory(kShaderBinaryDirectory);
    const auto vertShaderCode = readBinaryFile(shaderDirectory / "shader_base.vert.spv");
    const auto fragShaderCode = readBinaryFile(shaderDirectory / "shader_base.frag.spv");

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

    VkPipelineShaderStageCreateInfo shaderStages[] = {
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
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
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
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
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

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout_;
    if (vkCreatePipelineLayout(context_->device(), &pipelineLayoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        vkDestroyShaderModule(context_->device(), fragShaderModule, nullptr);
        vkDestroyShaderModule(context_->device(), vertShaderModule, nullptr);
        throw std::runtime_error("failed to create pipeline layout!");
    }

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
        &graphicsPipeline_
    );

    vkDestroyShaderModule(context_->device(), fragShaderModule, nullptr);
    vkDestroyShaderModule(context_->device(), vertShaderModule, nullptr);

    if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to create graphics pipeline!");
    }
}

void Renderer::createMeshData() {
    if (std::string_view(kModelPath).empty()) {
        meshData_.vertices.assign(kRectangleVertices.begin(), kRectangleVertices.end());
        meshData_.indices.assign(kRectangleIndices.begin(), kRectangleIndices.end());
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

    indexCount_ = static_cast<uint32_t>(meshData_.indices.size());
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

void Renderer::createTexture() {
    texture_ = std::make_unique<Texture>(*context_, std::filesystem::path(kTexturePath));
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
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = kMaxFramesInFlight;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = kMaxFramesInFlight;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = kMaxFramesInFlight;

    if (vkCreateDescriptorPool(context_->device(), &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool!");
    }
}

void Renderer::createDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(kMaxFramesInFlight, descriptorSetLayout_);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = kMaxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets_.resize(kMaxFramesInFlight);
    if (vkAllocateDescriptorSets(context_->device(), &allocInfo, descriptorSets_.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor sets!");
    }

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers_[i]->handle();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = texture_->imageView();
        imageInfo.sampler = texture_->sampler();

        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSets_[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSets_[i];
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

void Renderer::updateUniformBuffer() {
    const auto now = std::chrono::steady_clock::now();
    const float elapsedSeconds = std::chrono::duration<float>(now - startTime_).count();

    UniformBufferObject ubo{};
    // GLM post-multiplies transforms. Build S * T so the model is translated
    // to the origin first and then scaled; T * S would leave the center in
    // model units and move this scene outside the camera frustum.
    const glm::mat4 modelNormalization =
        glm::scale(glm::mat4(1.0f), glm::vec3(modelScale_)) *
        glm::translate(glm::mat4(1.0f), -modelCenter_);
    ubo.model = glm::rotate(
        glm::mat4(1.0f),
        elapsedSeconds * glm::radians(rotationSpeedDegrees_),
        glm::vec3(0.0f, 0.0f, 1.0f)
    ) * modelNormalization;
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

    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout_,
        0,
        1,
        &descriptorSets_[currentFrame_],
        0,
        nullptr
    );

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
    vkCmdDrawIndexed(commandBuffer, indexCount_, 1, 0, 0, 0);

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

    if (vkResetFences(context_->device(), 1, &inFlightFences_[currentFrame_]) != VK_SUCCESS) {
        throw std::runtime_error("failed to reset in-flight frame fence!");
    }
    updateUniformBuffer();
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
