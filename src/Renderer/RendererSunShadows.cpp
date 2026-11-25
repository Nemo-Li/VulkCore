#include "RendererImplementation.hpp"

#include "vulkcore/Terrain/Terrain.hpp"

namespace VulkCore {

void Renderer::createSunShadowResources() {
#ifdef VULKCORE_OHOS
    // Terrain would otherwise be submitted again for each cascade.
    return;
#endif
    const VkDevice device = context_->device();
    const VkPhysicalDevice physicalDevice = context_->physicalDevice();
    constexpr std::uint32_t tileSize = 1536;
    constexpr VkImageUsageFlags usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkPhysicalDeviceProperties deviceProperties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
    for (const VkFormat candidate : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM}) {
        VkImageFormatProperties properties{};
        if (vkGetPhysicalDeviceImageFormatProperties(physicalDevice, candidate, VK_IMAGE_TYPE_2D,
                VK_IMAGE_TILING_OPTIMAL, usage, 0, &properties) == VK_SUCCESS) {
            const auto& limits = deviceProperties.limits;
            sunShadowTileSize_ = std::min({tileSize, properties.maxExtent.width / 2,
                properties.maxExtent.height / 2, limits.maxFramebufferWidth / 2,
                limits.maxFramebufferHeight / 2});
            if (sunShadowTileSize_ >= 256) {
                format = candidate;
                break;
            }
        }
    }
    if (format == VK_FORMAT_UNDEFINED) {
        std::cerr << "warning: sampled depth format unavailable for sun shadows\n";
        return;
    }

    const auto check = [](VkResult result) {
        if (result != VK_SUCCESS) throw std::runtime_error("failed to allocate sun shadow resources");
    };
    try {
        VkAttachmentDescription attachment{};
        attachment.format = format;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        const VkAttachmentReference depth{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.pDepthStencilAttachment = &depth;
        // The frame fence covers the previous use of this atlas. This dependency makes all
        // early and late depth writes and the layout transition visible to scene receivers.
        const std::array dependencies{
            VkSubpassDependency{VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 0},
            VkSubpassDependency{0, VK_SUBPASS_EXTERNAL,
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT, 0}
        };
        VkRenderPassCreateInfo passInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        passInfo.attachmentCount = 1;
        passInfo.pAttachments = &attachment;
        passInfo.subpassCount = 1;
        passInfo.pSubpasses = &subpass;
        passInfo.dependencyCount = static_cast<std::uint32_t>(dependencies.size());
        passInfo.pDependencies = dependencies.data();
        check(vkCreateRenderPass(device, &passInfo, nullptr, &sunShadowRenderPass_));

        VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        check(vkCreateSampler(device, &samplerInfo, nullptr, &sunShadowSampler_));

        const std::uint32_t size = sunShadowTileSize_ * 2;
        for (auto& target : sunShadowTargets_) {
            VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.format = format;
            imageInfo.extent = {size, size, 1};
            imageInfo.mipLevels = imageInfo.arrayLayers = 1;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage = usage;
            check(vkCreateImage(device, &imageInfo, nullptr, &target.image));

            VkMemoryRequirements requirements{};
            vkGetImageMemoryRequirements(device, target.image, &requirements);
            VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocate.allocationSize = requirements.size;
            allocate.memoryTypeIndex = context_->findMemoryType(requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            check(vkAllocateMemory(device, &allocate, nullptr, &target.memory));
            check(vkBindImageMemory(device, target.image, target.memory, 0));

            VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            viewInfo.image = target.image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = format;
            viewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            check(vkCreateImageView(device, &viewInfo, nullptr, &target.view));

            VkFramebufferCreateInfo framebufferInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            framebufferInfo.renderPass = sunShadowRenderPass_;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = &target.view;
            framebufferInfo.width = framebufferInfo.height = size;
            framebufferInfo.layers = 1;
            check(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &target.framebuffer));
        }
    } catch (const std::exception& error) {
        destroySunShadowResources();
        std::cerr << "warning: " << error.what() << '\n';
    }
}

void Renderer::destroySunShadowResources() {
    const VkDevice device = context_->device();
    vkDestroyPipeline(device, sunShadowParticlePipeline_, nullptr);
    sunShadowParticlePipeline_ = VK_NULL_HANDLE;
    for (auto& target : sunShadowTargets_) {
        vkDestroyFramebuffer(device, target.framebuffer, nullptr);
        vkDestroyImageView(device, target.view, nullptr);
        vkDestroyImage(device, target.image, nullptr);
        vkFreeMemory(device, target.memory, nullptr);
        target = {};
    }
    vkDestroySampler(device, sunShadowSampler_, nullptr);
    vkDestroyRenderPass(device, sunShadowRenderPass_, nullptr);
    sunShadowSampler_ = VK_NULL_HANDLE;
    sunShadowRenderPass_ = VK_NULL_HANDLE;
    sunShadowTileSize_ = 0;
}

VkDescriptorImageInfo Renderer::sunShadowImageInfo(std::uint32_t frame) const {
    if (sunShadowRenderPass_) {
        return {sunShadowSampler_, sunShadowTargets_[frame].view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    }
    return {fallbackTexture_->sampler(), fallbackTexture_->imageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
}

void Renderer::updateSunShadowUniforms(UniformBufferObject& frame) {
    sunShadowsActive_ = sunShadowRenderPass_ && frame.sunLightDirection.w > 0.0f;
    if (!sunShadowsActive_) return;

    const glm::vec3 sun = glm::normalize(glm::vec3(frame.sunLightDirection));
    const glm::vec3 up = std::abs(sun.y) > 0.95f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    const glm::mat4 lightView = glm::lookAt(glm::vec3(0.0f), -sun, up);
    glm::vec3 minimum(std::numeric_limits<float>::max());
    glm::vec3 maximum(std::numeric_limits<float>::lowest());
    const auto includePoint = [&](glm::vec3 point) {
        minimum = glm::min(minimum, point);
        maximum = glm::max(maximum, point);
    };
    const auto includeBox = [&](const glm::mat4& transform, glm::vec3 lo, glm::vec3 hi) {
        for (unsigned corner = 0; corner < 8; ++corner) {
            const glm::vec3 p(corner & 1 ? hi.x : lo.x, corner & 2 ? hi.y : lo.y, corner & 4 ? hi.z : lo.z);
            includePoint(glm::vec3(transform * glm::vec4(p, 1.0f)));
        }
    };
    const auto includeNode = [&](const auto& self, const ModelNode& node) -> void {
        if (!isSceneNodeRenderEnabled(node.nodeId) || node.nodeId == kSunSceneNodeId) return;
        if (node.hasBounds && !node.meshPartIndices.empty()) {
            includeBox(lightView * frame.model * transformForSceneNode(node.nodeId),
                node.boundsMinimum, node.boundsMaximum);
        }
        for (const ModelNode& child : node.children) self(self, child);
    };
    includeNode(includeNode, meshData_.hierarchy);
    for (const ModelNode& root : meshData_.additionalNodes) includeNode(includeNode, root);
    if (terrainScene_) {
        includeBox(lightView * frame.model, terrainScene_->boundsMinimum(), terrainScene_->boundsMaximum());
    }
    for (const ParticleVertex& vertex : particleVertices_) {
        includePoint(glm::vec3(lightView * glm::vec4(vertex.position, 1.0f)));
    }

    const float scale = std::max(frame.cameraPosition.w, 1e-8f);
    const float nearSplit = terrainScene_ ? 32.0f * scale : 5.0f;
    const float midSplit = terrainScene_ ? 256.0f * scale : 22.0f;
    frame.sunShadowParams = glm::vec4(nearSplit, midSplit, static_cast<float>(sunShadowTileSize_), 1.0f);
    const glm::mat4 lightFromView = lightView * glm::inverse(frame.view);
    const glm::mat4 viewFromClip = glm::inverse(frame.projection);
    const glm::vec3 lightCamera(lightFromView[3]);
    if (minimum.x > maximum.x) {
        minimum = lightCamera - glm::vec3(midSplit);
        maximum = lightCamera + glm::vec3(midSplit);
    }
    // Keep the light-axis range large enough for off-camera occluders, including terrain
    // peaks, while fitting each near slice tightly in the other two dimensions.
    const float zPad = std::max({0.05f * (maximum.z - minimum.z), 4.0f * scale, 0.01f * midSplit});
    const float zNear = -maximum.z - zPad;
    const float zFar = -minimum.z + zPad;
    const auto sliceProjection = [&](float start, float end, std::size_t cascade) {
        glm::vec2 center = glm::vec2(minimum + maximum) * 0.5f;
        float radius = std::max(std::max(maximum.x - minimum.x, maximum.y - minimum.y) * 0.5f, midSplit * 0.05f);
        if (cascade + 1 < kSunShadowCascadeCount) {
            const float middle = (start + end) * 0.5f;
            center = glm::vec2(lightFromView * glm::vec4(0, 0, -middle, 1));
            radius = 0.0f;
            for (const float distance : {start, end}) {
                for (const float x : {-1.0f, 1.0f}) {
                    for (const float y : {-1.0f, 1.0f}) {
                        const glm::vec3 p(viewFromClip * glm::vec4(x, y, 0, 1));
                        const glm::vec3 corner = p * (distance / -p.z);
                        radius = std::max(radius, glm::length(corner - glm::vec3(0, 0, -middle)));
                    }
                }
            }
        }
        radius *= 1.06f; // Leave room for PCF and cascade blending at the edges.
        const float texel = 2.0f * radius / sunShadowTileSize_;
        center = glm::round(center / texel) * texel; // Stabilize the map under camera translation.
        frame.sunShadowBias[static_cast<int>(cascade)] = std::max(0.22f * texel / (zFar - zNear), 0.000005f);
        return glm::orthoRH_ZO(center.x - radius, center.x + radius,
            center.y - radius, center.y + radius, zNear, zFar);
    };

    const std::array projections{sliceProjection(0.0f, nearSplit, 0),
        sliceProjection(nearSplit * 0.82f, midSplit, 1), sliceProjection(0.0f, 0.0f, 2)};
    for (std::size_t i = 0; i < kSunShadowCascadeCount; ++i) {
        frame.sunShadowMatrices[i] = projections[i] * lightView;
        sunShadowClipFromWorld_[i] = frame.sunShadowMatrices[i] * frame.model;
        // Reuse material vertex stages with the light's projection. The actual camera
        // position stays intact for grass LOD and other view-dependent geometry.
        UniformBufferObject shadow = frame;
        shadow.view = lightView;
        shadow.projection = projections[i];
        shadow.sunShadowParams.w = 0.0f;
        uniformBuffers_[currentFrame_]->upload(&shadow, sizeof(shadow), uniformSliceStride_ * (i + 1));
    }
}

} // namespace VulkCore
