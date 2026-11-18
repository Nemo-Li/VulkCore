#include "HdrComposition.hpp"

#include <stdexcept>

#include "RendererImplementation.hpp"
#include "vulkcore/Renderer/SceneProjection.hpp"

namespace VulkCore {
namespace {

constexpr VkFormat kHdr = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat kWeights = VK_FORMAT_R8G8B8A8_UNORM;

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) throw std::runtime_error(operation);
}

VkRenderPass createPass(VkDevice device, VkFormat color, VkFormat depth, bool present = false) {
    std::array<VkAttachmentDescription, 2> attachments{};
    attachments[0].format = color;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = present ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = present ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                         : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    attachments[1] = attachments[0];
    attachments[1].format = depth;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    const VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = depth != VK_FORMAT_UNDEFINED ? &depthRef : nullptr;

    std::array<VkSubpassDependency, 2> dependencies{};
    // Presentation matches the existing ImGui render-pass compatibility key.
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = depth == VK_FORMAT_UNDEFINED ? 1u : 2u;
    info.pAttachments = attachments.data();
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = present ? 1u : 2u;
    info.pDependencies = dependencies.data();
    VkRenderPass pass = VK_NULL_HANDLE;
    check(vkCreateRenderPass(device, &info, nullptr, &pass), "failed to create HDR render pass");
    return pass;
}

bool surfaceEncodesSrgb(VkFormat format) {
    return format == VK_FORMAT_R8G8B8A8_SRGB || format == VK_FORMAT_B8G8R8A8_SRGB ||
           format == VK_FORMAT_A8B8G8R8_SRGB_PACK32;
}

} // namespace

bool HdrComposition::supported(const VulkanContext& context) {
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(context.physicalDevice(), kHdr, &properties);
    constexpr auto needed = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
                            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                            VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    return (properties.optimalTilingFeatures & needed) == needed;
}

HdrComposition::HdrComposition(VulkanContext& context, const SwapChain& swapChain,
                               const HdrSettings& settings, const ShaderLookup& shader)
    : context_(context), extent_(swapChain.extent()),
      encodeSrgb_(!surfaceEncodesSrgb(swapChain.imageFormat())) {
    try {
        const auto device = context_.device();
        geometryPass_ = createPass(device, kHdr, swapChain.depthFormat());
        presentationPass_ = createPass(device, swapChain.imageFormat(), swapChain.depthFormat(), true);
        std::vector<VkExtent2D> bloomSizes;
        for (VkExtent2D size = extent_; static_cast<int>(bloomSizes.size()) < settings.bloomLevels;) {
            size = {(size.width + 1) / 2, (size.height + 1) / 2};
            bloomSizes.push_back(size);
            if (size.width == 1 && size.height == 1) break;
        }
        createLayouts(kMaxFramesInFlight * (1 + 2 * bloomSizes.size() + (settings.smaa ? 2 : 0)));
        composite_ = createPipeline(presentationPass_, shader, "shader_hdr_composite.frag");
        if (!bloomSizes.empty()) {
            filterPass_ = createPass(device, kHdr, VK_FORMAT_UNDEFINED);
            extract_ = createPipeline(filterPass_, shader, "shader_hdr_extract.frag");
            down_ = createPipeline(filterPass_, shader, "shader_hdr_down.frag");
            up_ = createPipeline(filterPass_, shader, "shader_hdr_up.frag");
        }
        if (settings.smaa) {
            aaPass_ = createPass(device, kWeights, VK_FORMAT_UNDEFINED);
            const auto data = std::filesystem::path(VULKCORE_ASSET_DIR) / "textures" / "smaa";
            constexpr auto clamp = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            area_ = std::make_unique<Texture>(context_, data / "area.png", clamp, clamp, TextureColorSpace::Linear);
            search_ = std::make_unique<Texture>(context_, data / "search.png", clamp, clamp, TextureColorSpace::Linear);
            edges_ = createPipeline(aaPass_, shader, "shader_hdr_edges.frag");
            weights_ = createPipeline(aaPass_, shader, "shader_hdr_weights.frag");
        }
        frames_.resize(kMaxFramesInFlight);
        for (Frame& frame : frames_) {
            createTarget(frame.geometry, extent_, geometryPass_, kHdr, swapChain.depthFormat());
            VkImageView source = frame.geometry.color.view;
            VkImageView bloom = source;
            VkImageView weights = source;
            if (!bloomSizes.empty()) {
                createTarget(frame.luminance, extent_, filterPass_, kHdr);
                frame.extract = descriptors(source);
                frame.down.resize(bloomSizes.size());
                frame.up.resize(bloomSizes.size() - 1);
                VkImageView previous = frame.luminance.color.view;
                for (std::size_t level = 0; level < frame.down.size(); ++level) {
                    createTarget(frame.down[level], bloomSizes[level], filterPass_, kHdr);
                    frame.downSets.push_back(descriptors(previous));
                    previous = frame.down[level].color.view;
                }
                frame.upSets.resize(frame.up.size());
                for (std::size_t level = frame.up.size(); level-- > 0;) {
                    createTarget(frame.up[level], bloomSizes[level], filterPass_, kHdr);
                    frame.upSets[level] = descriptors(previous, frame.down[level].color.view);
                    previous = frame.up[level].color.view;
                }
                bloom = previous;
            }
            if (settings.smaa) {
                createTarget(frame.edges, extent_, aaPass_, kWeights);
                createTarget(frame.weights, extent_, aaPass_, kWeights);
                frame.edgeSet = descriptors(source);
                frame.weightSet = descriptors(frame.edges.color.view, area_->imageView(), search_->imageView(), true);
                weights = frame.weights.color.view;
            }
            frame.composite = descriptors(source, bloom, weights);
        }
    } catch (...) {
        cleanup();
        throw;
    }
}

HdrComposition::~HdrComposition() { cleanup(); }

void HdrComposition::createImage(Image& image, VkExtent2D extent, VkFormat format, bool depth) {
    const auto device = context_.device();
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {extent.width, extent.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                       : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    check(vkCreateImage(device, &info, nullptr, &image.image), "failed to create HDR target image");
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, image.image, &requirements);
    VkMemoryAllocateInfo memory{};
    memory.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memory.allocationSize = requirements.size;
    memory.memoryTypeIndex = context_.findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device, &memory, nullptr, &image.memory), "failed to allocate HDR target");
    check(vkBindImageMemory(device, image.image, image.memory, 0), "failed to bind HDR target");
    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = image.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    check(vkCreateImageView(device, &view, nullptr, &image.view), "failed to create HDR target view");
}

void HdrComposition::createTarget(Target& target, VkExtent2D extent, VkRenderPass pass,
                                  VkFormat color, VkFormat depth) {
    target.extent = extent;
    createImage(target.color, extent, color, false);
    if (depth != VK_FORMAT_UNDEFINED) createImage(target.depth, extent, depth, true);
    const std::array views{target.color.view, target.depth.view};
    VkFramebufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    info.renderPass = pass;
    info.attachmentCount = depth == VK_FORMAT_UNDEFINED ? 1u : 2u;
    info.pAttachments = views.data();
    info.width = extent.width;
    info.height = extent.height;
    info.layers = 1;
    check(vkCreateFramebuffer(context_.device(), &info, nullptr, &target.framebuffer),
          "failed to create HDR framebuffer");
}

void HdrComposition::createLayouts(std::size_t setCount) {
    const auto device = context_.device();
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
    for (uint32_t i = 0; i < bindings.size(); ++i) {
        bindings[i] = {i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    }
    VkDescriptorSetLayoutCreateInfo set{};
    set.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set.bindingCount = static_cast<uint32_t>(bindings.size());
    set.pBindings = bindings.data();
    check(vkCreateDescriptorSetLayout(device, &set, nullptr, &setLayout_), "failed to create HDR descriptor layout");
    const VkPushConstantRange range{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::vec4)};
    VkPipelineLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &setLayout_;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &range;
    check(vkCreatePipelineLayout(device, &layout, nullptr, &layout_), "failed to create HDR pipeline layout");

    const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, uint32_t(3 * setCount)};
    VkDescriptorPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = uint32_t(setCount);
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &size;
    check(vkCreateDescriptorPool(device, &pool, nullptr, &pool_), "failed to create HDR descriptor pool");

    VkSamplerCreateInfo sampler{};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    check(vkCreateSampler(device, &sampler, nullptr, &linearSampler_), "failed to create HDR sampler");
    sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
    check(vkCreateSampler(device, &sampler, nullptr, &nearestSampler_), "failed to create nearest HDR sampler");
}

VkDescriptorSet HdrComposition::descriptors(VkImageView source, VkImageView second,
                                            VkImageView third, bool lookup) const {
    const auto device = context_.device();
    VkDescriptorSetAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = pool_;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &setLayout_;
    VkDescriptorSet set = VK_NULL_HANDLE;
    check(vkAllocateDescriptorSets(device, &allocate, &set), "failed to allocate HDR descriptor set");
    const std::array views{source, second ? second : source, third ? third : source};
    std::array<VkDescriptorImageInfo, 3> images{};
    std::array<VkWriteDescriptorSet, 3> writes{};
    for (uint32_t i = 0; i < writes.size(); ++i) {
        images[i] = {lookup && i == 2 ? nearestSampler_ : linearSampler_,
                     views[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &images[i];
    }
    vkUpdateDescriptorSets(device, uint32_t(writes.size()), writes.data(), 0, nullptr);
    return set;
}

VkPipeline HdrComposition::createPipeline(VkRenderPass pass, const ShaderLookup& shader, const char* fragment) {
    const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,
         shader("shader_fullscreen.vert"), "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT,
         shader(fragment), "main", nullptr}
    }};
    VkPipelineVertexInputStateCreateInfo vertex{};
    vertex.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo samples{};
    samples.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    const std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = uint32_t(dynamicStates.size());
    dynamic.pDynamicStates = dynamicStates.data();
    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = uint32_t(stages.size());
    info.pStages = stages.data();
    info.pVertexInputState = &vertex;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &samples;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout_;
    info.renderPass = pass;
    VkPipeline pipeline = VK_NULL_HANDLE;
    check(vkCreateGraphicsPipelines(context_.device(), VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
          "failed to create HDR fullscreen pipeline");
    return pipeline;
}

void HdrComposition::draw(VkCommandBuffer commands, VkExtent2D extent, VkPipeline pipeline,
                          VkDescriptorSet set, const glm::vec4& parameters) const {
    const VkViewport viewport{0.0f, 0.0f, float(extent.width), float(extent.height), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &scissor);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set, 0, nullptr);
    vkCmdPushConstants(commands, layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(parameters), &parameters);
    vkCmdDraw(commands, 3, 1, 0, 0);
}

void HdrComposition::run(VkCommandBuffer commands, const Target& target, VkRenderPass pass,
                         VkPipeline pipeline, VkDescriptorSet set, const glm::vec4& parameters) const {
    const VkClearValue clear{};
    VkRenderPassBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = pass;
    begin.framebuffer = target.framebuffer;
    begin.renderArea.extent = target.extent;
    begin.clearValueCount = 1;
    begin.pClearValues = &clear;
    vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
    draw(commands, target.extent, pipeline, set, parameters);
    vkCmdEndRenderPass(commands);
}

void HdrComposition::beginGeometry(VkCommandBuffer commands, std::size_t frame, glm::vec3 background) const {
    std::array<VkClearValue, 2> clear{};
    clear[0].color = {{background.r, background.g, background.b, 1.0f}};
    clear[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = geometryPass_;
    begin.framebuffer = frames_[frame].geometry.framebuffer;
    begin.renderArea.extent = extent_;
    begin.clearValueCount = uint32_t(clear.size());
    begin.pClearValues = clear.data();
    vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
}

void HdrComposition::beginPresentation(VkCommandBuffer commands, std::size_t frameIndex,
                                      VkFramebuffer framebuffer, const HdrSettings& settings) const {
    const Frame& frame = frames_[frameIndex];
    if (!frame.down.empty()) {
        run(commands, frame.luminance, filterPass_, extract_, frame.extract,
            {settings.bloomThreshold, settings.bloomSmoothing, 0.0f, 0.0f});
        for (std::size_t level = 0; level < frame.down.size(); ++level) {
            run(commands, frame.down[level], filterPass_, down_, frame.downSets[level]);
        }
        for (std::size_t level = frame.up.size(); level-- > 0;) {
            run(commands, frame.up[level], filterPass_, up_, frame.upSets[level],
                {settings.bloomRadius, 0.0f, 0.0f, 0.0f});
        }
    }
    if (settings.smaa) {
        run(commands, frame.edges, aaPass_, edges_, frame.edgeSet);
        run(commands, frame.weights, aaPass_, weights_, frame.weightSet);
    }
    VkRenderPassBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = presentationPass_;
    begin.framebuffer = framebuffer;
    begin.renderArea.extent = extent_;
    vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
    draw(commands, extent_, composite_, frame.composite,
         {frame.down.empty() ? 0.0f : settings.bloomIntensity,
          settings.smaa ? 1.0f : 0.0f, encodeSrgb_ ? 1.0f : 0.0f, 0.0f});
}

void HdrComposition::destroyTarget(Target& target) {
    const auto device = context_.device();
    vkDestroyFramebuffer(device, target.framebuffer, nullptr);
    for (const auto& image : {target.color, target.depth}) {
        vkDestroyImageView(device, image.view, nullptr);
        vkDestroyImage(device, image.image, nullptr);
        vkFreeMemory(device, image.memory, nullptr);
    }
}

void HdrComposition::cleanup() {
    const auto device = context_.device();
    vkDestroyDescriptorPool(device, pool_, nullptr);
    for (Frame& frame : frames_) {
        destroyTarget(frame.geometry);
        destroyTarget(frame.luminance);
        for (Target& target : frame.down) destroyTarget(target);
        for (Target& target : frame.up) destroyTarget(target);
        destroyTarget(frame.edges);
        destroyTarget(frame.weights);
    }
    for (VkPipeline pipeline : {extract_, down_, up_, edges_, weights_, composite_}) {
        vkDestroyPipeline(device, pipeline, nullptr);
    }
    for (VkRenderPass pass : {geometryPass_, filterPass_, aaPass_, presentationPass_}) {
        vkDestroyRenderPass(device, pass, nullptr);
    }
    vkDestroySampler(device, linearSampler_, nullptr);
    vkDestroySampler(device, nearestSampler_, nullptr);
    vkDestroyPipelineLayout(device, layout_, nullptr);
    vkDestroyDescriptorSetLayout(device, setLayout_, nullptr);
}

void Renderer::configureHdrComposition() {
    hdrComposition_.reset();
    if (!hdrSettings_.enabled) return;
    if (!HdrComposition::supported(*context_)) {
        std::cerr << "warning: FP16 HDR blending is unavailable, using the legacy pipeline\n";
        return;
    }
    hdrComposition_ = std::make_unique<HdrComposition>(
        *context_, *swapChain_, hdrSettings_, [this](const char* name) { return shaderModule(name); }
    );
    if (hdrSkyboxPipeline_ == VK_NULL_HANDLE) {
        const VkRenderPass pass = hdrComposition_->geometryPass();
        hdrSkyboxPipeline_ = createSkyboxPipeline(pass);
        hdrParticleAlphaPipeline_ = createParticlePipeline(false, pass);
        hdrParticleAdditivePipeline_ = createParticlePipeline(true, pass);
    }
}

} // namespace VulkCore
