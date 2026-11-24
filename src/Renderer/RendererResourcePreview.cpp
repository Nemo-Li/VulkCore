#include "RendererImplementation.hpp"

#include "vulkcore/Utils/SbfresLoader.hpp"

namespace VulkCore {
namespace {

namespace fs = std::filesystem;
constexpr VkExtent2D kPreviewExtent{768, 480};
constexpr float kAspect = static_cast<float>(kPreviewExtent.width) / kPreviewExtent.height;
constexpr float kFov = 45.0f;
constexpr auto kHostMemory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
constexpr std::array<std::uint32_t, 4> kPreviewTextureBindings{1, 3, 4, kAlphaMaskBinding};

std::string filename(const fs::path& path) {
    const auto name = path.filename().u8string();
    return {reinterpret_cast<const char*>(name.data()), name.size()};
}

std::string lowerStem(const fs::path& path) {
    auto name = filename(path.stem());
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
    return name;
}

std::string animationModelStem(const fs::path& path) {
    const auto name = lowerStem(path);
    for (const std::string_view suffix : {"_animation", ".animation", "-animation", "_anim", ".anim", "-anim"}) {
        if (name.ends_with(suffix)) return name.substr(0, name.size() - suffix.size());
    }
    return {};
}

glm::vec3 fittedCamera() {
    const float distance = 1.12f / std::sin(glm::radians(kFov * 0.5f));
    return distance * glm::normalize(glm::vec3(0.62f, 0.28f, 1.0f));
}

void orbitPreview(glm::vec3& camera, ImVec2 delta) {
    constexpr float sensitivity = 0.005f;
    const float radius = glm::length(camera);
    const float yaw = std::atan2(camera.x, camera.z) - delta.x * sensitivity;
    const float pitch = std::clamp(std::asin(std::clamp(camera.y / radius, -1.0f, 1.0f)) +
        delta.y * sensitivity, glm::radians(-85.0f), glm::radians(85.0f));
    camera = radius * glm::vec3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
}

} // namespace

void Renderer::loadResourcePreview() {
    auto& preview = resourcePreview_;
    if (preview.reload) {
        if (preview.indices) context_->waitIdle();
        destroyResourcePreviewBuffers();
        preview.mesh = {};
        preview.model.clear();
        preview.animation.clear();
        preview.modelChoices.clear();
        preview.message.clear();
        preview.targetsFailed = false;
        preview.reload = false;
        const auto& selected = resourceBrowser_.selected;
        if (selected.empty()) return;
        if (!isBfresPath(selected)) {
            preview.message = "Preview is available for SBFRES and BFRES models and skeletal animations.";
            return;
        }
        if (isBfresTextureSidecarPath(selected)) {
            preview.message = "Texture companion. Select its model to preview it.";
            return;
        }
        try {
            const auto content = inspectBfresContent(selected);
            if (content == BfresContent::Texture) {
                preview.message = "Open this texture package in SBFRES Viewer to browse its textures.";
                return;
            }
            if (content == BfresContent::Other) {
                preview.message = "This package has no model or skeletal animation to preview.";
                return;
            }
            if (content == BfresContent::Model) {
                preview.model = selected;
                for (const auto& entry : resourceBrowser_.entries) {
                    if (!entry.directory && isBfresPath(entry.path) &&
                        animationModelStem(entry.path) == lowerStem(selected)) {
                        preview.animation = entry.path;
                        break;
                    }
                }
                if (!preview.animation.empty()) {
                    try {
                        preview.mesh = loadSbfresMesh(preview.model, {}, preview.animation);
                    } catch (const std::exception& error) {
                        // A broken companion should not prevent viewing the model.
                        preview.message = error.what();
                        preview.animation.clear();
                    }
                }
                if (preview.mesh.indices.empty()) preview.mesh = loadSbfresMesh(preview.model, {}, {}, true);
            } else {
                preview.animation = selected;
                const auto expected = animationModelStem(selected);
                for (const auto& entry : resourceBrowser_.entries) {
                    if (entry.directory || !isBfresPath(entry.path) || entry.path == selected ||
                        isBfresTextureSidecarPath(entry.path) || !animationModelStem(entry.path).empty()) continue;
                    preview.modelChoices.push_back(entry.path);
                    if (lowerStem(entry.path) == expected) preview.model = entry.path;
                }
                if (std::find(preview.modelChoices.begin(), preview.modelChoices.end(), preview.requestedModel)
                    != preview.modelChoices.end()) {
                    preview.model = preview.requestedModel;
                }
                if (preview.model.empty() && preview.modelChoices.size() == 1) preview.model = preview.modelChoices.front();
                if (preview.model.empty()) {
                    preview.message = preview.modelChoices.empty()
                        ? "Put a compatible model in the same folder to preview this animation."
                        : "Choose a model to preview this animation.";
                    return;
                }
                preview.mesh = loadSbfresMesh(preview.model, {}, preview.animation);
            }
            preview.modelIndex = defaultPreviewModel(preview.mesh, true);
            preview.camera = fittedCamera();
            createResourcePreviewBuffers();
        } catch (const std::exception& error) {
            destroyResourcePreviewBuffers();
            preview.mesh = {};
            preview.message = error.what();
        }
    }
    if (preview.indices && !preview.targetsFailed && !preview.images.front()) {
        try {
            createResourcePreviewTargets();
        } catch (const std::exception& error) {
            destroyResourcePreviewTargets();
            preview.targetsFailed = true;
            preview.message = error.what();
        }
    }
}

void Renderer::drawResourcePreview() {
    auto& preview = resourcePreview_;
    const auto& selected = resourceBrowser_.selected;
    if (selected.empty()) {
        ImGui::TextDisabled("Select an SBFRES or BFRES file to preview");
        return;
    }
    if (preview.reload) {
        ImGui::TextDisabled("Loading preview...");
        return;
    }
    const bool hasImage = preview.indices && preview.images[currentFrame_];
    if (hasImage) {
        const float width = ImGui::GetContentRegionAvail().x;
        const float height = ImGui::GetContentRegionAvail().y;
        const float areaWidth = width * (width < 440.0f ? 0.52f : 0.60f);
        const float imageWidth = std::min(areaWidth, height * kAspect);
        if (imageWidth < 1.0f) return;
        const ImVec2 size(imageWidth, imageWidth / kAspect);
        const auto position = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("Orbit model", size);
        ImGui::GetWindowDrawList()->AddImage((ImTextureID)preview.images[currentFrame_], position,
            ImVec2(position.x + size.x, position.y + size.y));
        preview.draw = ImGui::IsItemVisible();
        const auto& io = ImGui::GetIO();
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            orbitPreview(preview.camera, io.MouseDelta);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
            const auto newDistance = std::clamp(glm::length(preview.camera) * std::exp(-0.16f * io.MouseWheel),
                0.2f, 60.0f);
            preview.camera = newDistance * glm::normalize(preview.camera);
        }
        ImGui::SameLine();
    }

    if (ImGui::BeginChild("Preview controls", ImVec2(0, 0))) {
        ImGui::TextWrapped("%s", filename(selected).c_str());
        if (!preview.modelChoices.empty()) {
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##Animation model", preview.model.empty()
                    ? "Choose model..." : filename(preview.model).c_str())) {
                for (const auto& model : preview.modelChoices) {
                    if (ImGui::Selectable(filename(model).c_str(), model == preview.model)) {
                        preview.requestedModel = model;
                        preview.reload = true;
                    }
                }
                ImGui::EndCombo();
            }
        }
        if (hasImage) {
            if (drawPreviewModelPicker(preview.mesh, preview.modelIndex)) preview.camera = fittedCamera();
            if (ImGui::Button("Fit view")) preview.camera = fittedCamera();
            ImGui::SetItemTooltip("Left-drag the preview to orbit, scroll to zoom");
            const auto nodeId = preview.mesh.hierarchy.children[preview.modelIndex].nodeId;
            for (auto& instance : preview.mesh.skeletalAnimationInstances) {
                if (instance.rootNodeId == nodeId && instance.animationCount > 0) {
                    ImGui::SeparatorText("Skeletal Animation");
                    ImGui::PushItemWidth(std::max(40.0f, ImGui::GetContentRegionAvail().x * 0.55f));
                    drawAnimationPlayback(preview.mesh, instance);
                    ImGui::PopItemWidth();
                    break;
                }
            }
        }
        if (!preview.message.empty()) ImGui::TextWrapped("%s", preview.message.c_str());
    }
    ImGui::EndChild();
}

void Renderer::createResourcePreviewBuffers() {
    auto& preview = resourcePreview_;
    const auto device = context_->device();
    const auto vertexBytes = sizeof(Vertex) * preview.mesh.vertices.size();
    const auto indexBytes = sizeof(std::uint32_t) * preview.mesh.indices.size();
    preview.indices = std::make_unique<Buffer>(*context_, indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, kHostMemory);
    preview.indices->upload(preview.mesh.indices.data(), indexBytes);
    for (std::size_t i = 0; i < kMaxFramesInFlight; ++i) {
        preview.vertices[i] = std::make_unique<Buffer>(*context_, vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, kHostMemory);
        preview.vertices[i]->upload(preview.mesh.vertices.data(), vertexBytes);
        preview.uniforms[i] = std::make_unique<Buffer>(*context_, sizeof(UniformBufferObject),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, kHostMemory);
    }

    const auto materialCount = static_cast<std::uint32_t>(preview.mesh.materials.size());
    const auto setCount = materialCount * kMaxFramesInFlight;
    // Scene material resources may be rebuilt while the preview stays open.
    const std::array<VkDescriptorPoolSize, 3> sizes{{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, setCount},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaterialImageBindingCount * setCount},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, setCount},
    }};
    VkDescriptorPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = setCount;
    pool.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    pool.pPoolSizes = sizes.data();
    if (vkCreateDescriptorPool(device, &pool, nullptr, &preview.descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Could not create the preview descriptor pool.");
    }
    const std::vector<VkDescriptorSetLayout> layouts(setCount, descriptorSetLayout_);
    preview.descriptorSets.resize(setCount);
    VkDescriptorSetAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = preview.descriptorPool;
    allocate.descriptorSetCount = setCount;
    allocate.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(device, &allocate, preview.descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Could not allocate preview descriptors.");
    }

    const std::vector<std::uint8_t> white{255, 255, 255, 255};
    preview.fallback = std::make_unique<Texture>(*context_, white, 1, 1);
    preview.textures.resize(materialCount);
    for (std::uint32_t i = 0; i < materialCount; ++i) {
        const auto& material = preview.mesh.materials[i];
        const std::array source{&material.embeddedTexture, &material.embeddedNormalTexture,
            &material.embeddedSpecularTexture, &material.embeddedAlphaMask};
        std::array<VkDescriptorImageInfo, kPreviewTextureBindings.size()> images{};
        for (std::size_t j = 0; j < source.size(); ++j) {
            const auto& embedded = *source[j];
            if (!embedded.empty()) {
                preview.textures[i][j] = std::make_unique<Texture>(*context_, embedded.data,
                    embedded.width, embedded.height, false,
                    embedded.srgb ? TextureColorSpace::Srgb : TextureColorSpace::Linear);
            }
            const auto& texture = preview.textures[i][j] ? *preview.textures[i][j] : *preview.fallback;
            images[j] = {texture.sampler(), texture.imageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        }
        for (std::uint32_t frame = 0; frame < kMaxFramesInFlight; ++frame) {
            const auto set = preview.descriptorSets[frame * materialCount + i];
            const VkDescriptorBufferInfo uniform{preview.uniforms[frame]->handle(), 0, sizeof(UniformBufferObject)};
            const VkDescriptorImageInfo whiteDepth{preview.fallback->sampler(), preview.fallback->imageView(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            std::array<VkWriteDescriptorSet, 2 + kPreviewTextureBindings.size()> writes{};
            writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, nullptr, &uniform, nullptr};
            writes.back() = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, kSunShadowBinding, 0, 1,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &whiteDepth, nullptr, nullptr};
            for (std::size_t j = 0; j < images.size(); ++j) {
                writes[j + 1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, kPreviewTextureBindings[j],
                    0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &images[j], nullptr, nullptr};
            }
            vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }
}

void Renderer::destroyResourcePreviewBuffers() {
    auto& preview = resourcePreview_;
    if (preview.descriptorPool) vkDestroyDescriptorPool(context_->device(), preview.descriptorPool, nullptr);
    preview.descriptorPool = VK_NULL_HANDLE;
    preview.descriptorSets.clear();
    preview.textures.clear();
    preview.fallback.reset();
    preview.indices.reset();
    preview.vertices = {};
    preview.uniforms = {};
}

void Renderer::createResourcePreviewTargets() {
    auto& preview = resourcePreview_;
    for (std::size_t i = 0; i < kMaxFramesInFlight; ++i) {
        auto& target = preview.targets[i];
        createSceneRenderTarget(target, kPreviewExtent, false);
        preview.images[i] = ImGui_ImplVulkan_AddTexture(target.colorImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (!preview.images[i]) throw std::runtime_error("Could not register the preview image.");
    }
}

void Renderer::destroyResourcePreviewTargets() {
    auto& preview = resourcePreview_;
    for (std::size_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (preview.images[i]) ImGui_ImplVulkan_RemoveTexture(preview.images[i]);
        preview.images[i] = VK_NULL_HANDLE;
        destroySceneRenderTarget(preview.targets[i]);
    }
}

void Renderer::recordResourcePreview(VkCommandBuffer commandBuffer) {
    auto& preview = resourcePreview_;
    const auto& target = preview.targets[currentFrame_];
    const auto& model = preview.mesh.hierarchy.children[preview.modelIndex];
    const auto center = model.boundsMinimum * 0.5f + model.boundsMaximum * 0.5f;
    const auto scale = static_cast<float>(1.0 / std::max(1e-9, modelBoundsRadius(model)));
    UniformBufferObject uniform;
    uniform.model = glm::scale(glm::mat4(1.0f), glm::vec3(scale)) * glm::translate(glm::mat4(1.0f), -center);
    uniform.view = glm::lookAt(preview.camera, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    uniform.projection = glm::perspectiveRH_ZO(glm::radians(kFov), kAspect, 0.03f, 100.0f);
    uniform.projection[1][1] *= -1.0f;
    uniform.ambientLight = glm::vec4(0.45f, 0.47f, 0.48f, 1.0f);
    uniform.cameraPosition = glm::vec4(preview.camera, scale);
    preview.uniforms[currentFrame_]->upload(&uniform, sizeof(uniform));

    const std::array<VkClearValue, 2> clear{{{{{0.028f, 0.036f, 0.051f, 1.0f}}}, {{{1.0f, 0}}}}};
    VkRenderPassBeginInfo pass{};
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    pass.renderPass = sceneRenderPass_;
    pass.framebuffer = target.framebuffer;
    pass.renderArea.extent = kPreviewExtent;
    pass.clearValueCount = static_cast<std::uint32_t>(clear.size());
    pass.pClearValues = clear.data();
    vkCmdBeginRenderPass(commandBuffer, &pass, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport viewport{0, 0, static_cast<float>(kPreviewExtent.width), static_cast<float>(kPreviewExtent.height), 0, 1};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &pass.renderArea);
    const auto vertexBuffer = preview.vertices[currentFrame_]->handle();
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, preview.indices->handle(), 0, VK_INDEX_TYPE_UINT32);
    const auto drawNode = [&](const auto& self, const ModelNode& node) -> void {
        for (const auto partIndex : node.meshPartIndices) {
            const auto& part = preview.mesh.parts[partIndex];
            const auto& material = preview.mesh.materials[part.materialIndex];
            const auto& textures = preview.textures[part.materialIndex];
            const auto pipeline = graphicsPipelineForMaterial(material, false,
                material.doubleSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT);
            if (!pipeline) continue;
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            const auto set = preview.descriptorSets[currentFrame_ * preview.mesh.materials.size() + part.materialIndex];
            const std::uint32_t uniformOffset = 0;
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelineLayout_, 0, 1, &set, 1, &uniformOffset);
            MaterialPushConstants params{};
            params.baseColor = material.baseColor;
            params.metallic = material.metallic;
            params.roughness = material.roughness;
            params.opacity = material.opacity;
            params.waterSurface = glm::vec4(material.emissionColor, material.emissionStrength);
            params.textureFlags = (textures[0] ? 1u : 0u) | (textures[1] ? 2u : 0u) | (textures[2] ? 4u : 0u);
            if (material.alphaCutoff >= 0.0f) {
                params.textureFlags |= kAlphaTestFlag;
                params.waterFlow.x = material.alphaCutoff;
            }
            vkCmdPushConstants(commandBuffer, pipelineLayout_, kMaterialPushConstantStageFlags, 0, sizeof(params), &params);
            vkCmdDrawIndexed(commandBuffer, part.indexCount, 1, part.firstIndex, 0, 0);
        }
        for (const auto& child : node.children) self(self, child);
    };
    drawNode(drawNode, model);
    vkCmdEndRenderPass(commandBuffer);

    // The presentation pass samples this frame's preview through ImGui.
    VkImageMemoryBarrier ready{};
    ready.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ready.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    ready.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    ready.oldLayout = ready.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ready.srcQueueFamilyIndex = ready.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ready.image = target.colorImage;
    ready.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &ready);
}

} // namespace VulkCore
