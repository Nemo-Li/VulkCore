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
    if (!authoredView_.orbitEnabled || playerController_.active() || !fixedFrameOutput_.empty()) {
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

    const glm::vec3 up = authoredView_.enabled ? authoredView_.up : kDefaultSceneUp;
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

bool Renderer::drawMiniMap(const glm::vec2& viewportOrigin, const glm::vec2& viewportSize) {
    if (!miniMapVisible_ || !terrainScene_ || !physicsWorld_ || !playerController_.active() ||
        !fixedFrameOutput_.empty()) {
        return false;
    }

    const auto& worldMinimum = terrainScene_->boundsMinimum();
    const auto& worldMaximum = terrainScene_->boundsMaximum();
    const float worldWidth = worldMaximum.x - worldMinimum.x;
    const float worldDepth = worldMaximum.z - worldMinimum.z;
    if (!std::isfinite(worldWidth) || !std::isfinite(worldDepth) ||
        worldWidth <= std::numeric_limits<float>::epsilon() ||
        worldDepth <= std::numeric_limits<float>::epsilon()) {
        return false;
    }

    constexpr float margin = 14.0f;
    constexpr ImVec2 padding(10.0f, 8.0f);
    const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
    const float maxWidth = viewportSize.x - 2.0f * (margin + padding.x);
    const float maxHeight = viewportSize.y - 2.0f * (margin + padding.y) - lineHeight;
    const bool showCoordinates = std::min(maxWidth, maxHeight - lineHeight) >= 128.0f;
    const float canvasSide = std::min({196.0f, maxWidth, maxHeight - (showCoordinates ? lineHeight : 0.0f)});
    if (canvasSide < 64.0f) return false;

    const ImVec2 windowSize(canvasSide + 2.0f * padding.x,
        canvasSide + 2.0f * padding.y + lineHeight * (showCoordinates ? 2.0f : 1.0f));
    ImGui::SetNextWindowPos(ImVec2(viewportOrigin.x + viewportSize.x - margin - windowSize.x,
        viewportOrigin.y + viewportSize.y - margin - windowSize.y));
    ImGui::SetNextWindowSize(windowSize);
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (!ImGui::Begin("Mini Map", nullptr, flags)) {
        ImGui::End();
        ImGui::PopStyleVar(2);
        return false;
    }
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    ImGui::TextUnformatted("Mini Map");
#ifndef VULKCORE_OHOS
    if (canvasSide >= 90.0f) {
        ImGui::SameLine();
        ImGui::TextDisabled("[M]");
    }
#endif
    const ImVec2 canvasMinimum = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##MiniMapCanvas", ImVec2(canvasSide, canvasSide));
    const bool mapHovered = ImGui::IsItemHovered();
    const bool mapClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

    const float scale = canvasSide / std::max(worldWidth, worldDepth);
    const ImVec2 mapSize(worldWidth * scale, worldDepth * scale);
    const ImVec2 mapMinimum(canvasMinimum.x + (canvasSide - mapSize.x) * 0.5f,
        canvasMinimum.y + (canvasSide - mapSize.y) * 0.5f);
    const ImVec2 mapMaximum(mapMinimum.x + mapSize.x, mapMinimum.y + mapSize.y);
    const auto worldToMap = [&](float x, float z) {
        return ImVec2(mapMinimum.x + (x - worldMinimum.x) * scale,
            mapMinimum.y + (z - worldMinimum.z) * scale);
    };

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(canvasMinimum,
        ImVec2(canvasMinimum.x + canvasSide, canvasMinimum.y + canvasSide), IM_COL32(10, 18, 18, 255), 5.0f);
    drawList->PushClipRect(mapMinimum, mapMaximum, true);
    const float heightRange = std::max(worldMaximum.y - worldMinimum.y, 0.0001f);
    constexpr glm::vec3 lowColor(0.10f, 0.27f, 0.13f);
    constexpr glm::vec3 middleColor(0.33f, 0.52f, 0.19f);
    constexpr glm::vec3 highColor(0.52f, 0.50f, 0.38f);
    for (const TerrainChunk& chunk : terrainScene_->chunks()) {
        for (const TerrainTile& tile : chunk.tiles()) {
            const auto& minimum = tile.boundsMinimum();
            const auto& maximum = tile.boundsMaximum();
            const auto start = worldToMap(minimum.x, minimum.z);
            const auto end = worldToMap(maximum.x, maximum.z);
            const float height = std::clamp(((minimum.y + maximum.y) * 0.5f - worldMinimum.y) / heightRange,
                0.0f, 1.0f);
            const glm::vec3 color = height < 0.58f ? glm::mix(lowColor, middleColor, height / 0.58f)
                : glm::mix(middleColor, highColor, (height - 0.58f) / 0.42f);
            drawList->AddRectFilled(start, end, ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, 1.0f)));
            drawList->AddRect(start, end, IM_COL32(220, 238, 191, 24));
        }
    }

    const auto& position = playerController_.position();
    const auto marker = worldToMap(position.x, position.z);
    const float yaw = playerController_.facingYawRadians();
    const ImVec2 facing(std::sin(yaw), std::cos(yaw));
    const ImVec2 right(-facing.y, facing.x);
    drawList->AddCircle(marker, 7.0f, IM_COL32(30, 42, 28, 255), 20, 2.0f);
    drawList->AddTriangleFilled(
        ImVec2(marker.x + facing.x * 10.0f, marker.y + facing.y * 10.0f),
        ImVec2(marker.x - facing.x * 4.0f + right.x * 5.0f, marker.y - facing.y * 4.0f + right.y * 5.0f),
        ImVec2(marker.x - facing.x * 4.0f - right.x * 5.0f, marker.y - facing.y * 4.0f - right.y * 5.0f),
        IM_COL32(255, 238, 102, 255));
    drawList->AddText(ImVec2(mapMinimum.x + 5.0f, mapMinimum.y + 4.0f), IM_COL32(240, 244, 225, 210), "N");
    drawList->PopClipRect();
    drawList->AddRect(mapMinimum, mapMaximum, IM_COL32(223, 238, 205, 185), 3.0f, 0, 1.4f);

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (mapHovered && mouse.x >= mapMinimum.x && mouse.x < mapMaximum.x &&
        mouse.y >= mapMinimum.y && mouse.y < mapMaximum.y) {
        const glm::vec2 destination(worldMinimum.x + (mouse.x - mapMinimum.x) / scale,
            worldMinimum.z + (mouse.y - mapMinimum.y) / scale);
        if (terrainScene_->renderedHeightAtWorld(destination)) {
            ImGui::SetTooltip("Travel to X %.1f, Z %.1f", destination.x, destination.y);
            if (mapClicked && playerController_.teleport(*terrainScene_, *physicsWorld_, destination)) {
                updatePlayerCamera();
            }
        } else {
            ImGui::SetTooltip("No traversable terrain here");
        }
    }

    if (showCoordinates) {
        if (playerController_.debugModeEnabled() && canvasSide >= 174.0f) {
            ImGui::Text("X %.0f  Y %.0f  Z %.0f", position.x, position.y, position.z);
        } else {
            ImGui::Text("X %.0f  Z %.0f", position.x, position.z);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    return hovered;
}

bool Renderer::buildImGuiFrame() {
    ImGui_ImplVulkan_NewFrame();
    newWindowImGuiFrame(context_->window(), swapChain_->extent());
    ImGui::NewFrame();

    if (mode_ == Mode::ModelViewer) {
        return drawModelViewerUi();
    }

    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
        toolUiVisible_ = !toolUiVisible_;
        if (!toolUiVisible_) sceneEditMode_ = false;
    }
    const auto& io = ImGui::GetIO();
    const bool sceneShortcuts = io.KeyMods == 0 && !io.WantTextInput &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
#ifndef VULKCORE_OHOS
    if (sceneShortcuts && fixedFrameOutput_.empty() && ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
        sceneEditMode_ = !sceneEditMode_;
        if (sceneEditMode_) toolUiVisible_ = true;
    }
#endif
    if (playerController_.active() && ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
        playerController_.setDebugModeEnabled(!playerController_.debugModeEnabled());
    }
    if (terrainScene_ && playerController_.active() && sceneShortcuts &&
        !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_M, false)) {
        miniMapVisible_ = !miniMapVisible_;
    }
    if (transitionSettings_.enabled) {
        const bool held = !sceneEditMode_ && !playerController_.debugModeEnabled() &&
            ImGui::IsKeyDown(ImGuiKey_Space) &&
            (cameraOrbitDragging_ || !ImGui::IsAnyItemActive());
        if (held != transitionHeld_) {
            transitionHeld_ = held;
            transitionPlayer_.loop = false;
            transitionPlayer_.play(held, transitionSettings_);
        }
    }
    bool viewportHovered = !io.WantCaptureMouse;
#ifndef VULKCORE_OHOS
    if (toolUiVisible_) {
        viewportHovered = drawGameEditor();
    } else
#endif
    {
        const auto* viewport = ImGui::GetMainViewport();
        glm::vec2 viewportSize(viewport->WorkSize.x, viewport->WorkSize.y);
#ifdef VULKCORE_OHOS
        // Leave room for the touch controls along the bottom of the screen.
        viewportSize.y *= 0.75f;
#endif
        if (drawMiniMap({viewport->WorkPos.x, viewport->WorkPos.y}, viewportSize)) viewportHovered = false;
    }
    updateCameraOrbit(viewportHovered);
    return viewportHovered;
}


} // namespace VulkCore
