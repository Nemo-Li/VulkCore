#include "RendererImplementation.hpp"

#include "vulkcore/Terrain/Terrain.hpp"

namespace VulkCore {

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

void Renderer::drawShaderDemoControls(bool& shouldSaveSettings) {
    ImGui::SeparatorText("Shader Demo Lab");
    ImGui::TextWrapped(
        "Reference-aligned material and screen-space techniques; all demos use the shared renderer ABI."
    );

    const char* postPreview = postProcessEffect_ < kPostProcessDemos.size()
        ? kPostProcessDemos[postProcessEffect_].name
        : kPostProcessDemos[0].name;
    if (ImGui::BeginCombo("Screen-space demo", postPreview)) {
        for (std::size_t index = 0; index < kPostProcessDemos.size(); ++index) {
            const bool selected = postProcessEffect_ == index;
            if (ImGui::Selectable(kPostProcessDemos[index].name, selected)) {
                postProcessEffect_ = static_cast<std::uint32_t>(index);
                postProcessStrength_ = kPostProcessDemos[index].defaultStrength;
                postProcessParameter_ = kPostProcessDemos[index].defaultParameter;
                shouldSaveSettings = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::TextWrapped("%s", kPostProcessDemos[postProcessEffect_].description);
    ImGui::SliderFloat("Demo strength", &postProcessStrength_, 0.0f, 2.0f, "%.2f");
    shouldSaveSettings |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Demo parameter", &postProcessParameter_, 0.0f, 1.0f, "%.2f");
    shouldSaveSettings |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::Checkbox("Split view (original / effect)", &postProcessSplitView_);
    shouldSaveSettings |= ImGui::IsItemDeactivatedAfterEdit();
    if (postProcessSplitView_) {
        ImGui::SliderFloat("Split position", &postProcessSplitPosition_, 0.1f, 0.9f, "%.2f");
        shouldSaveSettings |= ImGui::IsItemDeactivatedAfterEdit();
    }
    if (ImGui::Button("Reset screen demo")) {
        postProcessEffect_ = 0;
        postProcessStrength_ = kPostProcessDemos[0].defaultStrength;
        postProcessParameter_ = kPostProcessDemos[0].defaultParameter;
        postProcessSplitView_ = false;
        postProcessSplitPosition_ = 0.5f;
        shouldSaveSettings = true;
    }

    ImGui::SeparatorText("Surface shader preset");
    if (selectedModelNode_ == nullptr ||
        selectedModelNode_->objectType != SceneObjectType::ImportedMesh) {
        ImGui::TextWrapped("Select an imported mesh node to apply a material shader preset.");
        return;
    }
    const ModelNode& node = *selectedModelNode_;
    std::int32_t materialIndex = node.materialOverride;
    if (materialIndex < 0) {
        materialIndex = inheritedMaterialOverrideForSceneNode(node.nodeId);
    }
    if (materialIndex < 0 && !node.meshPartIndices.empty() &&
        node.meshPartIndices.front() < meshData_.parts.size()) {
        materialIndex = static_cast<std::int32_t>(
            meshData_.parts[node.meshPartIndices.front()].materialIndex
        );
    }
    if (materialIndex < 0 || static_cast<std::size_t>(materialIndex) >= meshData_.materials.size()) {
        ImGui::TextWrapped("The selected node has no material binding.");
        return;
    }
    MaterialData& material = meshData_.materials[materialIndex];
    std::size_t currentPreset = 0;
    for (std::size_t index = 0; index < kSurfaceShaderDemos.size(); ++index) {
        if (material.shaderConfiguration.shader(ShaderStage::Vertex) == kSurfaceShaderDemos[index].vertexShader &&
            material.shaderConfiguration.shader(ShaderStage::Fragment) == kSurfaceShaderDemos[index].fragmentShader) {
            currentPreset = index;
            break;
        }
    }
    if (ImGui::BeginCombo("Material technique", kSurfaceShaderDemos[currentPreset].name)) {
        for (std::size_t index = 0; index < kSurfaceShaderDemos.size(); ++index) {
            const bool selected = currentPreset == index;
            if (ImGui::Selectable(kSurfaceShaderDemos[index].name, selected)) {
                material.shaderConfiguration = ShaderConfiguration{};
                material.shaderConfiguration.shader(ShaderStage::Vertex) =
                    kSurfaceShaderDemos[index].vertexShader;
                material.shaderConfiguration.shader(ShaderStage::Fragment) =
                    kSurfaceShaderDemos[index].fragmentShader;
                material.userModified = true;
                shouldSaveSettings = true;
                currentPreset = index;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::TextWrapped("%s", kSurfaceShaderDemos[currentPreset].description);
    if (currentPreset >= 10) {
        ImGui::TextWrapped("Water presets render after the opaque pass and sample scene color/depth.");
    }
}

void Renderer::createParticleSystemObject() {
    if (particleSystems_.size() >= kMaximumParticleSystems) {
        return;
    }
    std::uint32_t particleNodeId = kParticleSceneNodeBase;
    while (particleNodeId == kSunSceneNodeId ||
           findSceneNodeById(meshData_, particleNodeId) != nullptr) {
        ++particleNodeId;
    }

    ModelNode particleNode;
    particleNode.name = "Particle System " + std::to_string(particleNodeId);
    particleNode.nodeId = particleNodeId;
    particleNode.objectType = SceneObjectType::ParticleSystem;
    meshData_.additionalNodes.push_back(std::move(particleNode));
    ModelNode& createdNode = meshData_.additionalNodes.back();
    particleSystems_.try_emplace(createdNode.nodeId);
    createParticleDescriptorSets(createdNode.nodeId);
    selectModelNode(createdNode);
    saveTransformSettings();
}

void Renderer::drawParticleSystemControls(ModelNode& node, bool& shouldSaveSettings) {
    const auto systemIt = particleSystems_.find(node.nodeId);
    if (systemIt == particleSystems_.end()) {
        ImGui::TextWrapped("Particle component is not available for this object.");
        return;
    }
    ParticleSystem& particleSystem = systemIt->second;
    ParticleSystemSettings& settings = particleSystem.settings();
    bool settingsChanged = false;
    if (particleTexturePathNodeId_ != node.nodeId) {
        particleTexturePathNodeId_ = node.nodeId;
        particleTexturePathBuffer_.fill('\0');
        const std::string path = settings.texturePath.string();
        const std::size_t copyCount = std::min(
            path.size(),
            particleTexturePathBuffer_.size() - 1
        );
        std::copy_n(path.data(), copyCount, particleTexturePathBuffer_.data());
    }

    ImGui::SeparatorText("Particle System");
    ImGui::TextWrapped("GameObject: %s", node.name.c_str());
    ImGui::TextWrapped("CPU simulation + camera-facing billboard batch");

    ImGui::SeparatorText("GameObject Transform");
    ImGui::DragFloat3(
        "Object position",
        &node.transformTranslation.x,
        0.01f,
        -2.0f,
        2.0f,
        "%.2f"
    );
    shouldSaveSettings |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::DragFloat3(
        "Object rotation",
        &node.transformRotationDegrees.x,
        1.0f,
        -360.0f,
        360.0f,
        "%.1f deg"
    );
    shouldSaveSettings |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat(
        "Object scale",
        &node.transformScaleFactor,
        kMinimumModelScaleFactor,
        kMaximumModelScaleFactor,
        "%.2fx"
    );
    shouldSaveSettings |= ImGui::IsItemDeactivatedAfterEdit();

    bool enabled = settings.enabled;
    if (ImGui::Checkbox("Enabled", &enabled)) {
        particleSystem.setEnabled(enabled);
        shouldSaveSettings = true;
    }
    ImGui::SameLine();
    ImGui::Text("Alive: %zu", particleSystem.aliveCount());
    bool playOnAwake = settings.playOnAwake;
    if (ImGui::Checkbox("Play on awake", &playOnAwake)) {
        settings.playOnAwake = playOnAwake;
        shouldSaveSettings = true;
    }
    bool looping = settings.looping;
    if (ImGui::Checkbox("Looping", &looping)) {
        settings.looping = looping;
        settingsChanged = true;
    }
    if (ImGui::Button("Play")) {
        particleSystem.play();
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        particleSystem.stop();
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart")) {
        particleSystem.restart();
    }

    int maxParticles = static_cast<int>(settings.maxParticles);
    if (ImGui::SliderInt("Max particles", &maxParticles, 1, static_cast<int>(
            ParticleSystem::kMaximumSupportedParticles))) {
        settings.maxParticles = static_cast<std::uint32_t>(maxParticles);
        settingsChanged = true;
    }
    ImGui::SliderFloat("Duration", &settings.duration, 0.05f, 60.0f, "%.2f s");
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Emission rate", &settings.emissionRate, 0.0f, 200.0f, "%.1f /s");
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::SeparatorText("Main Module");
    ImGui::SliderFloat("Start lifetime", &settings.startLifetime, 0.02f, 20.0f, "%.2f s");
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Lifetime randomness", &settings.lifetimeRandomness, 0.0f, 1.0f, "%.2f");
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Start speed", &settings.startSpeed, 0.0f, 8.0f, "%.2f");
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Speed randomness", &settings.speedRandomness, 0.0f, 1.0f, "%.2f");
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Gravity", &settings.gravityModifier, -4.0f, 4.0f, "%.2f");
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::DragFloat3(
        "Position",
        &settings.position.x,
        0.01f,
        -2.0f,
        2.0f,
        "%.2f"
    );
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::SeparatorText("Shape");
    const char* shapeLabels[] = {"Point", "Sphere", "Cone", "Tornado", "Ring"};
    int shape = static_cast<int>(settings.shape);
    if (ImGui::Combo("Emitter shape", &shape, shapeLabels, IM_ARRAYSIZE(shapeLabels))) {
        settings.shape = static_cast<ParticleSystemSettings::Shape>(shape);
        settingsChanged = true;
    }
    ImGui::SliderFloat("Spread", &settings.spreadDegrees, 0.0f, 89.0f, "%.1f deg");
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::DragFloat3(
        "Direction",
        &settings.emissionDirection.x,
        0.01f,
        -1.0f,
        1.0f,
        "%.2f"
    );
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Shape radius", &settings.shapeRadius, 0.0f, 1.0f, "%.2f");
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::SeparatorText("Over Lifetime");
    ImGui::SliderFloat("Start size", &settings.startSize, 0.001f, 0.5f, "%.3f");
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("End size", &settings.endSize, 0.001f, 0.5f, "%.3f");
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat(
        "Rotation speed",
        &settings.rotationSpeedDegrees,
        -360.0f,
        360.0f,
        "%.1f deg/s"
    );
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::ColorEdit4("Start color", &settings.startColor.x);
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::ColorEdit4("End color", &settings.endColor.x);
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();

    const char* blendLabels[] = {"Alpha", "Additive"};
    int blendMode = static_cast<int>(settings.blendMode);
    if (ImGui::Combo("Blend mode", &blendMode, blendLabels, IM_ARRAYSIZE(blendLabels))) {
        settings.blendMode = static_cast<ParticleSystemSettings::BlendMode>(blendMode);
        settingsChanged = true;
    }

    ImGui::SeparatorText("Renderer");
    ImGui::InputText(
        "Particle texture",
        particleTexturePathBuffer_.data(),
        particleTexturePathBuffer_.size()
    );
    if (ImGui::Button("Load particle texture")) {
        settings.texturePath = particleTexturePathBuffer_.data();
        loadParticleTexture(node.nodeId);
        shouldSaveSettings = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear particle texture")) {
        settings.texturePath.clear();
        particleTexturePathBuffer_.fill('\0');
        loadParticleTexture(node.nodeId);
        shouldSaveSettings = true;
    }
    if (particleTextures_.contains(node.nodeId)) {
        ImGui::TextWrapped("Texture loaded; RGBA is multiplied by particle color.");
    } else {
        ImGui::TextWrapped("No valid texture; using the soft-circle fallback.");
    }
    if (ImGui::Button("Reset particle settings")) {
        particleSystem.setSettings(ParticleSystemSettings{});
        particleTexturePathBuffer_.fill('\0');
        loadParticleTexture(node.nodeId);
        shouldSaveSettings = true;
    }
    if (settingsChanged) {
        particleSystem.setSettings(ParticleSystem::sanitizeSettings(settings));
        shouldSaveSettings = true;
    }
}

void Renderer::drawResourceManager(bool& shouldSaveSettings) {
    ImGui::TextUnformatted("Resource Manager");
    ImGui::SameLine();
    ImGui::TextWrapped("Import, inspect, and remap render resources");

    constexpr float kImportButtonWidth = 108.0f;
    const float importPathWidth = std::max(
        220.0f,
        ImGui::GetContentRegionAvail().x - kImportButtonWidth -
            ImGui::GetStyle().ItemSpacing.x
    );
    ImGui::SetNextItemWidth(importPathWidth);
    ImGui::InputTextWithHint(
        "##ModelImportPath",
        "Absolute or workspace-relative model path",
        modelImportPathBuffer_.data(),
        modelImportPathBuffer_.size()
    );
    ImGui::SameLine();
    if (ImGui::Button("Import Model", ImVec2(kImportButtonWidth, 0.0f))) {
        if (importModelResource(std::filesystem::path(modelImportPathBuffer_.data()))) {
            saveTransformSettings();
        }
    }
    if (!resourceImportStatus_.empty()) {
        const ImVec4 color = resourceImportFailed_
            ? ImVec4(1.0f, 0.38f, 0.32f, 1.0f)
            : ImVec4(0.42f, 0.88f, 0.56f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextWrapped("%s", resourceImportStatus_.c_str());
        ImGui::PopStyleColor();
    }

    constexpr float kSceneSaveButtonWidth = 96.0f;
    constexpr float kNewSceneButtonWidth = 132.0f;
    const float scenePathWidth = std::max(
        180.0f,
        ImGui::GetContentRegionAvail().x - kSceneSaveButtonWidth -
            kNewSceneButtonWidth - ImGui::GetStyle().ItemSpacing.x * 2.0f
    );
    ImGui::SetNextItemWidth(scenePathWidth);
    ImGui::InputTextWithHint(
        "##ScenePath",
        "Scene path, e.g. scenes/demo.scene",
        scenePathBuffer_.data(),
        scenePathBuffer_.size()
    );
    ImGui::SameLine();
    if (ImGui::Button("Save Scene", ImVec2(kSceneSaveButtonWidth, 0.0f))) {
        saveSceneResource(std::filesystem::path(scenePathBuffer_.data()));
    }
    ImGui::SameLine();
    if (ImGui::Button("New Empty Scene", ImVec2(kNewSceneButtonWidth, 0.0f))) {
        createNewEmptyScene();
    }
    if (!sceneStatus_.empty()) {
        const ImVec4 color = sceneStatusFailed_
            ? ImVec4(1.0f, 0.38f, 0.32f, 1.0f)
            : ImVec4(0.42f, 0.88f, 0.56f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextWrapped("%s", sceneStatus_.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Separator();
    constexpr ImGuiTableFlags kWorkspaceFlags =
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("ResourceWorkspace", 3, kWorkspaceFlags)) {
        return;
    }
    ImGui::TableSetupColumn("Assets", ImGuiTableColumnFlags_WidthStretch, 0.85f);
    ImGui::TableSetupColumn("Model Hierarchy", ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn("Inspector", ImGuiTableColumnFlags_WidthStretch, 1.45f);
    ImGui::TableNextRow();

    const std::array<const char*, 3> textureSlotLabels = {
        "Base Color",
        "Normal",
        "Specular",
    };
    const auto texturePath = [](const MaterialData& material, ResourceTextureSlot slot)
        -> const std::filesystem::path& {
        switch (slot) {
        case ResourceTextureSlot::BaseColor:
            return material.texturePath;
        case ResourceTextureSlot::Normal:
            return material.normalTexturePath;
        case ResourceTextureSlot::Specular:
            return material.specularTexturePath;
        }
        return material.texturePath;
    };
    const auto embeddedTexture = [](const MaterialData& material, ResourceTextureSlot slot)
        -> const EmbeddedTextureData& {
        switch (slot) {
        case ResourceTextureSlot::BaseColor:
            return material.embeddedTexture;
        case ResourceTextureSlot::Normal:
            return material.embeddedNormalTexture;
        case ResourceTextureSlot::Specular:
            return material.embeddedSpecularTexture;
        }
        return material.embeddedTexture;
    };
    const auto animationBehaviorLabel = [](AnimationBehavior behavior) {
        switch (behavior) {
        case AnimationBehavior::Default:
            return "Default";
        case AnimationBehavior::Constant:
            return "Constant";
        case AnimationBehavior::Linear:
            return "Linear";
        case AnimationBehavior::Repeat:
            return "Repeat";
        }
        return "Default";
    };
    const auto animationChannelKeyCount = [](const SkeletalAnimationChannel& channel) {
        return channel.positionKeys.size() +
            channel.rotationKeys.size() +
            channel.scalingKeys.size();
    };
    const MeshData* selectedResourceModelData = selectedResourceType_ == ResourceSelectionType::Model
        ? selectedModelPreview_.get()
        : nullptr;
    const bool editingCachedModelMaterials = selectedModelPreview_ != nullptr &&
        !selectedModelResourcePath_.empty();
    std::vector<MaterialData>* resourceMaterials = editingCachedModelMaterials
        ? &selectedModelPreview_->materials
        : &meshData_.materials;
    const auto textureIsLoaded = [this, editingCachedModelMaterials](
        std::size_t materialIndex,
        ResourceTextureSlot slot
    ) {
        if (editingCachedModelMaterials) {
            return false;
        }
        switch (slot) {
        case ResourceTextureSlot::BaseColor:
            return materialIndex < materialTextures_.size() &&
                materialTextures_[materialIndex] != nullptr;
        case ResourceTextureSlot::Normal:
            return materialIndex < normalTextures_.size() &&
                normalTextures_[materialIndex] != nullptr;
        case ResourceTextureSlot::Specular:
            return materialIndex < specularTextures_.size() &&
                specularTextures_[materialIndex] != nullptr;
        }
        return false;
    };

    ImGui::TableSetColumnIndex(0);
    ImGui::BeginChild(
        "ResourceLibrary",
        ImVec2(0.0f, 0.0f),
        false,
        ImGuiWindowFlags_NoScrollbar
    );
    ImGui::TextUnformatted("Assets");
    ImGui::TextWrapped("Model resources under assets");
    ImGui::TextWrapped("Select a model to inspect and edit its cached materials");
    ImGui::Separator();

    const auto modelResourceLabel = [this](const std::filesystem::path& path) {
        std::string label;
        if (!assetsDirectory_.empty()) {
            std::error_code error;
            const std::filesystem::path relativePath =
                std::filesystem::relative(path, assetsDirectory_, error);
            if (!error && !relativePath.empty() && relativePath.string() != ".") {
                label = relativePath.generic_string();
            }
        }
        if (label.empty()) {
            label = path.filename().string();
        }
        return label.empty() ? std::string("Unnamed model") : label;
    };
    const auto sceneResourceLabel = [this](const std::filesystem::path& path) {
        std::string label;
        if (!scenesDirectory_.empty()) {
            std::error_code error;
            const std::filesystem::path relativePath =
                std::filesystem::relative(path, scenesDirectory_, error);
            if (!error && !relativePath.empty() && relativePath.string() != ".") {
                label = relativePath.generic_string();
            }
        }
        if (label.empty()) {
            label = path.filename().string();
        }
        return label.empty() ? std::string("Unnamed scene") : label;
    };

    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    const std::string modelsLabel = "Models (" +
        std::to_string(modelResourcePaths_.size()) + ")";
    if (ImGui::TreeNodeEx(modelsLabel.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
        if (modelResourcePaths_.empty()) {
            ImGui::TextWrapped("No model resources found under assets.");
        }
        for (std::size_t modelIndex = 0;
             modelIndex < modelResourcePaths_.size();
             ++modelIndex) {
            const std::filesystem::path& modelPath = modelResourcePaths_[modelIndex];
            const std::string label = modelResourceLabel(modelPath);
            const bool selected = selectedResourceType_ == ResourceSelectionType::Model &&
                pathsEqual(selectedModelResourcePath_, modelPath);
            ImGui::PushID(static_cast<int>(modelIndex));
            if (ImGui::Selectable(label.c_str(), selected)) {
                selectModelResource(modelPath);
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    const std::string scenesLabel = "Scenes (" +
        std::to_string(sceneResourcePaths_.size()) + ")";
    if (ImGui::TreeNodeEx(scenesLabel.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
        if (sceneResourcePaths_.empty()) {
            ImGui::TextWrapped("No scene files found. Save one above.");
        }
        for (std::size_t sceneIndex = 0;
             sceneIndex < sceneResourcePaths_.size();
             ++sceneIndex) {
            const std::filesystem::path& scenePath = sceneResourcePaths_[sceneIndex];
            const std::string label = sceneResourceLabel(scenePath);
            const bool selected = !currentScenePath_.empty() &&
                pathsEqual(currentScenePath_, scenePath);
            ImGui::PushID(static_cast<int>(sceneIndex));
            if (ImGui::Selectable(label.c_str(), selected)) {
                loadSceneResource(scenePath);
            }
            if (selected) {
                ImGui::SameLine();
                ImGui::TextWrapped("[active]");
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    const std::string materialsLabel = "Materials (" +
        std::to_string(resourceMaterials->size()) + ")";
    if (ImGui::TreeNodeEx(materialsLabel.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
        for (std::size_t materialIndex = 0;
             materialIndex < resourceMaterials->size();
             ++materialIndex) {
            const MaterialData& material = (*resourceMaterials)[materialIndex];
            const std::string label = material.name.empty()
                ? "Material " + std::to_string(materialIndex)
                : material.name;
            ImGui::PushID(static_cast<int>(materialIndex));
            const bool selected = selectedResourceType_ == ResourceSelectionType::Material &&
                selectedResourceMaterialIndex_ == static_cast<std::int32_t>(materialIndex);
            if (ImGui::Selectable(label.c_str(), selected)) {
                selectedResourceType_ = ResourceSelectionType::Material;
                selectedResourceMaterialIndex_ = static_cast<std::int32_t>(materialIndex);
                selectedModelNode_ = nullptr;
                updateBoundingBoxVertices();
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    ImGui::SetNextItemOpen(false, ImGuiCond_Once);
    if (ImGui::TreeNodeEx("Textures", ImGuiTreeNodeFlags_SpanAvailWidth)) {
        bool hasTextureResource = false;
        for (std::size_t materialIndex = 0;
             materialIndex < resourceMaterials->size();
             ++materialIndex) {
            const MaterialData& material = (*resourceMaterials)[materialIndex];
            for (std::size_t slotIndex = 0; slotIndex < textureSlotLabels.size(); ++slotIndex) {
                const auto slot = static_cast<ResourceTextureSlot>(slotIndex);
                const std::filesystem::path& path = texturePath(material, slot);
                const EmbeddedTextureData& embedded = embeddedTexture(material, slot);
                if (path.empty() && embedded.empty()) {
                    continue;
                }
                hasTextureResource = true;
                std::string sourceName = !path.empty()
                    ? path.filename().string()
                    : "<embedded>";
                if (sourceName.empty()) {
                    sourceName = path.string();
                }
                const std::string label = std::string(textureSlotLabels[slotIndex]) +
                    ": " + sourceName;
                ImGui::PushID(static_cast<int>(materialIndex));
                ImGui::PushID(static_cast<int>(slotIndex));
                const bool selected = selectedResourceType_ == ResourceSelectionType::Texture &&
                    selectedResourceMaterialIndex_ == static_cast<std::int32_t>(materialIndex) &&
                    selectedResourceTextureSlot_ == slot;
                if (ImGui::Selectable(label.c_str(), selected)) {
                    selectedResourceType_ = ResourceSelectionType::Texture;
                    selectedResourceMaterialIndex_ = static_cast<std::int32_t>(materialIndex);
                    selectedResourceTextureSlot_ = slot;
                    selectedModelNode_ = nullptr;
                    updateBoundingBoxVertices();
                }
                ImGui::PopID();
                ImGui::PopID();
            }
        }
        if (!hasTextureResource) {
            ImGui::TextWrapped("No referenced textures");
        }
        ImGui::TreePop();
    }
    ImGui::EndChild();

    ImGui::TableSetColumnIndex(1);
    ImGui::BeginChild(
        "ResourceModelHierarchy",
        ImVec2(0.0f, 0.0f),
        false,
        ImGuiWindowFlags_NoScrollbar
    );
    ImGui::TextUnformatted("Model Hierarchy");
    const MeshData* hierarchyData = selectedResourceType_ == ResourceSelectionType::Model
        ? selectedResourceModelData
        : &meshData_;
    if (hierarchyData == nullptr) {
        ImGui::TextWrapped("Unable to inspect the selected model resource.");
        if (!selectedModelPreviewError_.empty()) {
            ImGui::TextWrapped("%s", selectedModelPreviewError_.c_str());
        }
    } else {
        ImGui::TextWrapped(
            "%zu nodes / %zu mesh nodes",
            countModelNodes(hierarchyData->hierarchy),
            countMeshNodes(hierarchyData->hierarchy)
        );
        ImGui::Separator();
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (!hierarchyData->hierarchy.name.empty()) {
            if (selectedResourceType_ == ResourceSelectionType::Model) {
                drawResourceModelHierarchy(hierarchyData->hierarchy);
            } else {
                drawModelHierarchy(meshData_.hierarchy);
            }
        } else {
            ImGui::TextWrapped("No model hierarchy available for this resource.");
        }
    }
    ImGui::EndChild();

    ImGui::TableSetColumnIndex(2);
    ImGui::BeginChild(
        "ResourceInspector",
        ImVec2(0.0f, 0.0f),
        false,
        ImGuiWindowFlags_NoScrollbar
    );
    ImGui::TextUnformatted("Inspector");
    ImGui::Separator();

    if (selectedResourceType_ == ResourceSelectionType::Model) {
        if (selectedResourceModelData == nullptr) {
            ImGui::TextWrapped("Unable to inspect the selected model resource.");
            if (!selectedModelPreviewError_.empty()) {
                ImGui::TextWrapped("%s", selectedModelPreviewError_.c_str());
            }
        } else {
            const MeshData& modelData = *selectedResourceModelData;
            const std::string modelName = selectedModelResourcePath_.filename().string();
            ImGui::TextWrapped("Model: %s", modelName.empty() ? "Unnamed model" : modelName.c_str());
            ImGui::TextWrapped("Source: %s", selectedModelResourcePath_.string().c_str());
            ImGui::TextColored(
                ImVec4(0.52f, 0.72f, 1.0f, 1.0f),
                "Cached model information"
            );
            const auto cacheIt = modelResourceCache_.find(
                normalizedPath(selectedModelResourcePath_).string()
            );
            if (cacheIt != modelResourceCache_.end()) {
                ImGui::TextWrapped(
                    "%zu texture candidates from adjacent tex folder",
                    cacheIt->second.texturePaths.size()
                );
            }

            ImGui::SeparatorText("Model Information");
            ImGui::Text(
                "%zu vertices / %zu triangles / %zu materials",
                modelData.vertices.size(),
                modelData.indices.size() / 3,
                modelData.materials.size()
            );
            ImGui::Text(
                "%zu mesh parts / %zu hierarchy nodes",
                modelData.parts.size(),
                countModelNodes(modelData.hierarchy)
            );
            if (!modelData.vertices.empty()) {
                glm::vec3 minimum(std::numeric_limits<float>::max());
                glm::vec3 maximum(std::numeric_limits<float>::lowest());
                for (const Vertex& vertex : modelData.vertices) {
                    minimum = glm::min(minimum, vertex.position);
                    maximum = glm::max(maximum, vertex.position);
                }
                ImGui::Text("Bounds min: (%.2f, %.2f, %.2f)", minimum.x, minimum.y, minimum.z);
                ImGui::Text("Bounds max: (%.2f, %.2f, %.2f)", maximum.x, maximum.y, maximum.z);
            }
            ImGui::SeparatorText("Engine-ready import");
            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::TextWrapped("Triangulated mesh with 32-bit indices");
            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::TextWrapped("Position, generated/preserved normals, UV0, vertex color");
            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::TextWrapped("Base Color, Normal, and Specular texture slots");

            ImGui::SeparatorText("Skeleton");
            if (modelData.skeletonBones.empty()) {
                ImGui::TextWrapped("No skeletal skinning data found in this model.");
            } else {
                ImGui::Text(
                    "%zu bones / %zu skinned meshes / %zu skin weights",
                    modelData.skeletonBones.size(),
                    modelData.skinnedMeshCount,
                    modelData.skinWeightCount
                );
                if (ImGui::BeginTable(
                        "ResourceSkeletonBones",
                        4,
                        ImGuiTableFlags_Borders |
                            ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchProp
                    )) {
                    ImGui::TableSetupColumn("Bone");
                    ImGui::TableSetupColumn("Node ID");
                    ImGui::TableSetupColumn("Parent bone");
                    ImGui::TableSetupColumn("Influences");
                    ImGui::TableHeadersRow();
                    for (std::size_t boneIndex = 0;
                         boneIndex < modelData.skeletonBones.size();
                         ++boneIndex) {
                        const SkeletonBone& bone = modelData.skeletonBones[boneIndex];
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextWrapped("%s", bone.name.c_str());
                        ImGui::TableSetColumnIndex(1);
                        if (bone.nodeId == std::numeric_limits<std::uint32_t>::max()) {
                            ImGui::TextUnformatted("unresolved");
                        } else {
                            ImGui::Text("%u", bone.nodeId);
                        }
                        ImGui::TableSetColumnIndex(2);
                        if (bone.parentIndex >= 0 &&
                            static_cast<std::size_t>(bone.parentIndex) <
                                modelData.skeletonBones.size()) {
                            ImGui::TextWrapped(
                                "%s",
                                modelData.skeletonBones[
                                    static_cast<std::size_t>(bone.parentIndex)
                                ].name.c_str()
                            );
                        } else {
                            ImGui::TextUnformatted("-");
                        }
                        ImGui::TableSetColumnIndex(3);
                        ImGui::Text("%u", bone.influenceCount);
                    }
                    ImGui::EndTable();
                }
            }

            ImGui::SeparatorText("Skeletal Animations");
            if (modelData.skeletalAnimations.empty()) {
                ImGui::TextWrapped("No skeletal animation clips found in this model.");
            } else {
                ImGui::Text(
                    "%zu clip(s)",
                    modelData.skeletalAnimations.size()
                );
                for (std::size_t animationIndex = 0;
                     animationIndex < modelData.skeletalAnimations.size();
                     ++animationIndex) {
                    const SkeletalAnimationClip& animation =
                        modelData.skeletalAnimations[animationIndex];
                    ImGui::PushID(static_cast<int>(animationIndex));
                    const std::string animationLabel = animation.name.empty()
                        ? "Animation " + std::to_string(animationIndex)
                        : animation.name;
                    if (ImGui::TreeNodeEx(
                            animationLabel.c_str(),
                            ImGuiTreeNodeFlags_SpanAvailWidth
                        )) {
                        std::size_t totalKeyCount = 0;
                        for (const SkeletalAnimationChannel& channel : animation.channels) {
                            totalKeyCount += animationChannelKeyCount(channel);
                        }
                        if (animation.ticksPerSecond > 0.0) {
                            ImGui::Text(
                                "Duration: %.3f s (%.3f ticks) / %.3f ticks per second",
                                animation.durationTicks / animation.ticksPerSecond,
                                animation.durationTicks,
                                animation.ticksPerSecond
                            );
                        } else {
                            ImGui::Text(
                                "Duration: %.3f ticks / ticks per second unspecified",
                                animation.durationTicks
                            );
                        }
                        ImGui::Text(
                            "%zu channels / %zu keyframes / %u morph channels",
                            animation.channels.size(),
                            totalKeyCount,
                            animation.meshChannelCount
                        );
                        if (ImGui::BeginTable(
                                "ResourceAnimationChannels",
                                6,
                                ImGuiTableFlags_Borders |
                                    ImGuiTableFlags_RowBg |
                                    ImGuiTableFlags_SizingStretchProp
                            )) {
                            ImGui::TableSetupColumn("Channel");
                            ImGui::TableSetupColumn("Bone");
                            ImGui::TableSetupColumn("Position");
                            ImGui::TableSetupColumn("Rotation");
                            ImGui::TableSetupColumn("Scale");
                            ImGui::TableSetupColumn("Pre / post");
                            ImGui::TableHeadersRow();
                            for (const SkeletalAnimationChannel& channel : animation.channels) {
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                ImGui::TextWrapped("%s", channel.nodeName.c_str());
                                ImGui::TableSetColumnIndex(1);
                                if (channel.boneIndex >= 0 &&
                                    static_cast<std::size_t>(channel.boneIndex) <
                                        modelData.skeletonBones.size()) {
                                    ImGui::TextWrapped(
                                        "%s",
                                        modelData.skeletonBones[
                                            static_cast<std::size_t>(channel.boneIndex)
                                        ].name.c_str()
                                    );
                                } else {
                                    ImGui::TextUnformatted("node only");
                                }
                                ImGui::TableSetColumnIndex(2);
                                ImGui::Text("%zu", channel.positionKeys.size());
                                ImGui::TableSetColumnIndex(3);
                                ImGui::Text("%zu", channel.rotationKeys.size());
                                ImGui::TableSetColumnIndex(4);
                                ImGui::Text("%zu", channel.scalingKeys.size());
                                ImGui::TableSetColumnIndex(5);
                                ImGui::Text(
                                    "%s / %s",
                                    animationBehaviorLabel(channel.preBehavior),
                                    animationBehaviorLabel(channel.postBehavior)
                                );
                            }
                            ImGui::EndTable();
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
            }

            if (!modelData.materials.empty()) {
                ImGui::SeparatorText("Materials in Model");
                for (std::size_t materialIndex = 0;
                     materialIndex < modelData.materials.size();
                     ++materialIndex) {
                    const MaterialData& material = modelData.materials[materialIndex];
                    const std::string materialLabel = material.name.empty()
                        ? "Material " + std::to_string(materialIndex)
                        : material.name;
                    ImGui::Bullet();
                    ImGui::SameLine();
                    ImGui::TextWrapped("%s", materialLabel.c_str());
                }
            }

            ImGui::TextWrapped(
                "Select a material or texture in Assets to edit this cached model resource."
            );
            ImGui::TextWrapped("Import this resource to append another model to the scene.");
            if (ImGui::Button("Import Model to Scene")) {
                if (importModelResource(selectedModelResourcePath_)) {
                    saveTransformSettings();
                }
            }
            const bool canReplacePlayer = terrainScene_ != nullptr &&
                playerController_.active() && !currentScenePath_.empty();
            if (!canReplacePlayer) {
                ImGui::BeginDisabled();
            }
            const bool selectedAsPlayer = !playerModelResourcePath_.empty() &&
                pathsEqual(playerModelResourcePath_, selectedModelResourcePath_);
            if (ImGui::Button(
                    selectedAsPlayer ? "Current Player Model" : "Set as Player Model"
                ) && !selectedAsPlayer) {
                playerModelResourcePath_ = normalizedPath(selectedModelResourcePath_);
                if (saveTransformSettings()) {
                    loadSceneResource(currentScenePath_);
                }
            }
            if (!playerModelResourcePath_.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("Use Player Capsule")) {
                    playerModelResourcePath_.clear();
                    if (saveTransformSettings()) {
                        loadSceneResource(currentScenePath_);
                    }
                }
            }
            if (!canReplacePlayer) {
                ImGui::EndDisabled();
                ImGui::TextWrapped(
                    "Load a saved terrain scene with an active Player to replace its model."
                );
            } else {
                ImGui::TextWrapped(
                    "Player models are height-fitted to the existing capsule collider."
                );
            }
            if (selectedModelNode_ == nullptr) {
            ImGui::TextWrapped("Select a hierarchy node to inspect its material bindings.");
            } else if (selectedModelNode_->objectType == SceneObjectType::ParticleSystem) {
            ImGui::TextWrapped("The selected object is a Particle System, not a model node.");
            } else if (selectedModelNode_->objectType == SceneObjectType::Player) {
            ImGui::SeparatorText("Player Controller");
            ImGui::TextWrapped("Collider height: %.2f m", PlayerController::kBotwLinkHeight);
            ImGui::TextWrapped(
                "Visual: %s",
                playerModelResourcePath_.empty()
                    ? "procedural capsule"
                    : playerModelResourcePath_.filename().string().c_str()
            );
            ImGui::TextWrapped("Physics: NVIDIA PhysX 5.6.1 (60 Hz fixed step, CCD)");
            ImGui::Text("Grounded: %s", playerController_.grounded() ? "yes" : "no");
            const glm::vec3& playerVelocity = playerController_.velocity();
            ImGui::Text(
                "Velocity: (%.2f, %.2f, %.2f) m/s",
                playerVelocity.x,
                playerVelocity.y,
                playerVelocity.z
            );
            ImGui::TextWrapped(
                "WASD / Arrow keys move and turn, Space jumps, hold RMB to orbit the camera."
            );
            } else {
            ImGui::SeparatorText("Selected Model Node");
            ImGui::TextWrapped("%s  (ID %u)",
                selectedModelNode_->name.c_str(),
                selectedModelNode_->nodeId
            );
            ImGui::Text(
                "%u direct meshes / %zu material parts",
                selectedModelNode_->meshCount,
                selectedModelNode_->meshPartIndices.size()
            );

            if (!selectedModelNode_->meshPartIndices.empty() &&
                ImGui::BeginTable(
                    "NodeMaterialBindings",
                    3,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp
                )) {
                ImGui::TableSetupColumn("Part");
                ImGui::TableSetupColumn("Imported material");
                ImGui::TableSetupColumn("Effective material");
                ImGui::TableHeadersRow();
                for (std::size_t bindingIndex = 0;
                     bindingIndex < selectedModelNode_->meshPartIndices.size();
                     ++bindingIndex) {
                    const std::uint32_t partIndex =
                        selectedModelNode_->meshPartIndices[bindingIndex];
                    if (partIndex >= meshData_.parts.size()) {
                        continue;
                    }
                    const MeshPart& part = meshData_.parts[partIndex];
                    const std::uint32_t effectiveMaterialIndex = materialIndexForPart(part);
                    const char* importedName = part.materialIndex < meshData_.materials.size()
                        ? meshData_.materials[part.materialIndex].name.c_str()
                        : "<invalid>";
                    const char* effectiveName = effectiveMaterialIndex < meshData_.materials.size()
                        ? meshData_.materials[effectiveMaterialIndex].name.c_str()
                        : "<invalid>";
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%zu", bindingIndex);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", importedName);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", effectiveName);
                }
                ImGui::EndTable();
            } else if (selectedModelNode_->meshPartIndices.empty()) {
                ImGui::TextWrapped(
                    "This grouping node has no direct mesh; select a mesh-bearing child."
                );
            }
            drawNodeMaterialControls(*selectedModelNode_, shouldSaveSettings);
        }
        }
    } else if (selectedResourceMaterialIndex_ < 0 ||
               static_cast<std::size_t>(selectedResourceMaterialIndex_) >=
                   resourceMaterials->size()) {
        ImGui::TextWrapped("The selected resource is no longer available.");
    } else {
        const std::size_t materialIndex = static_cast<std::size_t>(
            selectedResourceMaterialIndex_
        );
        MaterialData& material = (*resourceMaterials)[materialIndex];
        const std::filesystem::path* editedModelPath = editingCachedModelMaterials
            ? &selectedModelResourcePath_
            : nullptr;
        if (selectedResourceType_ == ResourceSelectionType::Material) {
            ImGui::TextWrapped("Material #%zu", materialIndex);
            ImGui::TextWrapped("Name: %s", material.name.c_str());
            ImGui::SeparatorText("Referenced textures");
            for (std::size_t slotIndex = 0; slotIndex < textureSlotLabels.size(); ++slotIndex) {
                const auto slot = static_cast<ResourceTextureSlot>(slotIndex);
                const std::filesystem::path& path = texturePath(material, slot);
                const EmbeddedTextureData& embedded = embeddedTexture(material, slot);
                const bool configured = !path.empty() || !embedded.empty();
                const char* state = editingCachedModelMaterials
                    ? (configured ? "cached" : "fallback")
                    : (textureIsLoaded(materialIndex, slot) ? "loaded" : "fallback");
                if (!path.empty()) {
                    ImGui::TextWrapped(
                        "%s [%s]: %s",
                        textureSlotLabels[slotIndex],
                        state,
                        path.string().c_str()
                    );
                } else if (!embedded.empty()) {
                    ImGui::TextWrapped(
                        "%s [loaded]: embedded %ux%u",
                        textureSlotLabels[slotIndex],
                        embedded.width,
                        embedded.height
                    );
                } else {
                    ImGui::TextWrapped("%s: engine fallback", textureSlotLabels[slotIndex]);
                }
            }
        } else {
            const std::size_t slotIndex = static_cast<std::size_t>(
                selectedResourceTextureSlot_
            );
            const std::filesystem::path& path = texturePath(
                material,
                selectedResourceTextureSlot_
            );
            const EmbeddedTextureData& embedded = embeddedTexture(
                material,
                selectedResourceTextureSlot_
            );
            ImGui::TextWrapped("Texture: %s", textureSlotLabels[slotIndex]);
            ImGui::TextWrapped("Material: %s", material.name.c_str());
            if (!path.empty()) {
                ImGui::TextWrapped("Source: %s", path.string().c_str());
            } else if (!embedded.empty()) {
                ImGui::TextWrapped("Source: embedded (%ux%u)", embedded.width, embedded.height);
            } else {
                ImGui::TextWrapped("Source: engine fallback texture");
            }
            const bool loaded = editingCachedModelMaterials
                ? (!path.empty() || !embedded.empty())
                : textureIsLoaded(materialIndex, selectedResourceTextureSlot_);
            const char* resourceState = editingCachedModelMaterials
                ? (loaded ? "Cached texture configuration" : "Using engine fallback")
                : (loaded ? "GPU resource loaded" : "Using engine fallback");
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                loaded ? ImVec4(0.42f, 0.88f, 0.56f, 1.0f)
                       : ImVec4(1.0f, 0.72f, 0.28f, 1.0f)
            );
            ImGui::TextWrapped("%s", resourceState);
            ImGui::PopStyleColor();
        }
        ImGui::SeparatorText("Material Mapping");
        drawMaterialControls(
            material,
            selectedResourceMaterialIndex_,
            shouldSaveSettings,
            editedModelPath
        );
    }

    ImGui::EndChild();
    ImGui::EndTable();
}

void Renderer::drawMiniMap() {
    if (!terrainScene_ || !physicsWorld_ || !playerController_.active()) {
        return;
    }

    const glm::vec3& worldMinimum = terrainScene_->boundsMinimum();
    const glm::vec3& worldMaximum = terrainScene_->boundsMaximum();
    const float worldWidth = worldMaximum.x - worldMinimum.x;
    const float worldDepth = worldMaximum.z - worldMinimum.z;
    if (!std::isfinite(worldWidth) || !std::isfinite(worldDepth) ||
        worldWidth <= std::numeric_limits<float>::epsilon() ||
        worldDepth <= std::numeric_limits<float>::epsilon()) {
        return;
    }

    constexpr float kMapCanvasSize = 196.0f;
    constexpr ImVec2 kMapWindowSize(216.0f, 258.0f);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(
            viewport->WorkPos.x + viewport->WorkSize.x - kMapWindowSize.x - 14.0f,
            viewport->WorkPos.y + 14.0f
        ),
        ImGuiCond_Always
    );
    ImGui::SetNextWindowSize(kMapWindowSize, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.88f);

    constexpr ImGuiWindowFlags kMapWindowFlags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    if (!ImGui::Begin("Mini Map", nullptr, kMapWindowFlags)) {
        ImGui::End();
        ImGui::PopStyleVar(2);
        return;
    }

    ImGui::TextUnformatted("Mini Map");
    ImGui::SameLine();
    ImGui::TextDisabled("click to travel");

    const ImVec2 canvasMinimum = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize(kMapCanvasSize, kMapCanvasSize);
    ImGui::InvisibleButton("##MiniMapCanvas", canvasSize);
    const bool mapHovered = ImGui::IsItemHovered();
    const bool mapClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

    const float worldToMapScale = std::min(
        canvasSize.x / worldWidth,
        canvasSize.y / worldDepth
    );
    const ImVec2 mapSize(
        worldWidth * worldToMapScale,
        worldDepth * worldToMapScale
    );
    const ImVec2 mapMinimum(
        canvasMinimum.x + (canvasSize.x - mapSize.x) * 0.5f,
        canvasMinimum.y + (canvasSize.y - mapSize.y) * 0.5f
    );
    const ImVec2 mapMaximum(
        mapMinimum.x + mapSize.x,
        mapMinimum.y + mapSize.y
    );
    const auto worldToMap = [&](float worldX, float worldZ) {
        return ImVec2(
            mapMinimum.x + (worldX - worldMinimum.x) / worldWidth * mapSize.x,
            mapMinimum.y + (worldZ - worldMinimum.z) / worldDepth * mapSize.y
        );
    };
    const auto mixColor = [](const ImVec4& from, const ImVec4& to, float amount) {
        amount = std::clamp(amount, 0.0f, 1.0f);
        return ImVec4(
            from.x + (to.x - from.x) * amount,
            from.y + (to.y - from.y) * amount,
            from.z + (to.z - from.z) * amount,
            from.w + (to.w - from.w) * amount
        );
    };

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(
        canvasMinimum,
        ImVec2(canvasMinimum.x + canvasSize.x, canvasMinimum.y + canvasSize.y),
        IM_COL32(10, 18, 18, 255),
        5.0f
    );
    drawList->PushClipRect(mapMinimum, mapMaximum, true);
    const float terrainHeightRange = std::max(
        worldMaximum.y - worldMinimum.y,
        0.0001f
    );
    const ImVec4 lowColor(0.10f, 0.27f, 0.13f, 1.0f);
    const ImVec4 middleColor(0.33f, 0.52f, 0.19f, 1.0f);
    const ImVec4 highColor(0.52f, 0.50f, 0.38f, 1.0f);
    for (const TerrainChunk& chunk : terrainScene_->chunks()) {
        for (const TerrainTile& tile : chunk.tiles()) {
            const glm::vec3& tileMinimum = tile.boundsMinimum();
            const glm::vec3& tileMaximum = tile.boundsMaximum();
            const ImVec2 tileStart = worldToMap(tileMinimum.x, tileMinimum.z);
            const ImVec2 tileEnd = worldToMap(tileMaximum.x, tileMaximum.z);
            const float averageHeight = (tileMinimum.y + tileMaximum.y) * 0.5f;
            const float normalizedHeight = std::clamp(
                (averageHeight - worldMinimum.y) / terrainHeightRange,
                0.0f,
                1.0f
            );
            const ImVec4 tileColor = normalizedHeight < 0.58f
                ? mixColor(lowColor, middleColor, normalizedHeight / 0.58f)
                : mixColor(
                    middleColor,
                    highColor,
                    (normalizedHeight - 0.58f) / 0.42f
                );
            drawList->AddRectFilled(
                ImVec2(std::min(tileStart.x, tileEnd.x), std::min(tileStart.y, tileEnd.y)),
                ImVec2(std::max(tileStart.x, tileEnd.x), std::max(tileStart.y, tileEnd.y)),
                ImGui::ColorConvertFloat4ToU32(tileColor)
            );
            drawList->AddRect(
                ImVec2(std::min(tileStart.x, tileEnd.x), std::min(tileStart.y, tileEnd.y)),
                ImVec2(std::max(tileStart.x, tileEnd.x), std::max(tileStart.y, tileEnd.y)),
                IM_COL32(220, 238, 191, 24)
            );
        }
    }
    drawList->PopClipRect();
    drawList->AddRect(mapMinimum, mapMaximum, IM_COL32(223, 238, 205, 185), 3.0f, 0, 1.4f);

    const glm::vec3& playerPosition = playerController_.position();
    const ImVec2 playerMapPosition = worldToMap(playerPosition.x, playerPosition.z);
    const glm::vec2 facingDirection(
        std::sin(playerController_.facingYawRadians()),
        std::cos(playerController_.facingYawRadians())
    );
    const ImVec2 facingScreenDirection(facingDirection.x, facingDirection.y);
    const ImVec2 markerTip(
        playerMapPosition.x + facingScreenDirection.x * 10.0f,
        playerMapPosition.y + facingScreenDirection.y * 10.0f
    );
    const ImVec2 markerRight(-facingScreenDirection.y, facingScreenDirection.x);
    drawList->AddTriangleFilled(
        markerTip,
        ImVec2(
            playerMapPosition.x - facingScreenDirection.x * 4.0f + markerRight.x * 5.0f,
            playerMapPosition.y - facingScreenDirection.y * 4.0f + markerRight.y * 5.0f
        ),
        ImVec2(
            playerMapPosition.x - facingScreenDirection.x * 4.0f - markerRight.x * 5.0f,
            playerMapPosition.y - facingScreenDirection.y * 4.0f - markerRight.y * 5.0f
        ),
        IM_COL32(255, 238, 102, 255)
    );
    drawList->AddCircle(playerMapPosition, 7.0f, IM_COL32(30, 42, 28, 255), 20, 2.0f);
    drawList->AddText(
        ImVec2(mapMinimum.x + 5.0f, mapMinimum.y + 4.0f),
        IM_COL32(240, 244, 225, 210),
        "N"
    );

    const ImVec2 mousePosition = ImGui::GetIO().MousePos;
    const bool mouseInsideMap = mousePosition.x >= mapMinimum.x &&
        mousePosition.x <= mapMaximum.x &&
        mousePosition.y >= mapMinimum.y &&
        mousePosition.y <= mapMaximum.y;
    if (mapHovered && mouseInsideMap) {
        const glm::vec2 destination(
            worldMinimum.x + (mousePosition.x - mapMinimum.x) /
                mapSize.x * worldWidth,
            worldMinimum.z + (mousePosition.y - mapMinimum.y) /
                mapSize.y * worldDepth
        );
        const std::optional<float> destinationHeight =
            terrainScene_->renderedHeightAtWorld(destination);
        if (destinationHeight.has_value()) {
            ImGui::SetTooltip(
                "Travel to X %.1f, Z %.1f",
                destination.x,
                destination.y
            );
            if (mapClicked && playerController_.teleport(
                    *terrainScene_,
                    *physicsWorld_,
                    destination
                )) {
                updatePlayerCamera();
            }
        } else {
            ImGui::SetTooltip("No traversable terrain here");
        }
    }

    ImGui::Text(
        "X %.1f   Z %.1f",
        playerController_.position().x,
        playerController_.position().z
    );
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void Renderer::buildImGuiFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
        toolUiVisible_ = !toolUiVisible_;
    }
    if (!toolUiVisible_) {
        drawMiniMap();
        ImGui::Render();
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    constexpr ImGuiWindowFlags kMainWindowFlags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBackground;

    ImGui::Begin("VulkCore Main Screen", nullptr, kMainWindowFlags);
    constexpr float kPanelMargin = 16.0f;
    constexpr float kLeftPanelWidth = 300.0f;
    constexpr float kRightPanelWidth = 360.0f;
    constexpr float kToolbarHeight = 42.0f;
    const float mainWindowWidth = ImGui::GetWindowWidth();
    const float mainWindowHeight = ImGui::GetWindowHeight();
    const bool canCreateParticleSystem = particleSystems_.size() < kMaximumParticleSystems;
    const float bottomPanelHeight = std::clamp(
        mainWindowHeight * 0.36f,
        260.0f,
        390.0f
    );
    const float topPanelHeight = std::max(
        180.0f,
        mainWindowHeight - bottomPanelHeight - kToolbarHeight - kPanelMargin * 4.0f
    );

    ImGui::SetCursorPos(ImVec2(kPanelMargin, kPanelMargin));
    ImGui::BeginChild(
        "SceneToolbar",
        ImVec2(std::max(320.0f, mainWindowWidth - kPanelMargin * 2.0f), kToolbarHeight),
        true,
        ImGuiWindowFlags_NoScrollbar
    );
    ImGui::TextUnformatted("Scene Tools");
    ImGui::SameLine();
    ImGui::TextDisabled("[F1: Hide/show all tools]");
    ImGui::SameLine();
    ImGui::BeginDisabled(!canCreateParticleSystem);
    if (ImGui::Button("Create Particle System")) {
        createParticleSystemObject();
    }
    ImGui::EndDisabled();
    if (!canCreateParticleSystem) {
        ImGui::SameLine();
        ImGui::TextWrapped("Particle System limit reached (%u)", kMaximumParticleSystems);
    }
    ImGui::SameLine();
    if (ImGui::Button(sunPresent_ ? "Remove Sun" : "Add Sun")) {
        if (sunPresent_) {
            removeSceneNodeById(sunNodeId_);
        } else {
            createSunMesh();
            rebuildSceneGeometryResources();
            selectedModelNode_ = &meshData_.additionalNodes.back();
            updateBoundingBoxVertices();
        }
        saveTransformSettings();
    }
    const bool toolbarHovered = ImGui::IsWindowHovered();
    ImGui::EndChild();

    const float topPanelY = kPanelMargin * 2.0f + kToolbarHeight;
    ImGui::SetCursorPos(ImVec2(kPanelMargin, topPanelY));
    ImGui::BeginChild(
        "SceneControls",
        ImVec2(kLeftPanelWidth, topPanelHeight),
        true,
        ImGuiWindowFlags_NoScrollbar
    );
    ImGui::TextUnformatted("VulkCore");
    ImGui::TextWrapped("Vulkan scene rendered with Dear ImGui UI");
    ImGui::Separator();
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Text("Frame time: %.2f ms", 1000.0f / std::max(ImGui::GetIO().Framerate, 0.001f));
    ImGui::TextWrapped("%s", screenshotStatus_.c_str());
    bool shouldSaveTransformSettings = false;
    ImGui::SeparatorText("Camera");
    ImGui::TextWrapped("The camera looks at the scene origin.");
    const glm::vec3 defaultCameraPosition(0.0f, 0.0f, 4.0f);
    ImGui::DragFloat3(
        "Camera position (X/Y/Z)",
        &cameraPosition_.x,
        0.01f,
        -100.0f,
        100.0f,
        "%.2f"
    );
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        if (glm::length(cameraPosition_) < 0.05f) {
            cameraPosition_ = defaultCameraPosition;
        }
        shouldSaveTransformSettings = true;
    }
    if (ImGui::Button("Reset camera position")) {
        cameraPosition_ = defaultCameraPosition;
        shouldSaveTransformSettings = true;
    }
    bool removeSelectedNodeRequested = false;
    std::uint32_t selectedNodeIdToRemove = std::numeric_limits<std::uint32_t>::max();
    ImGui::SeparatorText("Transform");
    if (selectedModelNode_ != nullptr) {
        ImGui::SeparatorText("Selected Node");
        ImGui::TextWrapped("Node: %s", selectedModelNode_->name.c_str());
        selectedNodeIdToRemove = selectedModelNode_->nodeId;
        ImGui::TextWrapped("Remove this node and all of its children from the scene.");
        if (ImGui::Button("Remove from Scene")) {
            removeSelectedNodeRequested = true;
        }
        bool renderEnabled = selectedModelNode_->renderEnabled;
        if (ImGui::Checkbox("Render Enabled", &renderEnabled)) {
            selectedModelNode_->renderEnabled = renderEnabled;
            shouldSaveTransformSettings = true;
        }
        if (!renderEnabled) {
            ImGui::TextWrapped("Rendering is disabled for this node and its children.");
        }
        if (selectedModelNode_->objectType == SceneObjectType::ParticleSystem) {
            drawParticleSystemControls(*selectedModelNode_, shouldSaveTransformSettings);
        } else {
            if (selectedModelNode_->nodeId == sunNodeId_) {
                ImGui::TextWrapped("Generated spherical sun mesh");
            }
            const bool isRootNode = selectedModelNode_ == &meshData_.hierarchy;
            const bool isImportedModelRoot = std::any_of(
                sceneModelResources_.begin(),
                sceneModelResources_.end(),
                [this](const SceneModelResource& model) {
                    return model.rootNodeId == selectedModelNode_->nodeId;
                }
            );
            const bool isNamedMeshNode = selectedModelNode_->meshCount > 0 &&
                !selectedModelNode_->name.empty() &&
                selectedModelNode_->name != "<unnamed>";
            if (isRootNode || isImportedModelRoot || isNamedMeshNode) {
                ImGui::TextWrapped(
                    isRootNode
                        ? "Transform applies to the scene root."
                        : isImportedModelRoot
                            ? "Transform applies to this imported model."
                        : "Position is in scene units; rotation and scale use local axes."
                );
                ImGui::DragFloat3(
                    "Position (X/Y/Z)",
                    &selectedModelNode_->transformTranslation.x,
                    0.01f,
                    -10.0f,
                    10.0f,
                    "%.2f"
                );
                shouldSaveTransformSettings |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::DragFloat3(
                    "Rotation (X/Y/Z)",
                    &selectedModelNode_->transformRotationDegrees.x,
                    1.0f,
                    -360.0f,
                    360.0f,
                    "%.1f deg"
                );
                shouldSaveTransformSettings |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::SliderFloat(
                    "Scale",
                    &selectedModelNode_->transformScaleFactor,
                    kMinimumModelScaleFactor,
                    kMaximumModelScaleFactor,
                    "%.2fx"
                );
                shouldSaveTransformSettings |= ImGui::IsItemDeactivatedAfterEdit();
                if (ImGui::Button("Reset selected transform")) {
                    selectedModelNode_->transformTranslation = glm::vec3(0.0f);
                    selectedModelNode_->transformRotationDegrees = glm::vec3(0.0f);
                    selectedModelNode_->transformScaleFactor = 1.0f;
                    shouldSaveTransformSettings = true;
                }
            } else {
                ImGui::TextWrapped("Transform is available for named mesh nodes only.");
            }

            drawSkeletalAnimationControls(*selectedModelNode_, shouldSaveTransformSettings);
            ImGui::SeparatorText("Node Animation");
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
        }
    } else {
        ImGui::TextWrapped("Select a scene object to edit its transform.");
    }
    drawShaderDemoControls(shouldSaveTransformSettings);
    ImGui::Spacing();
    ImGui::TextWrapped("The 3D model remains the main viewport and is rendered before this transparent ImGui layer.");
    ImGui::SeparatorText("Environment Light");
    ImGui::ColorEdit3("Ambient color", &ambientLightColor_.x);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        ambientLightColor_ = glm::clamp(
            ambientLightColor_,
            glm::vec3(0.0f),
            glm::vec3(1.0f)
        );
        shouldSaveTransformSettings = true;
    }
    ImGui::SliderFloat(
        "Ambient intensity",
        &ambientLightIntensity_,
        0.0f,
        4.0f,
        "%.2f"
    );
    shouldSaveTransformSettings |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::ColorEdit3("Sun light color", &sunLightColor_.x);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        sunLightColor_ = glm::clamp(sunLightColor_, glm::vec3(0.0f), glm::vec3(1.0f));
        shouldSaveTransformSettings = true;
    }
    ImGui::SliderFloat("Sun light intensity", &sunLightIntensity_, 0.0f, 4.0f, "%.2f");
    shouldSaveTransformSettings |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::DragFloat3(
        "Sun light direction",
        &sunLightDirection_.x,
        0.01f,
        -1.0f,
        1.0f,
        "%.2f"
    );
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        if (glm::length(sunLightDirection_) < 0.001f) {
            sunLightDirection_ = glm::vec3(0.45f, 0.55f, 0.75f);
        } else {
            sunLightDirection_ = glm::normalize(sunLightDirection_);
        }
        shouldSaveTransformSettings = true;
    }
    if (ImGui::Button("Reset environment light")) {
        ambientLightColor_ = glm::vec3(0.18f, 0.21f, 0.24f);
        ambientLightIntensity_ = 1.0f;
        sunLightDirection_ = glm::vec3(0.45f, 0.55f, 0.75f);
        sunLightColor_ = glm::vec3(0.82f, 0.86f, 0.9f);
        sunLightIntensity_ = 1.0f;
        shouldSaveTransformSettings = true;
    }
    const bool leftPanelHovered = ImGui::IsWindowHovered();
    ImGui::EndChild();

    if (removeSelectedNodeRequested) {
        shouldSaveTransformSettings |= removeSceneNodeById(selectedNodeIdToRemove);
    }

    ImGui::SetCursorPos(ImVec2(
        std::max(kPanelMargin, mainWindowWidth - kRightPanelWidth - kPanelMargin),
        topPanelY
    ));
    ImGui::BeginChild(
        "ModelHierarchy",
        ImVec2(kRightPanelWidth, topPanelHeight),
        true,
        ImGuiWindowFlags_NoScrollbar
    );
    ImGui::TextUnformatted("Scene Objects");
    ImGui::TextWrapped("Imported model and generated GameObjects");
    ImGui::Separator();
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (!meshData_.hierarchy.name.empty()) {
        drawModelHierarchy(meshData_.hierarchy);
    }
    for (ModelNode& node : meshData_.additionalNodes) {
        drawModelHierarchy(node);
    }
    if (meshData_.hierarchy.name.empty() && meshData_.additionalNodes.empty()) {
        ImGui::TextWrapped("No model hierarchy available");
    }
    const bool rightPanelHovered = ImGui::IsWindowHovered();
    ImGui::EndChild();

    const float resourcePanelY = topPanelY + topPanelHeight + kPanelMargin;
    ImGui::SetCursorPos(ImVec2(kPanelMargin, resourcePanelY));
    ImGui::BeginChild(
        "ResourceManagerPanel",
        ImVec2(
            std::max(320.0f, mainWindowWidth - kPanelMargin * 2.0f),
            std::max(160.0f, mainWindowHeight - resourcePanelY - kPanelMargin)
        ),
        true,
        ImGuiWindowFlags_NoScrollbar
    );
    drawResourceManager(shouldSaveTransformSettings);
    const bool resourcePanelHovered = ImGui::IsWindowHovered();
    ImGui::EndChild();

    if (shouldSaveTransformSettings) {
        saveTransformSettings();
    }

    if (selectedModelNode_ != nullptr &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !leftPanelHovered &&
        !rightPanelHovered &&
        !toolbarHovered &&
        !resourcePanelHovered &&
        ImGui::IsWindowHovered()) {
        selectedModelNode_ = nullptr;
        updateBoundingBoxVertices();
    }
    ImGui::End();

    drawMiniMap();
    ImGui::Render();
}


} // namespace VulkCore
