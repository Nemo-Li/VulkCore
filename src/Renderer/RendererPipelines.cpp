#include "RendererImplementation.hpp"
#include "HdrComposition.hpp"

namespace VulkCore {

void Renderer::createDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 1 + kMaterialImageBindingCount> bindings{};
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

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[7] = bindings[6];
    bindings[7].binding = 7;
    bindings[kAlphaMaskBinding] = bindings[6];
    bindings[kAlphaMaskBinding].binding = kAlphaMaskBinding;

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
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentReference{};
    colorAttachmentReference.attachment = 0;
    colorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = swapChain_->depthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
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

void Renderer::createSceneRenderResources() {
    const VkExtent2D extent = swapChain_->extent();

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapChain_->imageFormat();
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorReference{};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = swapChain_->depthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthReference{};
    depthReference.attachment = 1;
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;

    // Match the swap-chain render pass dependency shape so material pipelines
    // created for that pass remain compatible in this off-screen pass. The
    // explicit barriers after vkCmdEndRenderPass provide the outgoing color
    // and depth visibility needed by the transfer and sampling stages.
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

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
    if (vkCreateRenderPass(context_->device(), &renderPassInfo, nullptr, &sceneRenderPass_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create scene render pass!");
    }

    const auto createImage = [&](VkFormat format,
                                 VkImageUsageFlags usage,
                                 VkImage& image,
                                 VkDeviceMemory& memory) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = {extent.width, extent.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = usage;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (vkCreateImage(context_->device(), &imageInfo, nullptr, &image) != VK_SUCCESS) {
            throw std::runtime_error("failed to create scene render image!");
        }
        try {
            VkMemoryRequirements memoryRequirements{};
            vkGetImageMemoryRequirements(context_->device(), image, &memoryRequirements);

            VkMemoryAllocateInfo allocateInfo{};
            allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocateInfo.allocationSize = memoryRequirements.size;
            allocateInfo.memoryTypeIndex = context_->findMemoryType(
                memoryRequirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            );
            if (vkAllocateMemory(context_->device(), &allocateInfo, nullptr, &memory) != VK_SUCCESS) {
                throw std::runtime_error("failed to allocate scene render image memory!");
            }
            if (vkBindImageMemory(context_->device(), image, memory, 0) != VK_SUCCESS) {
                throw std::runtime_error("failed to bind scene render image memory!");
            }
        } catch (...) {
            if (memory != VK_NULL_HANDLE) {
                vkFreeMemory(context_->device(), memory, nullptr);
                memory = VK_NULL_HANDLE;
            }
            vkDestroyImage(context_->device(), image, nullptr);
            image = VK_NULL_HANDLE;
            throw;
        }
    };

    const auto createImageView = [&](VkImage image, VkFormat format, VkImageAspectFlags aspect) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspect;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView imageView = VK_NULL_HANDLE;
        if (vkCreateImageView(context_->device(), &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
            throw std::runtime_error("failed to create scene render image view!");
        }
        return imageView;
    };

    try {
        sceneRenderTargets_.resize(kMaxFramesInFlight);
        for (SceneRenderTarget& target : sceneRenderTargets_) {
            createImage(
                swapChain_->imageFormat(),
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT |
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                target.colorImage,
                target.colorMemory
            );
            target.colorImageView = createImageView(
                target.colorImage,
                swapChain_->imageFormat(),
                VK_IMAGE_ASPECT_COLOR_BIT
            );

            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.maxAnisotropy = 1.0f;
            samplerInfo.maxLod = 0.0f;
            if (vkCreateSampler(context_->device(), &samplerInfo, nullptr, &target.colorSampler) != VK_SUCCESS) {
                throw std::runtime_error("failed to create scene render sampler!");
            }

            createImage(
                swapChain_->depthFormat(),
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT,
                target.depthImage,
                target.depthMemory
            );
            target.depthImageView = createImageView(
                target.depthImage,
                swapChain_->depthFormat(),
                VK_IMAGE_ASPECT_DEPTH_BIT
            );

            VkSamplerCreateInfo depthSamplerInfo{};
            depthSamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            depthSamplerInfo.magFilter = VK_FILTER_NEAREST;
            depthSamplerInfo.minFilter = VK_FILTER_NEAREST;
            depthSamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            depthSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            depthSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            depthSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            depthSamplerInfo.maxAnisotropy = 1.0f;
            depthSamplerInfo.maxLod = 0.0f;
            if (vkCreateSampler(
                    context_->device(),
                    &depthSamplerInfo,
                    nullptr,
                    &target.depthSampler
                ) != VK_SUCCESS) {
                throw std::runtime_error("failed to create scene depth sampler!");
            }

            const std::array<VkImageView, 2> framebufferAttachments = {
                target.colorImageView,
                target.depthImageView
            };
            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = sceneRenderPass_;
            framebufferInfo.attachmentCount = static_cast<uint32_t>(framebufferAttachments.size());
            framebufferInfo.pAttachments = framebufferAttachments.data();
            framebufferInfo.width = extent.width;
            framebufferInfo.height = extent.height;
            framebufferInfo.layers = 1;
            if (vkCreateFramebuffer(
                    context_->device(),
                    &framebufferInfo,
                    nullptr,
                    &target.framebuffer
                ) != VK_SUCCESS) {
                throw std::runtime_error("failed to create scene render framebuffer!");
            }
        }
    } catch (...) {
        destroySceneRenderResources();
        throw;
    }
}

void Renderer::destroySceneRenderResources() {
    if (!context_) {
        return;
    }
    for (SceneRenderTarget& target : sceneRenderTargets_) {
        if (target.framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(context_->device(), target.framebuffer, nullptr);
        }
        if (target.colorSampler != VK_NULL_HANDLE) {
            vkDestroySampler(context_->device(), target.colorSampler, nullptr);
        }
        if (target.depthSampler != VK_NULL_HANDLE) {
            vkDestroySampler(context_->device(), target.depthSampler, nullptr);
        }
        if (target.colorImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(context_->device(), target.colorImageView, nullptr);
        }
        if (target.depthImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(context_->device(), target.depthImageView, nullptr);
        }
        if (target.colorImage != VK_NULL_HANDLE) {
            vkDestroyImage(context_->device(), target.colorImage, nullptr);
        }
        if (target.depthImage != VK_NULL_HANDLE) {
            vkDestroyImage(context_->device(), target.depthImage, nullptr);
        }
        if (target.colorMemory != VK_NULL_HANDLE) {
            vkFreeMemory(context_->device(), target.colorMemory, nullptr);
        }
        if (target.depthMemory != VK_NULL_HANDLE) {
            vkFreeMemory(context_->device(), target.depthMemory, nullptr);
        }
        target = SceneRenderTarget{};
    }
    sceneRenderTargets_.clear();
    if (sceneRenderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(context_->device(), sceneRenderPass_, nullptr);
        sceneRenderPass_ = VK_NULL_HANDLE;
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
        // Full-screen demo shaders use a different vertex interface and are
        // owned by the post-process pipeline, not by mesh materials.
        if (shaderName == "shader_fullscreen.vert" ||
            shaderName == "shader_postprocess.frag" ||
            shaderName == "shader_skybox.frag") {
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

std::string Renderer::graphicsPipelineKey(
    const ShaderConfiguration& configuration,
    const MaterialData& material, bool hdr, VkCullModeFlags cullMode
) const {
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
    key += "blend=" + std::to_string(static_cast<int>(material.blendMode));
    key += ";depthWrite=" + std::to_string(material.depthWrite ? 1 : 0);
    key += ";cull=" + std::to_string(cullMode);
    key += ";lessEqual=" + std::to_string(material.depthLessEqual);
    key += ";hdr=" + std::to_string(hdr);
    return key;
}

VkPipeline Renderer::createMaterialGraphicsPipeline(
    const ShaderConfiguration& configuration,
    const MaterialData& material,
    VkRenderPass pass,
    VkCullModeFlags cullMode
) {
    const bool hasTessellationControl =
        !configuration.shader(ShaderStage::TessellationControl).empty();
    const bool hasTessellationEvaluation =
        !configuration.shader(ShaderStage::TessellationEvaluation).empty();
    if (hasTessellationControl != hasTessellationEvaluation) {
        throw std::runtime_error(
            "tessellation control and evaluation shaders must be configured together"
        );
    }
    const bool isWaterShader = isWaterShaderConfiguration(configuration);

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

    auto bindingDescription = Vertex::bindingDescription();
    const auto attributeDescriptions = Vertex::attributeDescriptions();

    const bool instancedGrass = configuration.shader(ShaderStage::Vertex) == kGrassVertexShader;
    const std::array<VkVertexInputAttributeDescription, 4> grassAttributes{{
        {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GrassInstance, rootHeight)},
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GrassInstance, normalWidth)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GrassInstance, motion)},
        {3, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(GrassInstance, tint)}
    }};
    if (instancedGrass) {
        bindingDescription = {0, sizeof(GrassInstance), VK_VERTEX_INPUT_RATE_INSTANCE};
    }

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(
        instancedGrass ? grassAttributes.size() : attributeDescriptions.size()
    );
    vertexInputInfo.pVertexAttributeDescriptions = instancedGrass
        ? grassAttributes.data() : attributeDescriptions.data();

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
    rasterizer.cullMode = cullMode;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = isWaterShader
        ? VK_FALSE
        : (material.depthWrite ? VK_TRUE : VK_FALSE);
    depthStencil.depthCompareOp = material.depthLessEqual ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    const bool enableBlend = isWaterShader || isTransparentMaterial(material);
    colorBlendAttachment.blendEnable = enableBlend ? VK_TRUE : VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    if (material.blendMode == MaterialData::BlendMode::Additive) {
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    } else if (material.blendMode == MaterialData::BlendMode::Multiply) {
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    }
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
    pipelineInfo.renderPass = pass;
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

VkPipeline Renderer::graphicsPipelineForMaterial(
    const MaterialData& material, bool hdr, VkCullModeFlags cullMode
) {
    const bool isDefaultState = material.blendMode == MaterialData::BlendMode::Opaque &&
        material.depthWrite && !material.depthLessEqual &&
        cullMode == VK_CULL_MODE_BACK_BIT && !isTransparentMaterial(material);
    if (!hdr && !material.shaderConfiguration.hasGraphicsOverride() && isDefaultState) {
        return graphicsPipeline_;
    }

    const std::string key = graphicsPipelineKey(material.shaderConfiguration, material, hdr, cullMode);
    if (const auto existing = nodeGraphicsPipelines_.find(key); existing != nodeGraphicsPipelines_.end()) {
        return existing->second;
    }
    const bool instancedGrass = material.shaderConfiguration.shader(ShaderStage::Vertex) ==
        kGrassVertexShader;
    if (failedNodeGraphicsPipelineKeys_.contains(key)) {
        return hdr || instancedGrass ? VK_NULL_HANDLE : graphicsPipeline_;
    }

    const VkRenderPass pass = hdr ? hdrComposition_->geometryPass() : renderPass_;
    try {
        const VkPipeline pipeline = createMaterialGraphicsPipeline(
            material.shaderConfiguration, material, pass, cullMode
        );
        nodeGraphicsPipelines_.emplace(key, pipeline);
        return pipeline;
    } catch (const std::exception& error) {
        failedNodeGraphicsPipelineKeys_.insert(key);
        std::cerr << "warning: material '" << material.name
                  << "' shader pipeline failed: " << error.what() << '\n';
        if (instancedGrass) return VK_NULL_HANDLE;
        if (!hdr) return graphicsPipeline_;
        const VkPipeline fallback = createMaterialGraphicsPipeline({}, material, pass, cullMode);
        nodeGraphicsPipelines_.emplace(key, fallback);
        return fallback;
    }
}

void Renderer::prepareTerrainAndGrassPipelines() {
    // Compile pipelines at scene load, even when the initial view sees sky.
    const bool hdr = hdrComposition_ != nullptr;
    std::unordered_set<std::uint32_t> preparedMaterials;
    for (const MeshPart& part : meshData_.parts) {
        if (!part.terrainBounds && !grassLodForLayer(part.nodeId - kGrassLodNodeBase)) continue;
        const std::uint32_t index = materialIndexForPart(part);
        if (index >= meshData_.materials.size() || !preparedMaterials.insert(index).second) continue;
        const MaterialData& material = meshData_.materials[index];
        if (material.doubleSided && material.drawBackFacesFirst && isTransparentMaterial(material)) {
            graphicsPipelineForMaterial(material, hdr, VK_CULL_MODE_FRONT_BIT);
            graphicsPipelineForMaterial(material, hdr, VK_CULL_MODE_BACK_BIT);
        } else {
            graphicsPipelineForMaterial(material, hdr,
                material.doubleSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT);
        }
    }
}

std::uint32_t Renderer::materialIndexForPart(const MeshPart& part) const {
    const std::int32_t overrideIndex = inheritedMaterialOverrideForSceneNode(part.nodeId);
    if (overrideIndex >= 0 &&
        static_cast<std::size_t>(overrideIndex) < meshData_.materials.size()) {
        return static_cast<std::uint32_t>(overrideIndex);
    }
    return part.materialIndex;
}

VkPipeline Renderer::computePipelineForMaterial(const MaterialData& material) {
    const std::string& computeShaderName = material.shaderConfiguration.shader(ShaderStage::Compute);
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
        std::cerr << "warning: material '" << material.name
                  << "' compute shader pipeline failed: " << error.what() << '\n';
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
    pushConstantRange.stageFlags = kMaterialPushConstantStageFlags;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(MaterialPushConstants);
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    if (vkCreatePipelineLayout(context_->device(), &pipelineLayoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout!");
    }

    graphicsPipeline_ = createMaterialGraphicsPipeline({}, {}, renderPass_, VK_CULL_MODE_BACK_BIT);
}

void Renderer::createParticlePipelines() {
    particleAlphaPipeline_ = createParticlePipeline(false, renderPass_);
    particleAdditivePipeline_ = createParticlePipeline(true, renderPass_);
}

void Renderer::createPostProcessPipeline() {
    VkPipelineShaderStageCreateInfo vertexStage{};
    vertexStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexStage.module = shaderModule("shader_fullscreen.vert");
    vertexStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragmentStage{};
    fragmentStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentStage.module = shaderModule("shader_postprocess.frag");
    fragmentStage.pName = "main";
    const std::array<VkPipelineShaderStageCreateInfo, 2> stages = {
        vertexStage,
        fragmentStage
    };

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    const std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<std::uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
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

    if (vkCreateGraphicsPipelines(
            context_->device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
            &postProcessPipeline_
        ) != VK_SUCCESS) {
        throw std::runtime_error("failed to create post-process graphics pipeline");
    }
}

VkPipeline Renderer::createSkyboxPipeline(VkRenderPass pass) {
    VkPipelineShaderStageCreateInfo vertexStage{};
    vertexStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexStage.module = shaderModule("shader_fullscreen.vert");
    vertexStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragmentStage{};
    fragmentStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentStage.module = shaderModule("shader_skybox.frag");
    fragmentStage.pName = "main";
    const std::array<VkPipelineShaderStageCreateInfo, 2> stages = {
        vertexStage,
        fragmentStage
    };

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // The sky is drawn first and never touches depth. Opaque geometry then
    // naturally overwrites it while untouched pixels keep the sky color.
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    const std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<std::uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = pass;
    pipelineInfo.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(
            context_->device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
            &pipeline
        ) != VK_SUCCESS) {
        throw std::runtime_error("failed to create skybox graphics pipeline");
    }
    return pipeline;
}

VkPipeline Renderer::createParticlePipeline(bool additiveBlending, VkRenderPass pass) {
    VkPipelineShaderStageCreateInfo vertexStage{};
    vertexStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexStage.module = shaderModule("shader_particle.vert");
    vertexStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragmentStage{};
    fragmentStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentStage.module = shaderModule("shader_particle.frag");
    fragmentStage.pName = "main";
    const std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {
        vertexStage,
        fragmentStage
    };

    const VkVertexInputBindingDescription bindingDescription =
        ParticleVertex::bindingDescription();
    const auto attributeDescriptions = ParticleVertex::attributeDescriptions();
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount =
        static_cast<std::uint32_t>(attributeDescriptions.size());
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
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = additiveBlending
        ? VK_BLEND_FACTOR_ONE
        : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    const std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<std::uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = pass;
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
        throw std::runtime_error("failed to create particle graphics pipeline");
    }
    return pipeline;
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
    initializeEmptyScene();
}



} // namespace VulkCore
