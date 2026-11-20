#include "RendererImplementation.hpp"

#include "vulkcore/Terrain/Terrain.hpp"

namespace VulkCore {

void Renderer::initializeImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    if (!initializeWindowImGui(context_->window())) {
        ImGui::DestroyContext();
        throw std::runtime_error("failed to initialize Dear ImGui platform backend!");
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
        shutdownWindowImGui();
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
    shutdownWindowImGui();
    ImGui::DestroyContext();
    imguiInitialized_ = false;
}

void Renderer::drawParticleSystemControls(std::uint32_t nodeId, bool& shouldSaveSettings, ModelNode* node) {
    const auto systemIt = particleSystems_.find(nodeId);
    if (systemIt == particleSystems_.end()) {
        ImGui::TextWrapped("Particle component is not available for this object.");
        return;
    }
    ParticleSystem& particleSystem = systemIt->second;
    ParticleSystemSettings& settings = particleSystem.settings();
    bool settingsChanged = false;
    if (particleTexturePathNodeId_ != nodeId) {
        particleTexturePathNodeId_ = nodeId;
        particleTexturePathBuffer_.fill('\0');
        const std::string path = settings.texturePath.string();
        const std::size_t copyCount = std::min(
            path.size(),
            particleTexturePathBuffer_.size() - 1
        );
        std::copy_n(path.data(), copyCount, particleTexturePathBuffer_.data());
    }

    if (node) {
        ImGui::SeparatorText("GameObject Transform");
        ImGui::DragFloat3(
            "Object position",
            &node->transformTranslation.x,
            0.01f,
            -2.0f,
            2.0f,
            "%.2f"
        );
        shouldSaveSettings |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat3(
            "Object rotation",
            &node->transformRotationDegrees.x,
            1.0f,
            -360.0f,
            360.0f,
            "%.1f deg"
        );
        shouldSaveSettings |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat(
            "Object scale",
            &node->transformScaleFactor,
            kMinimumModelScaleFactor,
            kMaximumModelScaleFactor,
            "%.2fx"
        );
        shouldSaveSettings |= ImGui::IsItemDeactivatedAfterEdit();
    }

    bool enabled = settings.enabled;
    if (ImGui::Checkbox("Enabled", &enabled)) {
        particleSystem.setEnabled(enabled);
        shouldSaveSettings = true;
    }
    ImGui::SameLine();
    ImGui::Text("Alive: %zu", particleSystem.aliveCount());
    if (node && ImGui::Checkbox("Play on awake", &settings.playOnAwake)) {
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
    ImGui::SliderFloat("Emission rate", &settings.emissionRate, 0.0f, 1000.0f, "%.1f /s");
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
    if (node) {
        ImGui::DragFloat3(
            "Position",
            &settings.position.x,
            0.01f,
            -2.0f,
            2.0f,
            "%.2f"
        );
        settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();
    }

    ImGui::SeparatorText("Shape");
    const char* shapeLabels[] = {"Point", "Sphere", "Cone", "Tornado", "Ring"};
    int shape = static_cast<int>(settings.shape);
    if (ImGui::Combo("Emitter shape", &shape, shapeLabels, IM_ARRAYSIZE(shapeLabels))) {
        settings.shape = static_cast<ParticleSystemSettings::Shape>(shape);
        settingsChanged = true;
    }
    ImGui::SliderFloat("Spread", &settings.spreadDegrees, 0.0f, 89.0f, "%.1f deg");
    settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();
    if (nodeId != kPlayerLightningParticleNodeId) {
        ImGui::DragFloat3(
            "Direction",
            &settings.emissionDirection.x,
            0.01f,
            -1.0f,
            1.0f,
            "%.2f"
        );
        settingsChanged |= ImGui::IsItemDeactivatedAfterEdit();
    }
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
        loadParticleTexture(nodeId);
        shouldSaveSettings = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear particle texture")) {
        settings.texturePath.clear();
        particleTexturePathBuffer_.fill('\0');
        loadParticleTexture(nodeId);
        shouldSaveSettings = true;
    }
    if (particleTextures_.contains(nodeId)) {
        ImGui::TextWrapped("Texture loaded; RGBA is multiplied by particle color.");
    } else {
        ImGui::TextWrapped("No valid texture; using the soft-circle fallback.");
    }
    if (node && ImGui::Button("Reset particle settings")) {
        particleSystem.setSettings(ParticleSystemSettings{});
        particleTexturePathBuffer_.fill('\0');
        loadParticleTexture(nodeId);
        shouldSaveSettings = true;
    }
    if (settingsChanged) {
        particleSystem.setSettings(ParticleSystem::sanitizeSettings(settings));
        shouldSaveSettings = true;
    }
}

void Renderer::updateTransition(float seconds) {
    if (!transitionSettings_.enabled || !fixedFrameOutput_.empty()) return;
    transitionPlayer_.update(seconds, transitionSettings_);
    if (!authoredView_.enabled || transitionSettings_.activeFov <= 0.0f) return;

    const float amount = std::min(seconds, 0.1f) * 1.5f;
    transitionCameraFov_ = std::lerp(transitionCameraFov_,
        transitionPlayer_.active ? transitionSettings_.activeFov : authoredView_.fov, amount);
    // A player camera's position is constrained by terrain collision.
    if (playerController_.active()) return;

    transitionCameraOffset_ = std::lerp(transitionCameraOffset_,
        transitionPlayer_.active ? transitionSettings_.distanceOffset : 0.0f, amount);
    transitionCameraDistance_ = std::lerp(transitionCameraDistance_,
        transitionCameraBaseDistance_ + transitionCameraOffset_, 0.048f);
    const glm::vec3 direction = cameraPosition_ - cameraTarget_;
    if (glm::length(direction) > 0.001f) {
        cameraPosition_ = cameraTarget_ + glm::normalize(direction) *
            std::max(0.05f, transitionCameraDistance_);
    }
}

void Renderer::updateCameraOrbit(bool viewportHovered) {
    if (!authoredView_.enabled || !authoredView_.orbitEnabled ||
        playerController_.active() || !fixedFrameOutput_.empty()) {
        cameraOrbitDragging_ = false;
        return;
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        cameraOrbitDragging_ = viewportHovered && !ImGui::IsAnyItemActive();
        return;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        cameraOrbitDragging_ = false;
    }
    const ImVec2 delta = ImGui::GetIO().MouseDelta;
    if (!cameraOrbitDragging_ || (delta.x == 0.0f && delta.y == 0.0f)) {
        return;
    }

    const glm::vec3 offset = cameraPosition_ - cameraTarget_;
    const float radius = glm::length(offset);
    if (radius < 0.05f) {
        return;
    }

    const glm::vec3 up = authoredView_.up;
    const float height = glm::dot(offset, up);
    glm::vec3 horizontal = offset - up * height;
    if (glm::length(horizontal) < 0.0001f) {
        const glm::vec3 reference = std::abs(up.x) < 0.9f
            ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
        horizontal = glm::cross(up, reference);
    }

    constexpr float kRadiansPerPixel = 0.005f;
    const glm::vec2 pitchLimits = glm::radians(authoredView_.orbitPitchLimits);
    const float pitch = std::clamp(
        std::asin(std::clamp(height / radius, -1.0f, 1.0f)) + delta.y * kRadiansPerPixel,
        pitchLimits.x,
        pitchLimits.y
    );
    const glm::mat4 yaw = glm::rotate(glm::mat4(1.0f), -delta.x * kRadiansPerPixel, up);
    const glm::vec3 direction(yaw * glm::vec4(glm::normalize(horizontal), 0.0f));
    cameraPosition_ = cameraTarget_ + radius * (direction * std::cos(pitch) + up * std::sin(pitch));
    cameraBasisInitialized_ = false;
}

bool Renderer::buildImGuiFrame() {
    ImGui_ImplVulkan_NewFrame();
    newWindowImGuiFrame(context_->window(), swapChain_->extent());
    ImGui::NewFrame();

    if (mode_ == Mode::ModelViewer) {
        const bool viewportHovered = drawModelViewerUi();
        ImGui::Render();
        return viewportHovered;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
        toolUiVisible_ = !toolUiVisible_;
    }
    if (playerController_.active() && ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
        playerController_.setDebugModeEnabled(!playerController_.debugModeEnabled());
    }
    if (transitionSettings_.enabled) {
        const bool held = !playerController_.debugModeEnabled() && ImGui::IsKeyDown(ImGuiKey_Space) &&
            (cameraOrbitDragging_ || !ImGui::IsAnyItemActive());
        if (held != transitionHeld_) {
            transitionHeld_ = held;
            transitionPlayer_.loop = false;
            transitionPlayer_.play(held, transitionSettings_);
        }
    }
    bool viewportHovered = !ImGui::GetIO().WantCaptureMouse;
#ifndef VULKCORE_OHOS
    if (toolUiVisible_) viewportHovered = drawGameEditor();
#endif
    updateCameraOrbit(viewportHovered);
    ImGui::Render();
    return viewportHovered;
}


} // namespace VulkCore
