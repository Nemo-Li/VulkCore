#include "RendererImplementation.hpp"

#include <utility>
#include "HdrComposition.hpp"

#include <ctime>

#include "vulkcore/Renderer/Frustum.hpp"
#include "vulkcore/Utils/Screenshot.hpp"

namespace VulkCore {

void Renderer::ensureScreenshotBuffer() {
    const VkExtent2D extent = swapChain_->extent();
    if (extent.width == 0 || extent.height == 0) {
        throw std::runtime_error("cannot capture a zero-sized swap chain image");
    }

    const VkDeviceSize requiredSize =
        static_cast<VkDeviceSize>(extent.width) *
        static_cast<VkDeviceSize>(extent.height) * 4u;
    if (screenshotBuffer_ != nullptr && screenshotBufferSize_ >= requiredSize) {
        return;
    }

    screenshotBuffer_ = std::make_unique<Buffer>(
        *context_,
        requiredSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    screenshotBufferSize_ = requiredSize;
}

void Renderer::recordScreenshotCopy(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    if (screenshotBuffer_ == nullptr) {
        throw std::runtime_error("screenshot buffer has not been initialized");
    }

    const VkExtent2D extent = swapChain_->extent();
    VkImageMemoryBarrier imageToTransfer{};
    imageToTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageToTransfer.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    imageToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    imageToTransfer.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    imageToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    imageToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageToTransfer.image = swapChain_->image(imageIndex);
    imageToTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageToTransfer.subresourceRange.levelCount = 1;
    imageToTransfer.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &imageToTransfer
    );

    VkBufferImageCopy copyRegion{};
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageExtent = {extent.width, extent.height, 1};
    vkCmdCopyImageToBuffer(
        commandBuffer,
        swapChain_->image(imageIndex),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        screenshotBuffer_->handle(),
        1,
        &copyRegion
    );

    VkImageMemoryBarrier imageToPresent{};
    imageToPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageToPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    imageToPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    imageToPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    imageToPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageToPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageToPresent.image = swapChain_->image(imageIndex);
    imageToPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageToPresent.subresourceRange.levelCount = 1;
    imageToPresent.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &imageToPresent
    );
}

void Renderer::saveScreenshot(const std::filesystem::path& output) {
    if (screenshotBuffer_ == nullptr) {
        throw std::runtime_error("screenshot buffer has not been initialized");
    }

    const VkExtent2D extent = swapChain_->extent();
    const std::size_t pixelCount =
        static_cast<std::size_t>(extent.width) * static_cast<std::size_t>(extent.height);
    const std::size_t byteCount = pixelCount * 4u;
    std::vector<std::uint8_t> sourcePixels(byteCount);
    screenshotBuffer_->download(sourcePixels.data(), byteCount);

    const VkFormat format = swapChain_->imageFormat();
    const bool isBgra =
        format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_B8G8R8A8_UNORM;
    const bool isRgba =
        format == VK_FORMAT_R8G8B8A8_SRGB || format == VK_FORMAT_R8G8B8A8_UNORM;
    if (!isBgra && !isRgba) {
        throw std::runtime_error("unsupported swap chain format for screenshot");
    }

    std::vector<std::uint8_t> rgbaPixels(byteCount);
    if (isRgba) {
        rgbaPixels = std::move(sourcePixels);
    } else {
        for (std::size_t index = 0; index < pixelCount; ++index) {
            const std::size_t offset = index * 4u;
            rgbaPixels[offset + 0] = sourcePixels[offset + 2];
            rgbaPixels[offset + 1] = sourcePixels[offset + 1];
            rgbaPixels[offset + 2] = sourcePixels[offset + 0];
            rgbaPixels[offset + 3] = sourcePixels[offset + 3];
        }
    }

    if (!output.empty()) {
        writeRgbaPng(output, extent.width, extent.height, rgbaPixels);
        return;
    }
    const std::filesystem::path directory =
        std::filesystem::current_path() / "screenshots";
    std::filesystem::create_directories(directory);

    const auto now = std::chrono::system_clock::now();
    const std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
    const std::tm* localTime = std::localtime(&timestamp);
    if (localTime == nullptr) {
        throw std::runtime_error("failed to determine screenshot timestamp");
    }

    std::ostringstream filename;
    filename << "vulkcore_"
             << std::put_time(localTime, "%Y%m%d_%H%M%S")
             << "_" << screenshotSequence_++ << ".png";
    const std::filesystem::path path = directory / filename.str();
    writeRgbaPng(path, extent.width, extent.height, rgbaPixels);
    const std::filesystem::path absolutePath = std::filesystem::absolute(path);
    screenshotStatus_ = "Screenshot saved: " + absolutePath.string();
    std::cerr << screenshotStatus_ << '\n';
}

void Renderer::recordLegacySceneTransfer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    const auto& sceneTarget = sceneRenderTargets_[currentFrame_];
    const auto extent = swapChain_->extent();
    VkImageMemoryBarrier sceneColorToTransfer{};
    sceneColorToTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    sceneColorToTransfer.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    sceneColorToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    sceneColorToTransfer.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    sceneColorToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    sceneColorToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    sceneColorToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    sceneColorToTransfer.image = sceneTarget.colorImage;
    sceneColorToTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    sceneColorToTransfer.subresourceRange.levelCount = 1;
    sceneColorToTransfer.subresourceRange.layerCount = 1;

    VkImageMemoryBarrier sceneDepthToTransfer{};
    sceneDepthToTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    sceneDepthToTransfer.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    sceneDepthToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    sceneDepthToTransfer.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    sceneDepthToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    sceneDepthToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    sceneDepthToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    sceneDepthToTransfer.image = sceneTarget.depthImage;
    sceneDepthToTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    sceneDepthToTransfer.subresourceRange.levelCount = 1;
    sceneDepthToTransfer.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        2,
        std::array<VkImageMemoryBarrier, 2>{sceneColorToTransfer, sceneDepthToTransfer}.data()
    );

    VkImageMemoryBarrier swapColorToTransfer{};
    swapColorToTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    swapColorToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    swapColorToTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    swapColorToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapColorToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapColorToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapColorToTransfer.image = swapChain_->image(imageIndex);
    swapColorToTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    swapColorToTransfer.subresourceRange.levelCount = 1;
    swapColorToTransfer.subresourceRange.layerCount = 1;

    VkImageMemoryBarrier swapDepthToTransfer{};
    swapDepthToTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    swapDepthToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    swapDepthToTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    swapDepthToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapDepthToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapDepthToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapDepthToTransfer.image = swapChain_->depthImage(imageIndex);
    swapDepthToTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    swapDepthToTransfer.subresourceRange.levelCount = 1;
    swapDepthToTransfer.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        commandBuffer,
        // The swap-chain acquire semaphore is waited at TRANSFER. Chain
        // the destination layout transitions to that wait as well.
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        2,
        std::array<VkImageMemoryBarrier, 2>{swapColorToTransfer, swapDepthToTransfer}.data()
    );

    VkImageCopy colorCopy{};
    colorCopy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorCopy.srcSubresource.layerCount = 1;
    colorCopy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorCopy.dstSubresource.layerCount = 1;
    colorCopy.extent = {extent.width, extent.height, 1};
    vkCmdCopyImage(
        commandBuffer,
        sceneTarget.colorImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        swapChain_->image(imageIndex),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &colorCopy
    );

    VkImageCopy depthCopy{};
    depthCopy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthCopy.srcSubresource.layerCount = 1;
    depthCopy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthCopy.dstSubresource.layerCount = 1;
    depthCopy.extent = {extent.width, extent.height, 1};
    vkCmdCopyImage(
        commandBuffer,
        sceneTarget.depthImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        swapChain_->depthImage(imageIndex),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &depthCopy
    );

    VkImageMemoryBarrier sceneColorToShader{};
    sceneColorToShader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    sceneColorToShader.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    sceneColorToShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    sceneColorToShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    sceneColorToShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    sceneColorToShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    sceneColorToShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    sceneColorToShader.image = sceneTarget.colorImage;
    sceneColorToShader.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    sceneColorToShader.subresourceRange.levelCount = 1;
    sceneColorToShader.subresourceRange.layerCount = 1;

    VkImageMemoryBarrier sceneDepthToShader{};
    sceneDepthToShader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    sceneDepthToShader.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    sceneDepthToShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    sceneDepthToShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    sceneDepthToShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    sceneDepthToShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    sceneDepthToShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    sceneDepthToShader.image = sceneTarget.depthImage;
    sceneDepthToShader.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    sceneDepthToShader.subresourceRange.levelCount = 1;
    sceneDepthToShader.subresourceRange.layerCount = 1;

    VkImageMemoryBarrier swapColorToAttachment{};
    swapColorToAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    swapColorToAttachment.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    swapColorToAttachment.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    swapColorToAttachment.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapColorToAttachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    swapColorToAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapColorToAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapColorToAttachment.image = swapChain_->image(imageIndex);
    swapColorToAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    swapColorToAttachment.subresourceRange.levelCount = 1;
    swapColorToAttachment.subresourceRange.layerCount = 1;

    VkImageMemoryBarrier swapDepthToAttachment{};
    swapDepthToAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    swapDepthToAttachment.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    swapDepthToAttachment.dstAccessMask =
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    swapDepthToAttachment.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapDepthToAttachment.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    swapDepthToAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapDepthToAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapDepthToAttachment.image = swapChain_->depthImage(imageIndex);
    swapDepthToAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    swapDepthToAttachment.subresourceRange.levelCount = 1;
    swapDepthToAttachment.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        4,
        std::array<VkImageMemoryBarrier, 4>{
            sceneColorToShader,
            sceneDepthToShader,
            swapColorToAttachment,
            swapDepthToAttachment
        }.data()
    );
}

void Renderer::recordCommandBuffer(
    VkCommandBuffer commandBuffer,
    uint32_t imageIndex,
    bool captureScreenshot,
    const std::optional<glm::mat4>& clipFromScene
) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    const VkExtent2D extent = swapChain_->extent();
    std::uint32_t uniformOffset = 0;
    const uint32_t materialCount = static_cast<uint32_t>(meshData_.materials.size());

    VkViewport viewport{};
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    const VkRect2D scissor{{0, 0}, extent};

    const VkBuffer buffers[] = {vertexBuffers_[currentFrame_]->handle()};
    const VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer_->handle(), 0, VK_INDEX_TYPE_UINT32);

    const auto bindSceneViewport = [&]() {
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    };

    struct DrawItem {
        const MeshPart* part;
        std::uint32_t materialIndex;
        std::optional<GrassLod> grassLod;
    };
    std::vector<DrawItem> opaque;
    std::vector<DrawItem> transparent;
    std::vector<DrawItem> casters;
    bool needsOpaqueSnapshot = realtimeNeedsOpaqueSnapshot();
    terrainDrawCandidates_ = 0;
    terrainDrawCulled_ = 0;
    for (const MeshPart& part : meshData_.parts) {
        const auto grassLod = grassLodForLayer(part.nodeId - kGrassLodNodeBase);
        if ((!grassLod && part.indexCount == 0) || !isSceneNodeRenderEnabled(part.nodeId)) continue;
        if (grassLod && (grassCoverage_.ranges().empty() || !grassIndexBuffer_)) continue;
        const std::uint32_t index = materialIndexForPart(part);
        if (index >= materialCount || index >= materialTextures_.size()) continue;
        const MaterialData& material = meshData_.materials[index];
        if (material.shaderConfiguration.shader(ShaderStage::Vertex).starts_with("shader_grass")
            != grassLod.has_value()) continue;
        if (material.transitionOpacity && transitionSettings_.enabled &&
            (transitionPlayer_.progress < material.transitionVisibleFrom ||
             transitionPlayer_.progress > material.transitionVisibleUntil)) continue;
        // The light sees off-camera geometry that can still occlude visible surfaces.
        // Skip dense grass blades to avoid drawing them again for each shadow cascade.
        if (sunShadowsActive_ && part.nodeId != kSunSceneNodeId && grassLod != GrassLod::NearMesh) {
            casters.push_back({&part, index, grassLod});
        }
        if (const auto& bounds = part.terrainBounds) {
            ++terrainDrawCandidates_;
            // The terrain shader applies the same transform as the CPU test.
            // Other geometry stages may displace vertices beyond these bounds.
            const auto& shaders = material.shaderConfiguration;
            if (terrainFrustumCullingEnabled_ && clipFromScene &&
                shaders.shader(ShaderStage::Vertex) == "shader_terrain.vert" &&
                shaders.shader(ShaderStage::TessellationControl).empty() &&
                shaders.shader(ShaderStage::TessellationEvaluation).empty() &&
                shaders.shader(ShaderStage::Geometry).empty() &&
                isOutsideVulkanFrustum(
                    *clipFromScene * transformForSceneNode(part.nodeId),
                    bounds->minimum,
                    bounds->maximum
                )) {
                ++terrainDrawCulled_;
                continue;
            }
        }
        const bool readsOpaque = material.samplesOpaqueScene ||
            isWaterShaderConfiguration(material.shaderConfiguration);
        needsOpaqueSnapshot |= readsOpaque;
        auto& queue = readsOpaque || isTransparentMaterial(material) ? transparent : opaque;
        queue.push_back({&part, index, grassLod});
    }

    const bool grassEnabled = isSceneNodeRenderEnabled(kGrassSceneNodeId);
    VkPipeline boundPipeline = VK_NULL_HANDLE;
    VkDescriptorSet boundSet = VK_NULL_HANDLE;
    const auto drawPart = [&](const DrawItem& item, bool hdr, int cascade = -1) {
        const MeshPart& part = *item.part;
        const bool shadow = cascade >= 0;
        const std::uint32_t materialIndex = item.materialIndex;
        const MaterialData& material = meshData_.materials[materialIndex];
        const auto& shaders = material.shaderConfiguration;
        const glm::mat4 nodeTransform = transformForSceneNode(part.nodeId);
        if (shadow && part.terrainBounds && shaders.shader(ShaderStage::Vertex) == "shader_terrain.vert" &&
            shaders.shader(ShaderStage::TessellationControl).empty() &&
            shaders.shader(ShaderStage::TessellationEvaluation).empty() &&
            shaders.shader(ShaderStage::Geometry).empty() && isOutsideVulkanFrustum(
                sunShadowClipFromWorld_[cascade] * nodeTransform,
                part.terrainBounds->minimum, part.terrainBounds->maximum)) return;
        const VkDescriptorSet set = descriptorSets_[currentFrame_ * materialCount + materialIndex];
        if (set != boundSet) {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipelineLayout_, 0, 1, &set, 1, &uniformOffset);
            boundSet = set;
        }

        MaterialPushConstants materialPushConstants{};
        materialPushConstants.nodeTransform = nodeTransform;
        materialPushConstants.baseColor = material.baseColor;
        const bool terrainMaterial = part.nodeId >= kTerrainChunkNodeBase &&
            part.nodeId < kPlayerSceneNodeId;
        const bool grassGroundMaterial = terrainMaterial || (item.grassLod == GrassLod::NearMesh &&
            shaders.shader(ShaderStage::Fragment) == "shader_grass.frag");
        const bool terrainMacro = terrainMaterial && terrainAlbedoLayers_ &&
            material.texturePath.filename() == "terrain_atlas.png" && materialTextures_[materialIndex];
        materialPushConstants.textureFlags =
            (materialTextures_[materialIndex] != nullptr ? 1u : 0u) |
            (normalTextures_[materialIndex] != nullptr ? 2u : 0u) |
            (specularTextures_[materialIndex] != nullptr ? 4u : 0u) |
            (terrainMaterial && terrainAlbedoLayers_ ? 8u : 0u) |
            (terrainMaterial && terrainSurfaceLayers_ ? 16u : 0u) |
            (terrainMacro ? 32u : 0u) |
            (grassGroundMaterial && grassGroundTexture_ && grassEnabled ? kGrassGroundTextureFlag : 0u);
        materialPushConstants.metallic = material.metallic;
        materialPushConstants.roughness = material.roughness;
        materialPushConstants.opacity = material.transitionOpacity && transitionSettings_.enabled
            ? material.opacity * transitionPlayer_.progress : material.opacity;
        const WaterShaderSettings& waterSettings = material.waterShaderSettings;
        materialPushConstants.waterFlow = glm::vec4(
            waterSettings.flowDirection,
            waterSettings.flowSpeed,
            waterSettings.flowStrength
        );
        materialPushConstants.waterSurface = glm::vec4(
            waterSettings.flowTurbulence,
            waterSettings.waveScale,
            waterSettings.waveHeight,
            0.0f
        );
        if (!isWaterShaderConfiguration(material.shaderConfiguration)) {
            // The default shader uses this slot for emission. Water shaders
            // keep the same 128-byte push-constant layout for compatibility
            // with Vulkan devices that only expose the guaranteed minimum.
            materialPushConstants.waterSurface = glm::vec4(
                material.emissionColor,
                material.emissionStrength
            );
        }
        if (material.customParameters) {
            materialPushConstants.waterFlow = material.shaderParameters0;
            materialPushConstants.waterSurface = material.shaderParameters1;
        }
        const auto& fragment = material.shaderConfiguration.shader(ShaderStage::Fragment);
        if (material.alphaCutoff >= 0.0f && (fragment.empty() || fragment == kDefaultFragmentShader)) {
            // The base shader reuses an unused component to stay within 128 bytes.
            materialPushConstants.textureFlags |= kAlphaTestFlag;
            materialPushConstants.waterFlow.x = material.alphaCutoff;
        }
        if (item.grassLod) {
            materialPushConstants.waterFlow = grassFadeDistances(*item.grassLod);
            materialPushConstants.metallic = ochreDirtRatio_;
            materialPushConstants.waterSurface.x = 1.0f;
            materialPushConstants.waterSurface.w = static_cast<float>(*item.grassLod);
            if (*item.grassLod == GrassLod::NearMesh) {
                // Near grass has no fade-in, leaving waterFlow.x free for the growth noise frequency.
                materialPushConstants.waterFlow.x = 1.0f / grassGrowth_.noiseSize;
                materialPushConstants.waterSurface.x = grassHeightScale_;
                materialPushConstants.waterSurface.y = glm::radians(grassGrowth_.minimumTilt);
                materialPushConstants.waterSurface.z = glm::radians(grassGrowth_.maximumTilt);
            }
        } else if (terrainMaterial && fragment == "shader_terrain.frag") {
            if (grassEnabled) materialPushConstants.waterFlow = grassFadeDistances(GrassLod::NearMesh);
            // Terrain and grass shaders use the metallic slot for the ochre mix.
            materialPushConstants.metallic = ochreDirtRatio_;
        }
        if (shadow) {
            if (isTransparentMaterial(material)) materialPushConstants.textureFlags |= kShadowBlendFlag;
            if (isWaterShaderConfiguration(shaders)) {
                materialPushConstants.textureFlags |= kShadowWaterFlag | kShadowBlendFlag;
            } else if (material.alphaCutoff >= 0.0f && !item.grassLod) {
                materialPushConstants.textureFlags |= kShadowCutoutFlag;
                materialPushConstants.waterFlow.x = material.alphaCutoff;
            }
        }
        vkCmdPushConstants(
            commandBuffer,
            pipelineLayout_,
            kMaterialPushConstantStageFlags,
            0,
            sizeof(MaterialPushConstants),
            &materialPushConstants
        );
        const auto drawFaces = [&](VkCullModeFlags cullMode) {
            const VkPipeline pipeline = shadow ? sunShadowPipelineForMaterial(material)
                : graphicsPipelineForMaterial(material, hdr, cullMode);
            if (pipeline == VK_NULL_HANDLE) return;
            if (pipeline != boundPipeline) {
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                boundPipeline = pipeline;
            }
            if (!item.grassLod) {
                vkCmdDrawIndexed(commandBuffer, part.indexCount, 1, part.firstIndex, 0, 0);
                return;
            }

            const VkBuffer instanceBuffer = grassFrameBuffers_[currentFrame_].instances->handle();
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &instanceBuffer, offsets);
            vkCmdBindIndexBuffer(commandBuffer, grassIndexBuffer_->handle(), 0, VK_INDEX_TYPE_UINT16);
            const std::uint32_t indexCount = *item.grassLod == GrassLod::NearMesh
                ? static_cast<std::uint32_t>(kGrassIndices.size()) : 6u;
            const auto clipFromGrass = shadow ? std::optional(sunShadowClipFromWorld_[cascade] * nodeTransform)
                : (clipFromScene ? std::optional(*clipFromScene * nodeTransform) : std::nullopt);
            std::uint32_t first = 0;
            std::uint32_t count = 0;
            for (const auto& range : grassCoverage_.ranges()) {
                if (range.lod != *item.grassLod) continue;
                // Larger blades can reach past the baked instance bounds.
                const float expansion = *item.grassLod == GrassLod::NearMesh
                    ? 3.0f * std::max(0.0f, grassHeightScale_ - 1.0f) : 0.0f;
                if (clipFromGrass && isOutsideVulkanFrustum(*clipFromGrass,
                    range.minimum - glm::vec3(expansion),
                    range.maximum + glm::vec3(expansion))) continue;
                if (count && first + count != range.firstInstance) {
                    vkCmdDrawIndexed(commandBuffer, indexCount, count, 0, 0, first);
                    count = 0;
                }
                if (!count) first = range.firstInstance;
                count += range.instanceCount;
            }
            if (count) vkCmdDrawIndexed(commandBuffer, indexCount, count, 0, 0, first);
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);
            vkCmdBindIndexBuffer(commandBuffer, indexBuffer_->handle(), 0, VK_INDEX_TYPE_UINT32);
        };
        if (shadow) {
            drawFaces(VK_CULL_MODE_NONE);
        } else if (material.doubleSided && material.drawBackFacesFirst && isTransparentMaterial(material)) {
            drawFaces(VK_CULL_MODE_FRONT_BIT);
            drawFaces(VK_CULL_MODE_BACK_BIT);
        } else {
            drawFaces(material.doubleSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT);
        }
    };

    const bool drawDaylight = skyboxEnabled_ &&
        skyboxTexture_ != nullptr && currentFrame_ < skyboxDescriptorSets_.size();
    // Preserve the draw order when an opaque material deliberately skips depth writes.
    const bool skyBeforeOpaque = drawDaylight && std::any_of(opaque.begin(), opaque.end(),
        [&](const DrawItem& item) { return !meshData_.materials[item.materialIndex].depthWrite; });
    const auto drawSky = [&](bool hdr) {
        vkCmdBindPipeline(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            hdr ? hdrSkyboxPipeline_ : skyboxPipeline_
        );
        const VkDescriptorSet& skyboxDescriptorSet = skyboxDescriptorSets_[currentFrame_];
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout_,
            0,
            1,
            &skyboxDescriptorSet,
            1,
            &uniformOffset
        );
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        boundPipeline = VK_NULL_HANDLE;
        boundSet = VK_NULL_HANDLE;
    };

    const auto drawOpaque = [&](bool hdr) {
        if (skyBeforeOpaque) drawSky(hdr);
        boundPipeline = VK_NULL_HANDLE;
        boundSet = VK_NULL_HANDLE;
        for (const DrawItem& item : opaque) drawPart(item, hdr);
        // Fill the remaining clear-depth pixels before capturing the scene for water.
        if (drawDaylight && !skyBeforeOpaque) drawSky(hdr);
    };

    const auto drawParticles = [&](bool hdr, bool shadow = false) {
        if (!particleDrawRanges_.empty()) {
            const VkBuffer particleBuffer = particleVertexBuffers_[currentFrame_]->handle();
            const VkDeviceSize particleOffset = 0;
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &particleBuffer, &particleOffset);
            for (const ParticleDrawRange& range : particleDrawRanges_) {
                VkDescriptorSet particleDescriptorSet;
                if (range.nodeId) {
                    const auto descriptorIt = particleDescriptorSets_.find(*range.nodeId);
                    if (descriptorIt == particleDescriptorSets_.end()) continue;
                    particleDescriptorSet = descriptorIt->second[currentFrame_];
                } else {
                    if (shadow) continue;
                    particleDescriptorSet = descriptorSets_[currentFrame_ * materialCount];
                }
                const VkPipeline particlePipeline = shadow ? sunShadowParticlePipeline_
                    : (range.blendMode == ParticleSystemSettings::BlendMode::Additive
                        ? (hdr ? hdrParticleAdditivePipeline_ : particleAdditivePipeline_)
                        : (hdr ? hdrParticleAlphaPipeline_ : particleAlphaPipeline_));
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, particlePipeline);
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipelineLayout_,
                    0,
                    1,
                    &particleDescriptorSet,
                    1,
                    &uniformOffset
                );
                MaterialPushConstants particlePushConstants{};
                particlePushConstants.textureFlags = range.nodeId && particleTextures_.contains(*range.nodeId) ? 1u : 0u;
                vkCmdPushConstants(
                    commandBuffer,
                    pipelineLayout_,
                    kMaterialPushConstantStageFlags,
                    0,
                    sizeof(MaterialPushConstants),
                    &particlePushConstants
                );
                vkCmdDraw(commandBuffer, range.vertexCount, 1, range.firstVertex, 0);
            }
        }
    };

    if (sunShadowRenderPass_) {
        const auto atlasSize = 2 * sunShadowTileSize_;
        VkClearValue clearDepth{};
        clearDepth.depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        pass.renderPass = sunShadowRenderPass_;
        pass.framebuffer = sunShadowTargets_[currentFrame_].framebuffer;
        pass.renderArea.extent = {atlasSize, atlasSize};
        pass.clearValueCount = 1;
        pass.pClearValues = &clearDepth;
        vkCmdBeginRenderPass(commandBuffer, &pass, VK_SUBPASS_CONTENTS_INLINE);
        if (sunShadowsActive_) {
            for (std::uint32_t i = 0; i < kSunShadowCascadeCount; ++i) {
                const auto x = (i % 2) * sunShadowTileSize_;
                const auto y = (i / 2) * sunShadowTileSize_;
                const float tile = static_cast<float>(sunShadowTileSize_);
                const VkViewport shadowViewport{static_cast<float>(x), static_cast<float>(y), tile, tile, 0.0f, 1.0f};
                const VkRect2D shadowScissor{{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)},
                                            {sunShadowTileSize_, sunShadowTileSize_}};
                vkCmdSetViewport(commandBuffer, 0, 1, &shadowViewport);
                vkCmdSetScissor(commandBuffer, 0, 1, &shadowScissor);
                // Swap the camera UBO slice while keeping the material descriptors and live geometry.
                uniformOffset = uniformSliceStride_ * (i + 1);
                boundPipeline = VK_NULL_HANDLE;
                boundSet = VK_NULL_HANDLE;
                vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);
                for (const DrawItem& item : casters) drawPart(item, false, static_cast<int>(i));
                drawParticles(false, true);
            }
        }
        vkCmdEndRenderPass(commandBuffer);
        uniformOffset = 0;
    }
#ifndef VULKCORE_OHOS
    if (resourcePreview_.draw) recordResourcePreview(commandBuffer);
#endif
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer_->handle(), 0, VK_INDEX_TYPE_UINT32);

    const SceneRenderTarget& sceneTarget = sceneRenderTargets_[currentFrame_];
    const bool hdr = hdrComposition_ != nullptr;
    const auto hdrSettings = hdr ? realtimeHdrSettings(false) : hdrSettings_;
    if (!hdr || needsOpaqueSnapshot) {
        VkRenderPassBeginInfo sceneRenderPassInfo{};
        sceneRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        sceneRenderPassInfo.renderPass = sceneRenderPass_;
        sceneRenderPassInfo.framebuffer = sceneTarget.framebuffer;
        sceneRenderPassInfo.renderArea.offset = {0, 0};
        sceneRenderPassInfo.renderArea.extent = extent;
        const bool isTornadoPreview = currentScenePath_.filename() == "tornado.scene";
        const glm::vec3 background = hdr ? hdrSettings.clearColor
            : (isTornadoPreview ? glm::vec3(0.0015f, 0.0025f, 0.0100f)
                                : glm::vec3(0.015f, 0.02f, 0.03f));
        std::array<VkClearValue, 2> sceneClearValues{};
        sceneClearValues[0].color = {{background.r, background.g, background.b, 1.0f}};
        sceneClearValues[1].depthStencil = {1.0f, 0};
        sceneRenderPassInfo.clearValueCount = static_cast<uint32_t>(sceneClearValues.size());
        sceneRenderPassInfo.pClearValues = sceneClearValues.data();
        vkCmdBeginRenderPass(commandBuffer, &sceneRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        bindSceneViewport();

        drawOpaque(false);
        vkCmdEndRenderPass(commandBuffer);
    }

    if (hdr) {
        if (needsOpaqueSnapshot) {
            // Expose the opaque snapshot only to materials that request it.
            VkImageMemoryBarrier depth{};
            depth.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            depth.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            depth.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            depth.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depth.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            depth.srcQueueFamilyIndex = depth.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            depth.image = sceneTarget.depthImage;
            depth.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            VkImageMemoryBarrier color = depth;
            color.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            color.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            color.image = sceneTarget.colorImage;
            color.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            const std::array barriers{color, depth};
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, uint32_t(barriers.size()), barriers.data());
        }
        hdrComposition_->beginGeometry(commandBuffer, currentFrame_, hdrSettings.clearColor);
        bindSceneViewport();
        drawOpaque(true);
        for (const DrawItem& item : transparent) drawPart(item, true);
        drawParticles(true);
        drawRealtimeEffects(commandBuffer, true);
        vkCmdEndRenderPass(commandBuffer);
        const auto postSource = drawRealtimePost(commandBuffer, 0, false);
        hdrComposition_->beginPresentation(commandBuffer, currentFrame_,
            swapChain_->framebuffers()[imageIndex], hdrSettings, postSource,
            hasActiveRealtimePost(true) ? HdrComposition::PostDraw([&](unsigned source) {
                return drawRealtimePost(commandBuffer, source, true);
            }) : HdrComposition::PostDraw{});
    } else {
        recordLegacySceneTransfer(commandBuffer, imageIndex);
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass_;
        renderPassInfo.framebuffer = swapChain_->framebuffers()[imageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = extent;
        const std::array<VkClearValue, 2> clearValues = {{
            {{{0.015f, 0.02f, 0.03f, 1.0f}}},
            {{{1.0f, 0}}}
        }};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();
        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        bindSceneViewport();

        // The demo is a regular full-screen pass over the off-screen scene. It
        // uses the same descriptor set and push-constant ABI as material shaders,
        // so no special render target or model-dependent path is introduced.
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, postProcessPipeline_);
        const VkDescriptorSet& postProcessDescriptorSet =
            descriptorSets_[static_cast<std::size_t>(currentFrame_) * materialCount];
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout_,
            0,
            1,
            &postProcessDescriptorSet,
            1,
            &uniformOffset
        );
        MaterialPushConstants postProcessConstants{};
        postProcessConstants.textureFlags = postProcessEffect_;
        postProcessConstants.metallic = postProcessStrength_;
        postProcessConstants.roughness = postProcessParameter_;
        postProcessConstants.opacity = postProcessSplitView_ ? 1.0f : 0.0f;
        postProcessConstants.waterFlow = glm::vec4(postProcessSplitPosition_, 0.0f, 0.0f, 0.0f);
        vkCmdPushConstants(
            commandBuffer,
            pipelineLayout_,
            kMaterialPushConstantStageFlags,
            0,
            sizeof(MaterialPushConstants),
            &postProcessConstants
        );
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);

        boundPipeline = VK_NULL_HANDLE;
        boundSet = VK_NULL_HANDLE;
        for (const DrawItem& item : transparent) drawPart(item, false);
        drawParticles(false);
        drawRealtimeEffects(commandBuffer, false);
    }

    if (!hdr && mode_ == Mode::ModelViewer && selectedModelNode_ && selectedModelNode_->hasBounds) {
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
            1,
            &uniformOffset
        );
        MaterialPushConstants boundingBoxPushConstants{};
        vkCmdPushConstants(
            commandBuffer,
            pipelineLayout_,
            kMaterialPushConstantStageFlags,
            0,
            sizeof(MaterialPushConstants),
            &boundingBoxPushConstants
        );
        vkCmdDraw(commandBuffer, static_cast<uint32_t>(boundingBoxVertices_.size()), 1, 0, 0);
    }

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

    vkCmdEndRenderPass(commandBuffer);
    if (captureScreenshot) {
        recordScreenshotCopy(commandBuffer, imageIndex);
    }
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }
}

void Renderer::captureTransitionFrame(float progress, float time, const std::filesystem::path& output) {
    fixedFrameOutput_ = output;
    ImGui::GetIO().IniFilename = nullptr;
    if (!swapChain_->supportsTransferSource()) {
        throw std::runtime_error("capture requires swap-chain image readback");
    }
    transitionPlayer_.seek(progress);
    transitionPlayer_.flowTime = time;
    if (!drawFrame()) throw std::runtime_error("failed to present the captured frame");
    if (terrainDrawCandidates_ != 0) {
        std::cout << "Terrain chunks queued: " << terrainDrawCandidates_ - terrainDrawCulled_
                  << '/' << terrainDrawCandidates_ << " (" << terrainDrawCulled_ << " culled)\n";
    }
}

bool Renderer::drawFrame() {
    if (mode_ == Mode::ModelViewer) loadModelViewerDrop();
#ifndef VULKCORE_OHOS
    if (const auto node = std::exchange(pendingSceneNodeRemoval_, std::nullopt)) {
        if (removeSceneNodeById(*node)) {
            editorSelection_ = EditorSelection::None;
            editorPart_.reset();
            editorDirty_ = true;
            sceneStatusFailed_ = false;
            sceneStatus_ = "Removed object from the scene.";
        }
    }
    if (!resourceBrowser_.root.empty()) {
        processResourceTransfers();
        processResourceTrash();
        processSceneModelDrop();
        if (toolUiVisible_ && resourceBrowser_.open) loadResourcePreview();
        resourceBrowser_.dropZones.clear();
        resourcePreview_.draw = false;
    }
    if (editorShaderApplyRequested_) {
        editorShaderApplyRequested_ = false;
        applyEditorShader();
    }
#endif
    if (pendingSceneChange_ != EditorSceneChange::None && (!editorDirty_ || editorSceneChangeConfirmed_)) {
        const auto action = std::exchange(pendingSceneChange_, EditorSceneChange::None);
        const auto path = std::exchange(pendingSceneOpen_, {});
        editorSceneChangeConfirmed_ = false;
        const auto before = currentScenePath_;
        const bool changed = action == EditorSceneChange::Create ? createNewEmptyScene() : loadSceneResource(path);
        if (changed || currentScenePath_ != before) editorDirty_ = false;
    }
#ifdef VULKCORE_OHOS
    // Update the swapchain, scene targets and projection before drawing into a
    // changed XComponent, rather than presenting one frame at the old ratio.
    if (framebufferResized_) {
        recreateSwapChain();
        framebufferResized_ = false;
    }
#endif
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
        return false;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    const bool screenshotPressed = keyPressed(context_->window(), Key::Screenshot);
    const bool screenshotRequested = !fixedFrameOutput_.empty() ||
        std::exchange(screenshotFromToolbar_, false) || (screenshotPressed && !screenshotKeyDown_);
    screenshotKeyDown_ = screenshotPressed;
    const bool captureScreenshot = screenshotRequested && swapChain_->supportsTransferSource();
    if (screenshotRequested && !swapChain_->supportsTransferSource()) {
        screenshotStatus_ = "Screenshot unavailable: swap chain does not support readback";
        std::cerr << "Warning: " << screenshotStatus_ << '\n';
    }
    if (captureScreenshot) {
        ensureScreenshotBuffer();
    }

    const bool viewportHovered = buildImGuiFrame();
    const auto now = std::chrono::steady_clock::now();
    const float elapsedSeconds = std::chrono::duration<float>(now - startTime_).count();
    const float particleDeltaSeconds = std::clamp(
        elapsedSeconds - lastFrameElapsedSeconds_,
        0.0f,
        0.1f
    );
#ifndef VULKCORE_OHOS
    if ((playerController_.active() || transitionSettings_.enabled) &&
        (!toolUiVisible_ || roadEditorActive())) {
        restoreSceneCamera();
    }
#endif
    updatePlayerController(particleDeltaSeconds, viewportHovered);
#ifndef VULKCORE_OHOS
    updateRoadEditorCamera(particleDeltaSeconds, viewportHovered);
#endif
    updateTransition(std::max(0.0f, elapsedSeconds - lastFrameElapsedSeconds_));
    updateCameraBasis();
    glm::vec3 editMovement(0.0f);
#ifndef VULKCORE_OHOS
    editMovement = updateGameEditorSelection(particleDeltaSeconds, viewportHovered);
#endif
    updateSceneNodeTransforms(elapsedSeconds, particleDeltaSeconds, editMovement);
#ifndef VULKCORE_OHOS
    if (resourcePreview_.draw) {
        updateSkeletalMesh(resourcePreview_.mesh, particleDeltaSeconds, kMaxFramesInFlight,
            [this](std::size_t start, std::size_t count) {
                resourcePreview_.vertices[currentFrame_]->upload(resourcePreview_.mesh.vertices.data() + start,
                    sizeof(Vertex) * count, sizeof(Vertex) * start);
            });
    }
#endif
    lastFrameElapsedSeconds_ = elapsedSeconds;
    updateRealtimeEffects(particleDeltaSeconds);
    if (mode_ == Mode::ModelViewer) updateBoundingBoxVertices();

    if (vkResetFences(context_->device(), 1, &inFlightFences_[currentFrame_]) != VK_SUCCESS) {
        throw std::runtime_error("failed to reset in-flight frame fence!");
    }
    const auto clipFromScene = updateUniformBuffer();
#ifndef VULKCORE_OHOS
    if (clipFromScene) {
        if (roadEditorActive()) updateRoadBrush(*clipFromScene, viewportHovered);
        else {
            updatePlayerMoveClick(*clipFromScene, viewportHovered);
            drawGameSelectionOutline(*clipFromScene);
        }
    }
#endif
    updateParticleSystems(particleDeltaSeconds, cameraRight_, cameraUp_);
    updateRoadIndex();
    ImGui::Render();
    updateGrassCoverage();
    updateParticleBuffer();
    if (mode_ == Mode::ModelViewer) updateBoundingBoxBuffer();
    if (vkResetCommandBuffer(commandBuffers_[currentFrame_], 0) != VK_SUCCESS) {
        throw std::runtime_error("failed to reset command buffer!");
    }
    recordCommandBuffer(commandBuffers_[currentFrame_], imageIndex, captureScreenshot, clipFromScene);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    const VkSemaphore waitSemaphores[] = {imageAvailableSemaphores_[currentFrame_]};
    const VkPipelineStageFlags waitStages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
    };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers_[currentFrame_];
    const VkSemaphore signalSemaphores[] = {renderFinishedSemaphores_[imageIndex]};
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
    if (captureScreenshot) {
        if (vkWaitForFences(
                context_->device(),
                1,
                &inFlightFences_[currentFrame_],
                VK_TRUE,
                UINT64_MAX
            ) != VK_SUCCESS) {
            throw std::runtime_error("failed to wait for screenshot frame fence!");
        }
        try {
            saveScreenshot(fixedFrameOutput_);
        } catch (const std::exception& error) {
            if (!fixedFrameOutput_.empty()) throw;
            screenshotStatus_ = "Screenshot failed: " + std::string(error.what());
            std::cerr << "Warning: " << screenshotStatus_ << '\n';
        }
    }
    const bool presented = presentResult == VK_SUCCESS || presentResult == VK_SUBOPTIMAL_KHR;
    if (!presented && presentResult != VK_ERROR_OUT_OF_DATE_KHR) {
        throw std::runtime_error("failed to present swap chain image!");
    }
    if (presentResult != VK_SUCCESS || framebufferResized_) {
        framebufferResized_ = false;
        recreateSwapChain();
    }

    currentFrame_ = (currentFrame_ + 1) % kMaxFramesInFlight;
    return presented;
}

void Renderer::recreateSwapChain() {
    int width = 0;
    int height = 0;
    framebufferSize(context_->window(), &width, &height);
#ifdef VULKCORE_OHOS
    // Surface lifecycle is owned by XComponent; never block its callback thread.
    if (width == 0 || height == 0) return;
#else
    while (width == 0 || height == 0) {
        if (windowShouldClose(context_->window())) {
            return;
        }
        waitWindowEvents();
        framebufferSize(context_->window(), &width, &height);
    }
#endif

    context_->waitIdle();
#ifndef VULKCORE_OHOS
    destroyResourcePreviewTargets();
#endif
    invalidateRealtimeEffectPipelines();
    destroySceneRenderResources();
    hdrComposition_.reset();
    swapChain_->recreate(renderPass_);
    recreatePresentSemaphores();
    createSceneRenderResources();
    configureHdrComposition();
    updateSceneTextureDescriptors();
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



} // namespace VulkCore
