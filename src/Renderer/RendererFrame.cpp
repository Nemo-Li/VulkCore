#include "RendererImplementation.hpp"

namespace VulkCore {

void Renderer::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    const VkExtent2D extent = swapChain_->extent();
    const uint32_t materialCount = static_cast<uint32_t>(meshData_.materials.size());

    VkViewport viewport{};
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.extent = extent;

    const VkBuffer buffers[] = {vertexBuffers_[currentFrame_]->handle()};
    const VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer_->handle(), 0, VK_INDEX_TYPE_UINT32);

    const auto bindViewport = [&]() {
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    };

    const auto drawPart = [&](const MeshPart& part) {
        if (!isSceneNodeRenderEnabled(part.nodeId)) {
            return;
        }
        const std::uint32_t materialIndex = materialIndexForPart(part);
        if (materialIndex >= materialCount || materialIndex >= materialTextures_.size()) {
            return;
        }
        const MaterialData& material = meshData_.materials[materialIndex];
        const VkPipeline materialPipeline = graphicsPipelineForMaterial(material);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, materialPipeline);

        const std::size_t descriptorSetIndex =
            static_cast<std::size_t>(currentFrame_) * materialCount + materialIndex;
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
        materialPushConstants.nodeTransform = transformForSceneNode(part.nodeId);
        materialPushConstants.baseColor = material.baseColor;
        materialPushConstants.textureFlags =
            (materialTextures_[materialIndex] != nullptr ? 1u : 0u) |
            (normalTextures_[materialIndex] != nullptr ? 2u : 0u) |
            (specularTextures_[materialIndex] != nullptr ? 4u : 0u);
        materialPushConstants.metallic = material.metallic;
        materialPushConstants.roughness = material.roughness;
        materialPushConstants.opacity = material.opacity;
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
        vkCmdPushConstants(
            commandBuffer,
            pipelineLayout_,
            kMaterialPushConstantStageFlags,
            0,
            sizeof(MaterialPushConstants),
            &materialPushConstants
        );
        vkCmdDrawIndexed(commandBuffer, part.indexCount, 1, part.firstIndex, 0, 0);
    };

    const SceneRenderTarget& sceneTarget = sceneRenderTargets_[currentFrame_];
    VkRenderPassBeginInfo sceneRenderPassInfo{};
    sceneRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    sceneRenderPassInfo.renderPass = sceneRenderPass_;
    sceneRenderPassInfo.framebuffer = sceneTarget.framebuffer;
    sceneRenderPassInfo.renderArea.offset = {0, 0};
    sceneRenderPassInfo.renderArea.extent = extent;
    const std::array<VkClearValue, 2> sceneClearValues = {{
        {{{0.015f, 0.02f, 0.03f, 1.0f}}},
        {{{1.0f, 0}}}
    }};
    sceneRenderPassInfo.clearValueCount = static_cast<uint32_t>(sceneClearValues.size());
    sceneRenderPassInfo.pClearValues = sceneClearValues.data();
    vkCmdBeginRenderPass(commandBuffer, &sceneRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    bindViewport();

    // Render the opaque scene into a sampled color/depth pair first. Water
    // parts are intentionally excluded: they need this color/depth as their
    // reflection and refraction source in the second pass.
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_);
    for (const MeshPart& part : meshData_.parts) {
        const std::uint32_t materialIndex = materialIndexForPart(part);
        if (materialIndex >= materialCount || part.indexCount == 0) {
            continue;
        }
        if (!isSceneNodeRenderEnabled(part.nodeId)) {
            continue;
        }
        const MaterialData& material = meshData_.materials[materialIndex];
        const ModelNode* node = findSceneNodeById(meshData_, part.nodeId);
        const bool isWater = node != nullptr &&
            isWaterShaderConfiguration(meshData_.materials[materialIndex].shaderConfiguration);
        if (!isWater && !isTransparentMaterial(material)) {
            drawPart(part);
        }
    }
    vkCmdEndRenderPass(commandBuffer);

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
    swapDepthToTransfer.image = swapChain_->depthImage();
    swapDepthToTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    swapDepthToTransfer.subresourceRange.levelCount = 1;
    swapDepthToTransfer.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
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
        swapChain_->depthImage(),
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
    swapColorToAttachment.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
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
    swapDepthToAttachment.image = swapChain_->depthImage();
    swapDepthToAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    swapDepthToAttachment.subresourceRange.levelCount = 1;
    swapDepthToAttachment.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
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
    bindViewport();

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
        0,
        nullptr
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

    // The opaque scene is already in the swap-chain color attachment. Only
    // water is rendered here so it can blend over and sample that scene.
    for (const MeshPart& part : meshData_.parts) {
        const std::uint32_t materialIndex = materialIndexForPart(part);
        if (materialIndex >= materialCount || part.indexCount == 0) {
            continue;
        }
        if (!isSceneNodeRenderEnabled(part.nodeId)) {
            continue;
        }
        const MaterialData& material = meshData_.materials[materialIndex];
        const ModelNode* node = findSceneNodeById(meshData_, part.nodeId);
        const bool isWater = node != nullptr &&
            isWaterShaderConfiguration(meshData_.materials[materialIndex].shaderConfiguration);
        if (isWater || isTransparentMaterial(material)) {
            drawPart(part);
        }
    }

    if (!particleDrawRanges_.empty()) {
        const VkBuffer particleBuffer = particleVertexBuffers_[currentFrame_]->handle();
        const VkDeviceSize particleOffset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &particleBuffer, &particleOffset);
        for (const ParticleDrawRange& range : particleDrawRanges_) {
            const auto descriptorIt = particleDescriptorSets_.find(range.nodeId);
            if (descriptorIt == particleDescriptorSets_.end()) {
                continue;
            }
            const VkPipeline particlePipeline =
                range.blendMode == ParticleSystemSettings::BlendMode::Additive
                    ? particleAdditivePipeline_
                    : particleAlphaPipeline_;
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, particlePipeline);
            const VkDescriptorSet& particleDescriptorSet =
                descriptorIt->second[currentFrame_];
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelineLayout_,
                0,
                1,
                &particleDescriptorSet,
                0,
                nullptr
            );
            MaterialPushConstants particlePushConstants{};
            particlePushConstants.textureFlags = particleTextures_.contains(range.nodeId)
                ? 1u
                : 0u;
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
            kMaterialPushConstantStageFlags,
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
    const auto now = std::chrono::steady_clock::now();
    const float elapsedSeconds = std::chrono::duration<float>(now - startTime_).count();
    const float particleDeltaSeconds = std::clamp(
        elapsedSeconds - lastFrameElapsedSeconds_,
        0.0f,
        0.1f
    );
    updatePlayerController(particleDeltaSeconds);
    updateSceneNodeTransforms(elapsedSeconds, particleDeltaSeconds);
    lastFrameElapsedSeconds_ = elapsedSeconds;
    updateCameraBasis();
    updateParticleSystems(particleDeltaSeconds, cameraRight_, cameraUp_);
    updateBoundingBoxVertices();

    if (vkResetFences(context_->device(), 1, &inFlightFences_[currentFrame_]) != VK_SUCCESS) {
        throw std::runtime_error("failed to reset in-flight frame fence!");
    }
    updateUniformBuffer();
    updateParticleBuffer();
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
    destroySceneRenderResources();
    swapChain_->recreate(renderPass_);
    createSceneRenderResources();
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
