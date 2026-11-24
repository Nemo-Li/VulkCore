#include "RendererImplementation.hpp"

#ifdef VULKCORE_HAS_EFFECT_EDITOR
#include "../Platform/EffectEditorView.hpp"
#endif

#ifndef VULKCORE_OHOS
#include "../Platform/EditorFileDialog.hpp"
#include "vulkcore/Physics/PhysicsWorld.hpp"
#include "vulkcore/Terrain/Terrain.hpp"

#include <imgui_internal.h>

namespace VulkCore {
namespace {

constexpr ImVec4 kTransparent{0.0f, 0.0f, 0.0f, 0.0f};
constexpr ImVec4 kEditorBorder{0.43f, 0.43f, 0.50f, 0.50f};
constexpr ImVec4 kEditorPopup{0.055f, 0.065f, 0.08f, 0.86f};
constexpr float kPanelMargin = 16.0f;
constexpr ImVec4 kEditorFailure{1.0f, 0.65f, 0.46f, 1.0f};

} // namespace

void Renderer::queueSceneOpen(const std::filesystem::path& path) {
    std::error_code error;
    if (!isSceneResourcePath(path) || !std::filesystem::is_regular_file(path, error)) {
        sceneStatusFailed_ = true;
        sceneStatus_ = "Choose an existing .scene file.";
        return;
    }
    pendingSceneOpen_ = normalizedPath(path);
    pendingSceneChange_ = EditorSceneChange::Open;
    editorSceneChangeConfirmed_ = false;
    toolUiVisible_ = true;
}

void Renderer::openEditorFilePicker(const char* title, EditorFileType type,
                                    const std::filesystem::path& directory,
                                    std::function<void(const std::filesystem::path&)> accept) {
#if defined(__APPLE__) || defined(_WIN32)
    const auto path = pickEditorFile(title, type, directory);
    if (!path.empty()) accept(path);
#else
    editorFilePicker_ = {title, type, directory, {}, std::move(accept)};
#endif
}

void Renderer::drawEditorFilePicker() {
#if !defined(__APPLE__) && !defined(_WIN32)
    if (!editorFilePicker_.accept) return;
    ImGui::OpenPopup("Choose file");
    ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Choose file")) return;
    auto& picker = editorFilePicker_;
    auto folder = picker.directory;
    if (folder.empty()) folder = std::filesystem::current_path();
    ImGui::TextUnformatted(picker.title.c_str());
    ImGui::TextWrapped("%s", pathUtf8(folder).c_str());
    if (ImGui::Button("Parent folder") && folder.has_parent_path()) {
        picker.directory = folder.parent_path();
    }
    std::vector<std::filesystem::path> folders, files;
    std::error_code error;
    for (std::filesystem::directory_iterator entry(folder, error), end;
         !error && entry != end; entry.increment(error)) {
        std::error_code typeError;
        if (entry->is_directory(typeError)) folders.push_back(entry->path());
        else if (entry->is_regular_file(typeError)) {
            const auto& path = entry->path();
            if (picker.type == EditorFileType::Scene && !isSceneResourcePath(path)) continue;
            if (picker.type == EditorFileType::RealtimeEffect && path.extension() != ".vfx3d") continue;
            files.push_back(path);
        }
    }
    std::sort(folders.begin(), folders.end());
    std::sort(files.begin(), files.end());
    std::filesystem::path selected;
    ImGui::BeginChild("Files", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.0f));
    for (const auto& directory : folders) {
        const auto label = pathUtf8(directory.filename()) + "/";
        if (ImGui::Selectable(label.c_str())) picker.directory = directory;
    }
    for (const auto& file : files) {
        if (ImGui::Selectable(pathUtf8(file.filename()).c_str())) selected = file;
    }
    ImGui::EndChild();
    ImGui::InputTextWithHint("##path", "File path", picker.path.data(), picker.path.size());
    if (ImGui::Button("Open") && picker.path[0] != '\0') selected = folder / picker.path.data();
    ImGui::SameLine();
    const bool cancel = ImGui::Button("Cancel");
    if (cancel || !selected.empty()) {
        auto accept = std::move(picker.accept);
        picker = {};
        if (!selected.empty()) accept(selected);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
#endif
}

bool Renderer::drawGameEditor() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("VulkCore Game Editor", nullptr, flags);
    ImGui::PopStyleVar(3);

#ifdef VULKCORE_HAS_EFFECT_EDITOR
    if (ImGui::BeginTabBar("Editor workspaces")) {
        if (ImGui::BeginTabItem("Scene")) {
            effectEditorSelected_ = false;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Effects")) {
            effectEditorSelected_ = true;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (effectEditorSelected_) {
        sceneEditorViewport_ = glm::vec4(0.0f);
        roadViewportOwnsInput_ = false;
        const auto top = ImGui::GetCursorScreenPos();
        const auto size = ImGui::GetContentRegionAvail();
        if (!effectEditorView_) effectEditorView_ = std::make_unique<EffectEditorView>(window_);
        if (!effectEditorView_->show(top.x - viewport->Pos.x, top.y - viewport->Pos.y, size.x, size.y)) {
            ImGui::TextWrapped("Effect editor assets unavailable. Build the VulkCoreEffectEditor target.");
        }
        ImGui::End();
        return false;
    }
    if (effectEditorView_) effectEditorView_->hide();
#endif

    const auto available = ImGui::GetContentRegionAvail();
    const auto origin = ImGui::GetCursorScreenPos();
    const float margin = std::min(kPanelMargin, std::min(available.x, available.y) * 0.05f);
    const float width = std::max(1.0f, available.x - 2.0f * margin);
    const float toolbarHeight = ImGui::GetFrameHeight() + 2.0f;
    const float panelY = origin.y + toolbarHeight + 2.0f * margin;
    const bool resourcesAvailable = !resourceBrowser_.root.empty();
    const float resourceHeight = !resourcesAvailable ? 0.0f :
        (resourceBrowser_.open ? std::min(370.0f, available.y * 0.44f) : toolbarHeight + 20.0f);
    const float panelHeight = std::max(1.0f, available.y - toolbarHeight - 3.0f * margin -
        (resourcesAvailable ? resourceHeight + margin : 0.0f));
    const float left = std::min(235.0f, width * 0.24f);
    const float right = std::min(340.0f, width * 0.35f);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, kTransparent);
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, kTransparent);
    ImGui::PushStyleColor(ImGuiCol_Border, kEditorBorder);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, kEditorPopup);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kEditorPopup);

    const auto newScene = [&] {
        pendingSceneOpen_.clear();
        pendingSceneChange_ = EditorSceneChange::Create;
        editorSceneChangeConfirmed_ = false;
    };
    const auto openScene = [&] {
        openEditorFilePicker("Open Scene", EditorFileType::Scene, currentScenePath_.empty() ?
            scenesDirectory_ : currentScenePath_.parent_path(),
            [this](const auto& path) { queueSceneOpen(path); });
    };
    const bool canSave = editorDirty_ || !currentScenePath_.empty();
    const auto saveScene = [&] {
        if (saveSceneResource(currentScenePath_)) editorDirty_ = false;
    };
    const bool modalOpen = ImGui::IsPopupOpen(nullptr,
        ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    const auto& io = ImGui::GetIO();
#ifdef __APPLE__
    const bool shortcut = io.KeySuper;
    constexpr const char* newSceneShortcut = "Cmd+N";
#else
    const bool shortcut = io.KeyCtrl;
    constexpr const char* newSceneShortcut = "Ctrl+N";
#endif
    if (shortcut && !io.WantTextInput && !modalOpen) {
        if (ImGui::IsKeyPressed(ImGuiKey_N, false)) newScene();
        else if (ImGui::IsKeyPressed(ImGuiKey_O, false)) openScene();
        else if (canSave && ImGui::IsKeyPressed(ImGuiKey_S, false)) saveScene();
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + margin, origin.y + margin));
    constexpr ImGuiWindowFlags toolbarFlags = ImGuiWindowFlags_MenuBar |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::BeginChild("SceneToolbar", ImVec2(width, toolbarHeight),
            ImGuiChildFlags_Borders, toolbarFlags) && ImGui::BeginMenuBar()) {
        if (ImGui::MenuItem("New Scene", newSceneShortcut)) newScene();
        if (ImGui::MenuItem("Open Scene...")) openScene();
        if (ImGui::MenuItem("Save", nullptr, false, canSave)) saveScene();
        if (ImGui::MenuItem("Import Realtime Effect...")) {
            openEditorFilePicker("Select the exported .vfx3d file", EditorFileType::RealtimeEffect,
                currentScenePath_.empty() ? scenesDirectory_ : currentScenePath_.parent_path(),
                [this](const auto& path) { importRealtimeEffect(path); });
        }
        ImGui::MenuItem("Edit Mode", "F5", &sceneEditMode_);
        if (sceneEditMode_ && terrainScene_ && ImGui::BeginMenu("Edit Tool")) {
            if (ImGui::MenuItem("Road Brush", nullptr, roadBrushEnabled_)) roadBrushEnabled_ = true;
            if (ImGui::MenuItem("Select / Transform", nullptr, !roadBrushEnabled_)) roadBrushEnabled_ = false;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("Capture screenshot", "F12")) screenshotFromToolbar_ = true;
            if (ImGui::MenuItem("Hide editor", "F3")) {
                toolUiVisible_ = false;
                sceneEditMode_ = false;
            }
            if (playerController_.active() && ImGui::MenuItem("Debug flight", "F2",
                playerController_.debugModeEnabled())) {
                playerController_.setDebugModeEnabled(!playerController_.debugModeEnabled());
            }
            if (terrainScene_ && playerController_.active()) {
                ImGui::MenuItem("Mini Map", "M", &miniMapVisible_);
            }
            if (terrainScene_ && ImGui::MenuItem("Terrain frustum culling", nullptr,
                terrainFrustumCullingEnabled_)) {
                terrainFrustumCullingEnabled_ = !terrainFrustumCullingEnabled_;
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        const std::string sceneName = currentScenePath_.empty()
            ? (editorDirty_ ? "Untitled" : "Drop a .scene file to open")
            : pathUtf8(currentScenePath_.filename());
        const std::string title = sceneName + (editorDirty_ ? " *" : "");
        ImGui::TextDisabled("%s", title.c_str());
        ImGui::EndMenuBar();
    }

    ImGui::EndChild();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(7, 7));
    const bool acceptModelDrop = resourcesAvailable && !modalOpen &&
        pendingSceneChange_ == EditorSceneChange::None;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + margin, panelY));
    if (ImGui::BeginChild("Outliner", ImVec2(left, panelHeight), ImGuiChildFlags_Borders)) drawGameOutliner();
    ImGui::EndChild();
    if (acceptModelDrop && ImGui::BeginDragDropTarget()) {
        acceptSceneModelDrop();
        ImGui::EndDragDropTarget();
    }
    const ImRect sceneViewport(ImVec2(origin.x + margin + left, panelY),
        ImVec2(origin.x + margin + width - right, panelY + panelHeight));
    sceneEditorViewport_ = {sceneViewport.Min.x, sceneViewport.Min.y, sceneViewport.Max.x, sceneViewport.Max.y};
    if (acceptModelDrop && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
        ImGui::BeginDragDropTargetCustom(sceneViewport, ImGui::GetID("##SceneViewport"))) {
        acceptSceneModelDrop();
        ImGui::EndDragDropTarget();
    }
    bool roadViewportHovered = false;
    roadViewportOwnsInput_ = false;
    if (roadEditorActive() && sceneViewport.GetWidth() > 0.0f) {
        // Capture the drag before ImGui treats it as a click on the window background.
        ImGui::SetCursorScreenPos(sceneViewport.Min);
        ImGui::InvisibleButton("##RoadViewport", sceneViewport.GetSize());
        roadViewportOwnsInput_ = ImGui::IsItemActive();
        roadViewportHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride);
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x + margin + width - right, panelY));
    if (ImGui::BeginChild("Inspector", ImVec2(right, panelHeight), ImGuiChildFlags_Borders)) drawGameInspector();
    ImGui::EndChild();
    if (resourcesAvailable) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + margin, origin.y + available.y - margin - resourceHeight));
        if (ImGui::BeginChild("Resource Manager", ImVec2(width, resourceHeight),
                ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            drawResourceBrowser();
        }
        ImGui::EndChild();
    }
    ImGui::PopStyleVar(3);

    if (pendingSceneChange_ != EditorSceneChange::None && editorDirty_ && !editorSceneChangeConfirmed_) {
        ImGui::OpenPopup("Unsaved scene changes");
    }
    if (ImGui::BeginPopupModal("Unsaved scene changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The current scene has unsaved changes.");
        const bool creating = pendingSceneChange_ == EditorSceneChange::Create;
        if (ImGui::Button(creating ? "Save and create" : "Save and open")) {
            saveScene();
            if (!editorDirty_) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard changes")) {
            editorSceneChangeConfirmed_ = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            pendingSceneChange_ = EditorSceneChange::None;
            pendingSceneOpen_.clear();
            ImGui::CloseCurrentPopup();
        }
        if (sceneStatusFailed_) {
            ImGui::PushStyleColor(ImGuiCol_Text, kEditorFailure);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 360.0f);
            ImGui::TextUnformatted(sceneStatus_.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        ImGui::EndPopup();
    }
    drawEditorFilePicker();
    drawEditorShaderDialog();
    // The root covers the game. Hovering a child or a popup belongs to the editor.
    const bool hovered = ImGui::IsWindowHovered();
    ImGui::PopStyleColor(5);
    ImGui::End();
    const bool miniMapHovered = drawMiniMap({sceneViewport.Min.x, sceneViewport.Min.y},
        {sceneViewport.GetWidth(), sceneViewport.GetHeight()});
    return (roadEditorActive() ? roadViewportHovered : hovered) && !miniMapHovered;
}

glm::vec3 Renderer::updateGameEditorSelection(float deltaSeconds, bool viewportHovered) {
    const auto& io = ImGui::GetIO();
    if (!sceneEditMode_ || roadEditorActive() || editorSelection_ != EditorSelection::Node || !selectedModelNode_ ||
        pendingSceneChange_ != EditorSceneChange::None || pendingSceneNodeRemoval_ ||
        io.KeyMods != 0 || io.WantTextInput || ImGui::IsAnyItemActive() || ImGui::GetDragDropPayload() ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        return glm::vec3(0.0f);
    }

    auto& node = *selectedModelNode_;
    const auto id = node.nodeId;
    const bool terrain = id >= kTerrainSceneNodeId && id < kPlayerSceneNodeId;
    const bool grass = id == kGrassSceneNodeId || grassLodForLayer(id - kGrassLodNodeBase).has_value();
    if (terrain || grass || id == kTornadoPreviewNodeId ||
        (&node == &meshData_.hierarchy && node.name == "Scene")) {
        return glm::vec3(0.0f);
    }

    const ImRect viewport({sceneEditorViewport_.x, sceneEditorViewport_.y},
        {sceneEditorViewport_.z, sceneEditorViewport_.w});
    if (viewportHovered && viewport.Contains(io.MousePos) && io.MouseWheel != 0.0f) {
        const float scale = std::clamp(node.transformScaleFactor * std::exp(0.12f * io.MouseWheel),
            kMinimumModelScaleFactor,
            node.objectType == SceneObjectType::RealtimeEffect ? kMaximumRealtimeEffectScale : kMaximumModelScaleFactor);
        if (scale != node.transformScaleFactor) {
            node.transformScaleFactor = scale;
            editorDirty_ = true;
        }
    }

    const auto axis = [](ImGuiKey positive, ImGuiKey negative) {
        return static_cast<float>(ImGui::IsKeyDown(positive)) - static_cast<float>(ImGui::IsKeyDown(negative));
    };
    const glm::vec2 input(axis(ImGuiKey_D, ImGuiKey_A), axis(ImGuiKey_W, ImGuiKey_S));
    if (input == glm::vec2(0.0f) || deltaSeconds <= 0.0f) return glm::vec3(0.0f);

    const glm::vec3 up = authoredView_.enabled ? authoredView_.up :
        (playerController_.active() ? glm::vec3(0.0f, 1.0f, 0.0f) : kDefaultSceneUp);
    glm::vec3 forward = cameraForward_ - up * glm::dot(cameraForward_, up);
    if (glm::length(forward) < 0.0001f) {
        forward = glm::cross(up, cameraRight_);
    }
    forward = glm::normalize(forward);
    const glm::vec3 right = glm::cross(forward, up);
    // Scale speed with the viewing distance so it feels similar across scene sizes.
    const float speed = glm::length(cameraTarget_ - cameraPosition_) * 0.85f;
    const glm::vec3 movement = glm::normalize(right * input.x + forward * input.y) * speed * deltaSeconds;

    if (node.objectType == SceneObjectType::Player && playerController_.active()) {
        // Keep the physics capsule and the saved spawn in sync with the edited player position.
        const glm::vec3 destination = playerController_.position() + movement / modelScale_;
        if (playerController_.moveSpawn(*terrainScene_, *physicsWorld_, {destination.x, destination.z})) {
            playerSpawnPosition_ = playerController_.spawnPosition();
            editorDirty_ = true;
            updatePlayerCamera();
            updateCameraBasis();
        }
        return glm::vec3(0.0f);
    }
    if (node.objectType == SceneObjectType::RealtimeEffect) {
        node.transformTranslation += movement / modelScale_;
        editorDirty_ = true;
        return glm::vec3(0.0f);
    }
    if (node.objectType == SceneObjectType::ParticleSystem) {
        // Particle objects store translation directly in scene units.
        node.transformTranslation += movement / modelScale_;
        editorDirty_ = true;
        return glm::vec3(0.0f);
    }
    return movement;
}

void Renderer::drawGameSelectionOutline(const glm::mat4& clipFromScene) {
    if (!sceneEditMode_ || editorSelection_ != EditorSelection::Node || !selectedModelNode_) return;
    const auto& node = *selectedModelNode_;
    if (!isSceneNodeRenderEnabled(node.nodeId)) return;

    glm::vec3 minimum = node.boundsMinimum;
    glm::vec3 maximum = node.boundsMaximum;
    glm::mat4 transform = transformForSceneNode(node.nodeId);
    if (editorPart_ && *editorPart_ < meshData_.parts.size()) {
        const auto& part = meshData_.parts[*editorPart_];
        if (part.indexCount > 0) {
            minimum = glm::vec3(std::numeric_limits<float>::max());
            maximum = -minimum;
            for (std::size_t i = part.firstIndex, end = i + part.indexCount; i < end; ++i) {
                const auto& position = meshData_.vertices[meshData_.indices[i]].position;
                minimum = glm::min(minimum, position);
                maximum = glm::max(maximum, position);
            }
        } else if (!node.hasBounds) {
            return;
        }
    } else if (node.objectType == SceneObjectType::ParticleSystem) {
        const auto system = particleSystems_.find(node.nodeId);
        if (system == particleSystems_.end()) return;
        const auto& settings = system->second.settings();
        const float radius = std::max({settings.startSize, settings.shapeRadius, 0.01f});
        minimum = settings.position - glm::vec3(radius);
        maximum = settings.position + glm::vec3(radius);
        for (const auto& vertex : system->second.vertices()) {
            minimum = glm::min(minimum, vertex.position);
            maximum = glm::max(maximum, vertex.position);
        }
        transform = particleObjectTransform(node);
    } else if (!node.hasBounds) {
        return;
    }

    const glm::mat4 clipFromNode = clipFromScene * transform;
    const std::array<glm::vec4, 8> corners = {{
        clipFromNode * glm::vec4(minimum.x, minimum.y, minimum.z, 1.0f),
        clipFromNode * glm::vec4(maximum.x, minimum.y, minimum.z, 1.0f),
        clipFromNode * glm::vec4(maximum.x, maximum.y, minimum.z, 1.0f),
        clipFromNode * glm::vec4(minimum.x, maximum.y, minimum.z, 1.0f),
        clipFromNode * glm::vec4(minimum.x, minimum.y, maximum.z, 1.0f),
        clipFromNode * glm::vec4(maximum.x, minimum.y, maximum.z, 1.0f),
        clipFromNode * glm::vec4(maximum.x, maximum.y, maximum.z, 1.0f),
        clipFromNode * glm::vec4(minimum.x, maximum.y, maximum.z, 1.0f)
    }};
    const auto* viewport = ImGui::GetMainViewport();
    const auto screenPoint = [viewport](const glm::vec4& point) {
        const glm::vec2 ndc = glm::vec2(point) / point.w;
        return ImVec2(viewport->Pos.x + (0.5f * ndc.x + 0.5f) * viewport->Size.x,
            viewport->Pos.y + (0.5f * ndc.y + 0.5f) * viewport->Size.y);
    };
    // Clip each edge in homogeneous Vulkan clip space before dividing by w.
    constexpr std::array<glm::vec4, 6> planes = {{
        {1, 0, 0, 1}, {-1, 0, 0, 1}, {0, 1, 0, 1}, {0, -1, 0, 1}, {0, 0, 1, 0}, {0, 0, -1, 1}
    }};
    auto* drawList = ImGui::GetBackgroundDrawList();
    drawList->PushClipRect(ImVec2(sceneEditorViewport_.x, sceneEditorViewport_.y),
        ImVec2(sceneEditorViewport_.z, sceneEditorViewport_.w), true);
    for (std::size_t edge = 0; edge < kBoundingBoxEdges.size(); edge += 2) {
        const auto& start = corners[kBoundingBoxEdges[edge]];
        const auto& end = corners[kBoundingBoxEdges[edge + 1]];
        float first = 0.0f;
        float last = 1.0f;
        for (const auto& plane : planes) {
            const float a = glm::dot(plane, start);
            const float b = glm::dot(plane, end);
            if (a < 0.0f && b < 0.0f) {
                first = 1.0f;
                last = 0.0f;
                break;
            }
            if (a < 0.0f) first = std::max(first, a / (a - b));
            else if (b < 0.0f) last = std::min(last, a / (a - b));
        }
        if (first >= last) continue;
        const glm::vec4 clippedStart = glm::mix(start, end, first);
        const glm::vec4 clippedEnd = glm::mix(start, end, last);
        if (clippedStart.w > 0.0f && clippedEnd.w > 0.0f) {
            drawList->AddLine(screenPoint(clippedStart), screenPoint(clippedEnd), IM_COL32(255, 209, 65, 255), 2.5f);
        }
    }
    drawList->PopClipRect();
}

bool Renderer::editorDeletePressed() const {
    const auto& io = ImGui::GetIO();
    if (io.KeyMods != 0 || io.WantTextInput || ImGui::IsAnyItemActive() || ImGui::GetDragDropPayload() ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        return false;
    }
#ifdef __APPLE__
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) return true;
#endif
    return ImGui::IsKeyPressed(ImGuiKey_Delete, false);
}

void Renderer::drawGameOutliner() {
    ImGui::SeparatorText("Scene");
    if (sceneEditMode_ && !roadEditorActive()) ImGui::TextDisabled("WASD Move / Wheel Scale");
    const auto select = [&](const char* label, EditorSelection selection) {
        if (ImGui::Selectable(label, editorSelection_ == selection)) {
            editorSelection_ = selection;
            selectedModelNode_ = nullptr;
            editorPart_.reset();
        }
    };
    select("Environment", EditorSelection::Environment);
    if (!currentScenePath_.empty()) select("Skybox", EditorSelection::Skybox);
    if (playerController_.active()) select("Player Settings", EditorSelection::PlayerSettings);

    const auto selectNode = [&](ModelNode& node, std::optional<std::uint32_t> part = std::nullopt) {
        editorSelection_ = EditorSelection::Node;
        editorPart_ = part;
        selectedModelNode_ = &node;
    };
    const auto drawRoot = [&](const auto& self, ModelNode& root) -> void {
        if (removedSceneNodeIds_.contains(root.nodeId) || root.name.empty() ||
            (root.children.empty() && root.meshPartIndices.empty() &&
                root.objectType != SceneObjectType::ParticleSystem &&
                root.objectType != SceneObjectType::RealtimeEffect)) return;
        ImGui::PushID(static_cast<int>(root.nodeId));
        const bool sceneRoot = &root == &meshData_.hierarchy && root.name == "Scene";
        const bool grass = root.nodeId == kGrassSceneNodeId;
        const bool expand = grass || !root.children.empty() || !root.meshPartIndices.empty();
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
        if (!expand) flags |= ImGuiTreeNodeFlags_Leaf;
        if (editorSelection_ == EditorSelection::Node && selectedModelNode_ == &root && !editorPart_) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        const auto* resource = sceneModelResourceForNode(root);
        const auto name = resource && resource->rootNodeId == root.nodeId && !resource->playerModel
            ? resource->path.stem().u8string() : std::u8string{};
        if (sceneRoot) flags |= ImGuiTreeNodeFlags_DefaultOpen;
        const bool open = ImGui::TreeNodeEx("##object", flags, "%s",
            name.empty() ? root.name.c_str() : reinterpret_cast<const char*>(name.c_str()));
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) selectNode(root);
        if (open && sceneRoot) {
            for (auto& object : root.children) self(self, object);
            ImGui::TreePop();
        } else if (open) {
            // Flatten deep authoring hierarchies into mesh rows. Every mesh remains reachable.
            const auto drawParts = [&](const auto& self, ModelNode& node) -> void {
                for (const auto part : node.meshPartIndices) {
                    if (part >= meshData_.parts.size() || removedSceneNodeIds_.contains(node.nodeId)) continue;
                    ImGui::PushID(static_cast<int>(part));
                    std::string label = node.name.empty() ? "Mesh" : node.name;
                    if (node.meshPartIndices.size() > 1) {
                        const auto index = materialIndexForPart(meshData_.parts[part]);
                        if (index < meshData_.materials.size()) label += " / " + meshData_.materials[index].name;
                    }
                    if (ImGui::Selectable(label.c_str(), editorPart_ == part)) selectNode(node, part);
                    ImGui::PopID();
                }
                for (auto& child : node.children) self(self, child);
            };
            drawParts(drawParts, root);
            if (grass) select("Grass Ground Material", EditorSelection::GrassGround);
            ImGui::TreePop();
        }
        ImGui::PopID();
    };
    drawRoot(drawRoot, meshData_.hierarchy);
    for (auto& object : meshData_.additionalNodes) drawRoot(drawRoot, object);

    if (pendingSceneChange_ == EditorSceneChange::None && ImGui::IsWindowFocused() &&
        editorSelection_ == EditorSelection::Node && selectedModelNode_ && editorDeletePressed()) {
        const auto id = selectedModelNode_->nodeId;
        if (selectedModelNode_ == &meshData_.hierarchy) {
            sceneStatusFailed_ = true;
            sceneStatus_ = "Select an object inside the scene group to remove it.";
        } else if (id >= kTerrainSceneNodeId && id < kPlayerSceneNodeId) {
            sceneStatusFailed_ = true;
            sceneStatus_ = "The terrain and its chunks are managed by the physics world.";
        } else {
            pendingSceneNodeRemoval_ = id;
        }
    }
}

void Renderer::drawEditorTexture(const char* label, const std::filesystem::path& path,
                                 const std::filesystem::path& defaultPath,
                                 std::function<void(const std::filesystem::path&)> accept) {
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    const auto& displayedPath = path.empty() ? defaultPath : path;
    ImGui::TextWrapped("%s", displayedPath.empty() ? "Procedural" : pathUtf8(displayedPath.filename()).c_str());
    if (!displayedPath.empty()) ImGui::SetItemTooltip("%s", pathUtf8(displayedPath).c_str());
    const auto apply = [this, accept = std::move(accept)](const std::filesystem::path& selected) {
        try {
            accept(normalizedPath(selected));
            editorDirty_ = true;
            sceneStatusFailed_ = false;
            sceneStatus_.clear();
        } catch (const std::exception& error) {
            sceneStatusFailed_ = true;
            sceneStatus_ = error.what();
        }
    };
    if (ImGui::Button("Replace...")) openEditorFilePicker(label, EditorFileType::Images,
        displayedPath.empty() ? assetsDirectory_ : displayedPath.parent_path(), apply);
    if (!path.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Reset")) apply({});
    }
    ImGui::PopID();
}

void Renderer::changeEditorMaterialTexture(std::uint32_t index, const std::filesystem::path& path) {
    if (!path.empty()) {
        // Decode before changing the material so unreadable files leave the current texture intact.
        const Texture candidate(*context_, path);
    }
    auto& material = meshData_.materials.at(index);
    material.texturePath = path;
    material.embeddedTexture = {};
    material.userModified = true;
    rebuildMaterialResources();
}

void Renderer::drawEditorMaterial(std::uint32_t index, std::string_view family) {
    if (index >= meshData_.materials.size()) return;
    ImGui::PushID(static_cast<int>(index));
    MaterialData& material = meshData_.materials[index];
    ImGui::SeparatorText("Graphics Pipeline");
    for (const auto stage : {ShaderStage::Vertex, ShaderStage::TessellationControl,
            ShaderStage::TessellationEvaluation, ShaderStage::Geometry, ShaderStage::Fragment}) {
        const auto& configured = material.shaderConfiguration.shader(stage);
        if (!configured.empty() || stage == ShaderStage::Vertex || stage == ShaderStage::Fragment) {
            drawEditorShader(stage, configured.empty() ? defaultShaderForStage(stage) : configured,
                family, index);
        }
    }
    ImGui::SeparatorText("Material");
    if (family == "mesh" || family == "tornado" || family == "sun") {
        drawMaterialControls(material, static_cast<std::int32_t>(index), editorDirty_);
        if (material.customParameters && ImGui::CollapsingHeader("Shader Parameters")) {
            bool changed = ImGui::DragFloat4("Parameters 0", &material.shaderParameters0.x, 0.01f);
            changed |= ImGui::DragFloat4("Parameters 1", &material.shaderParameters1.x, 0.01f);
            if (changed) {
                material.userModified = true;
                editorDirty_ = true;
            }
        }
    } else {
        bool changed = ImGui::ColorEdit4("Tint", &material.baseColor.x, ImGuiColorEditFlags_NoInputs);
        if (family == "water") {
            auto& water = material.waterShaderSettings;
            changed |= ImGui::SliderFloat("Opacity", &material.opacity, 0.0f, 1.0f);
            changed |= ImGui::DragFloat2("Flow direction", &water.flowDirection.x, 0.01f, -1.0f, 1.0f);
            changed |= ImGui::SliderFloat("Flow speed", &water.flowSpeed, 0.0f, 4.0f);
            changed |= ImGui::SliderFloat("Flow strength", &water.flowStrength, 0.0f, 4.0f);
            changed |= ImGui::SliderFloat("Turbulence", &water.flowTurbulence, 0.0f, 1.0f);
            changed |= ImGui::SliderFloat("Wave scale", &water.waveScale, 0.1f, 8.0f);
            changed |= ImGui::SliderFloat("Wave height", &water.waveHeight, 0.0f, 0.2f, "%.3f");
            drawEditorTexture("Flow texture", material.texturePath, {},
                [this, index](const auto& path) { changeEditorMaterialTexture(index, path); });
            drawEditorTexture("Normal texture", material.normalTexturePath, {},
                [this, index](const auto& path) {
                    if (!path.empty()) {
                        const Texture candidate(*context_, path);
                    }
                    auto& edited = meshData_.materials.at(index);
                    edited.normalTexturePath = path;
                    edited.embeddedNormalTexture = {};
                    edited.userModified = true;
                    rebuildMaterialResources();
                });
        }
        if (changed) {
            material.userModified = true;
            editorDirty_ = true;
        }
        if (family == "grass") {
            drawEditorTexture("Blade / card texture", material.texturePath, {},
                [this, index](const auto& path) { changeEditorMaterialTexture(index, path); });
        }
    }
    ImGui::PopID();
}

void Renderer::drawEnemyPatrol(const ModelNode& modelRoot) {
    auto entry = enemyPatrols_.find(modelRoot.nodeId);
    if (entry == enemyPatrols_.end()) {
        if (ImGui::Button("Add Patrol")) {
            enemyPatrols_.try_emplace(modelRoot.nodeId);
            editorDirty_ = true;
        }
        return;
    }
    if (!ImGui::CollapsingHeader("Enemy Patrol", ImGuiTreeNodeFlags_DefaultOpen)) return;

    auto& patrol = entry->second;
    constexpr auto clamp = ImGuiSliderFlags_AlwaysClamp;
    editorDirty_ |= ImGui::Checkbox("Patrol Enabled", &patrol.enabled);
    ImGui::TextUnformatted("Waypoint offset X/Z (m)");
    ImGui::SetNextItemWidth(-1.0f);
    editorDirty_ |= ImGui::DragFloat2("##Waypoint offset X/Z", &patrol.offset.x, 0.1f, -100.0f, 100.0f, "%.1f", clamp);
    editorDirty_ |= ImGui::DragFloat("Walk speed", &patrol.speed, 0.05f, 0.1f, 20.0f, "%.2f m/s", clamp);
    editorDirty_ |= ImGui::DragFloat("Endpoint wait", &patrol.wait, 0.1f, 0.0f, 30.0f, "%.1f s", clamp);
    editorDirty_ |= ImGui::DragFloat("Facing offset", &patrol.facingOffsetDegrees, 1.0f, -180.0f, 180.0f, "%.0f deg", clamp);

    if (auto* animation = sceneAnimation(modelRoot.nodeId)) {
        const auto selectClip = [&](const char* label, std::string& selection, bool walking) {
            const auto clip = patrolAnimationClip(meshData_, *animation, selection, walking);
            const auto* preview = clip ? meshData_.skeletalAnimations[animation->animationStart + *clip].name.c_str()
                : (selection == "-" ? "Bind pose" : "Auto");
            if (!ImGui::BeginCombo(label, preview)) return;
            if (ImGui::Selectable("Auto", selection.empty())) {
                selection.clear();
                editorDirty_ = true;
            }
            if (ImGui::Selectable("Bind pose", selection == "-")) {
                selection = "-";
                editorDirty_ = true;
            }
            for (std::uint32_t i = 0; i < animation->animationCount; ++i) {
                const auto& name = meshData_.skeletalAnimations[animation->animationStart + i].name;
                if (name.empty()) continue;
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(name.c_str(), selection == name)) {
                    selection = name;
                    editorDirty_ = true;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        };
        selectClip("Walking clip", patrol.walkClip, true);
        selectClip("Waiting clip", patrol.idleClip, false);
    }
}

void Renderer::drawGameInspector() {
    if (roadEditorActive()) {
        drawRoadBrushControls();
        return;
    }
    ImGui::SeparatorText("Inspector");
    ImGui::PushItemWidth(std::max(65.0f, ImGui::GetContentRegionAvail().x * 0.53f));
    const auto draw = [&] {
        if (editorSelection_ == EditorSelection::Environment) {
            ImGui::SeparatorText("Lighting");
            editorDirty_ |= ImGui::ColorEdit3("Ambient color", &ambientLightColor_.x, ImGuiColorEditFlags_NoInputs);
            editorDirty_ |= ImGui::SliderFloat("Ambient intensity", &ambientLightIntensity_, 0.0f, 4.0f);
            editorDirty_ |= ImGui::ColorEdit3("Sunlight color", &sunLightColor_.x, ImGuiColorEditFlags_NoInputs);
            editorDirty_ |= ImGui::SliderFloat("Sunlight intensity", &sunLightIntensity_, 0.0f, 4.0f);
            auto direction = sunLightDirection_;
            if (ImGui::DragFloat3("Sunlight direction", &direction.x, 0.01f, -1.0f, 1.0f) &&
                glm::length(direction) > 0.001f) {
                sunLightDirection_ = direction;
                editorDirty_ = true;
            }
            ImGui::SeparatorText("Distance Fog");
            auto& fog = distanceFogSettings_;
            editorDirty_ |= ImGui::Checkbox("Enabled", &fog.enabled);
            editorDirty_ |= ImGui::DragFloat("Start", &fog.startDistance, 1.0f, 0.0f, 20000.0f);
            editorDirty_ |= ImGui::DragFloat("Half distance", &fog.halfDistance, 1.0f, 1.0f, 20000.0f);
            editorDirty_ |= ImGui::SliderFloat("Max opacity", &fog.maxOpacity, 0.0f, 1.0f);
            editorDirty_ |= ImGui::ColorEdit3("Horizon", &fog.horizonColor.x, ImGuiColorEditFlags_NoInputs);
            editorDirty_ |= ImGui::ColorEdit3("Upper sky", &fog.upperColor.x, ImGuiColorEditFlags_NoInputs);
            if (terrainScene_) {
                editorDirty_ |= ImGui::SliderFloat("Floating mist", &fog.floatingMistOpacity,
                    0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            }
            if (hdrComposition_ && ImGui::CollapsingHeader("HDR")) {
                auto& hdr = hdrSettings_;
                editorDirty_ |= ImGui::DragFloat("Exposure", &hdr.exposure, 0.01f, 0.01f, 10.0f);
                editorDirty_ |= ImGui::DragFloat("Bloom threshold", &hdr.bloomThreshold, 0.01f, 0.0f, 10.0f);
                editorDirty_ |= ImGui::DragFloat("Bloom smoothing", &hdr.bloomSmoothing, 0.01f, 0.0f, 1.0f);
                editorDirty_ |= ImGui::DragFloat("Bloom intensity", &hdr.bloomIntensity, 0.01f, 0.0f, 10.0f);
                editorDirty_ |= ImGui::DragFloat("Bloom radius", &hdr.bloomRadius, 0.01f, 0.0f, 4.0f);
                editorDirty_ |= ImGui::ColorEdit3("Clear color", &hdr.clearColor.x, ImGuiColorEditFlags_NoInputs);
            }
            return;
        }
        if (editorSelection_ == EditorSelection::Skybox) {
            ImGui::SeparatorText("Skybox");
            editorDirty_ |= ImGui::Checkbox("Visible", &skyboxEnabled_);
            drawEditorTexture("Sky palette", skyboxTexturePath_, defaultSkyboxPath(),
                [this](const auto& path) { applySkyboxTexture(path); });
            ImGui::SeparatorText("Graphics Pipeline");
            drawEditorShader(ShaderStage::Vertex, "shader_skybox.vert", "skybox");
            drawEditorShader(ShaderStage::Fragment, "shader_skybox.frag", "skybox");
            return;
        }
        const auto drawFirstMaterial = [&](const ModelNode& node, std::string_view family) {
            const auto firstPart = [&](const auto& self, const ModelNode& current) -> const MeshPart* {
                if (!current.meshPartIndices.empty() && current.meshPartIndices.front() < meshData_.parts.size()) {
                    return &meshData_.parts[current.meshPartIndices.front()];
                }
                for (const auto& child : current.children) {
                    if (const auto* part = self(self, child)) return part;
                }
                return nullptr;
            };
            if (const auto* part = firstPart(firstPart, node)) drawEditorMaterial(materialIndexForPart(*part), family);
        };
        const auto drawGrassGrowth = [&] {
            float height = grassHeightScale_ * kDefaultGrassHeight;
            if (ImGui::SliderFloat("Average height", &height,
                0.25f * kDefaultGrassHeight, 3.0f * kDefaultGrassHeight, "%.2f m",
                ImGuiSliderFlags_AlwaysClamp)) {
                grassHeightScale_ = height / kDefaultGrassHeight;
                editorDirty_ = true;
            }
            constexpr float centimetersPerMeter = 100.0f;
            float minimumWidth = grassGrowth_.minimumBladeWidth * centimetersPerMeter;
            float maximumWidth = grassGrowth_.maximumBladeWidth * centimetersPerMeter;
            if (ImGui::DragFloatRange2("Base width range", &minimumWidth, &maximumWidth, 0.05f,
                GrassGrowthSettings::kMinBladeWidth * centimetersPerMeter,
                GrassGrowthSettings::kMaxBladeWidth * centimetersPerMeter,
                "%.1f cm", nullptr, ImGuiSliderFlags_AlwaysClamp)) {
                grassGrowth_.minimumBladeWidth = minimumWidth / centimetersPerMeter;
                grassGrowth_.maximumBladeWidth = maximumWidth / centimetersPerMeter;
                editorDirty_ = true;
            }
            editorDirty_ |= ImGui::DragFloatRange2("Tilt range", &grassGrowth_.minimumTilt,
                &grassGrowth_.maximumTilt, 0.25f, 0.0f, GrassGrowthSettings::kMaxTilt,
                "%.1f deg", nullptr, ImGuiSliderFlags_AlwaysClamp);
            editorDirty_ |= ImGui::SliderFloat("Direction noise size", &grassGrowth_.noiseSize,
                GrassGrowthSettings::kMinNoiseSize, GrassGrowthSettings::kMaxNoiseSize,
                "%.2f m", ImGuiSliderFlags_AlwaysClamp);
        };
        const auto drawGrassGround = [&] {
            if (terrainGrass_.groundAlbedo) {
                ImGui::TextUnformatted("MaterialAlb + MaterialCmb (layer 0)");
            } else {
                ImGui::TextDisabled("Terrain grass resources unavailable");
            }
            if (const auto* terrain = findSceneNodeById(meshData_, kTerrainSceneNodeId)) {
                drawFirstMaterial(*terrain, "terrain");
            }
        };
        if (editorSelection_ == EditorSelection::GrassGround) {
            ImGui::SeparatorText("Grass Ground");
            drawGrassGround();
            return;
        }
        if (editorSelection_ == EditorSelection::PlayerSettings && playerController_.active()) {
            ImGui::SeparatorText("Player Settings");
            const float previous = playerController_.moveSpeedMultiplier();
            float multiplier = previous;
            if (ImGui::SliderFloat("Move Speed", &multiplier,
                PlayerController::kMinMoveSpeedMultiplier, PlayerController::kMaxMoveSpeedMultiplier,
                "%.2fx", ImGuiSliderFlags_AlwaysClamp)) {
                playerController_.setMoveSpeedMultiplier(multiplier);
                editorDirty_ |= playerController_.moveSpeedMultiplier() != previous;
            }
            float cameraDistance = playerController_.cameraFollowDistance().value_or(
                PlayerController::kDefaultCameraFollowDistance);
            if (ImGui::SliderFloat("Camera Distance", &cameraDistance,
                PlayerController::kMinCameraFollowDistance, PlayerController::kMaxCameraFollowDistance,
                "%.2f m", ImGuiSliderFlags_AlwaysClamp)) {
                playerController_.setCameraFollowDistance(cameraDistance);
                editorDirty_ = true;
            }
            return;
        }
        if (!selectedModelNode_) {
            ImGui::TextWrapped("Select an object in the scene.");
            return;
        }
        auto& node = *selectedModelNode_;
        ImGui::TextWrapped("%s", node.name.c_str());
        editorDirty_ |= ImGui::Checkbox("Visible", &node.renderEnabled);
        if (node.objectType == SceneObjectType::RealtimeEffect) {
            editorDirty_ |= drawRealtimeEffectControls(node);
            return;
        }
        if (node.objectType == SceneObjectType::ParticleSystem) {
            ImGui::SeparatorText("Graphics Pipeline");
            drawEditorShader(ShaderStage::Vertex, "shader_particle.vert", "particle");
            drawEditorShader(ShaderStage::Fragment, "shader_particle.frag", "particle");
            drawParticleSystemControls(node.nodeId, editorDirty_, &node);
            return;
        }
        if (node.nodeId == kGrassSceneNodeId) {
            ImGui::SeparatorText("Grass / LOD0");
            drawGrassGrowth();
            if (!node.children.empty()) drawFirstMaterial(node.children.front(), "grass");
            ImGui::SeparatorText("Grass Ground");
            drawGrassGround();
            return;
        }
        if (const auto lod = grassLodForLayer(node.nodeId - kGrassLodNodeBase)) {
            if (*lod == GrassLod::NearMesh) {
                drawGrassGrowth();
            }
            if (*lod != GrassLod::YellowFlowerCard) drawFirstMaterial(node, "grass");
            return;
        }
        if (node.nodeId >= kTerrainSceneNodeId && node.nodeId < kPlayerSceneNodeId) {
            ImGui::TextDisabled("%zu / %zu chunks visible", terrainDrawCandidates_ - terrainDrawCulled_,
                terrainDrawCandidates_);
            drawFirstMaterial(node, "terrain");
            return;
        }
        if (node.nodeId >= kWaterSceneNodeId && node.nodeId < kGrassSceneNodeId) {
            drawFirstMaterial(node, "water");
            return;
        }
        if (node.nodeId == kTornadoPreviewNodeId) {
            drawFirstMaterial(node, "tornado");
            return;
        }
        ImGui::SeparatorText("Transform");
        const auto* resource = sceneModelResourceForNode(node);
        if (resource && !resource->playerModel && resource->rootNodeId == node.nodeId) {
            bool useGravity = modelGravity_.contains(node.nodeId);
            if (ImGui::Checkbox("Use Gravity", &useGravity)) {
                if (useGravity) {
                    modelGravity_.try_emplace(node.nodeId);
                } else {
                    modelGravity_.erase(node.nodeId);
                    physicsWorld_->removeGravityBox(node.nodeId);
                }
                editorDirty_ = true;
            }
            if (!terrainScene_) ImGui::SetItemTooltip("Gravity requires scene terrain.");
        }
        editorDirty_ |= ImGui::DragFloat3("Position", &node.transformTranslation.x, 0.005f);
        editorDirty_ |= ImGui::DragFloat3("Rotation", &node.transformRotationDegrees.x, 1.0f);
        editorDirty_ |= ImGui::SliderFloat("Scale", &node.transformScaleFactor,
            kMinimumModelScaleFactor, kMaximumModelScaleFactor);
        drawSkeletalAnimationControls(node, editorDirty_);
        if (sceneEditMode_ && resource && !resource->playerModel) {
            if (const auto* root = findSceneNodeById(meshData_, resource->rootNodeId); root && root->hasBounds) {
                drawEnemyPatrol(*root);
            }
        }
        if (ImGui::CollapsingHeader("Rotation Animation")) {
            editorDirty_ |= ImGui::Checkbox("Rotate", &node.rotationAnimationEnabled);
            editorDirty_ |= ImGui::DragFloat3("Start angle", &node.rotationAnimationInitialDegrees.x, 1.0f);
            editorDirty_ |= ImGui::DragFloat3("Speed", &node.rotationAnimationSpeedDegrees.x, 1.0f);
        }
        std::string_view family = "mesh";
        if (node.nodeId == kSunSceneNodeId) family = "sun";
        if (editorPart_ && *editorPart_ < meshData_.parts.size()) {
            drawEditorMaterial(materialIndexForPart(meshData_.parts[*editorPart_]), family);
        } else {
            drawFirstMaterial(node, family);
        }
    };
    if (!sceneStatus_.empty()) {
        if (sceneStatusFailed_) ImGui::PushStyleColor(ImGuiCol_Text, kEditorFailure);
        ImGui::TextWrapped("%s", sceneStatus_.c_str());
        if (sceneStatusFailed_) ImGui::PopStyleColor();
    }
    if (!sceneResourceWarnings_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kEditorFailure);
        const bool expanded = ImGui::TreeNode("Scene resource warnings", "Skipped resources (%zu)",
            sceneResourceWarnings_.size());
        ImGui::PopStyleColor();
        if (expanded) {
            for (const auto& warning : sceneResourceWarnings_) {
                ImGui::TextWrapped("%s", warning.c_str());
            }
            ImGui::TreePop();
        }
    }
    draw();
    ImGui::PopItemWidth();
}

} // namespace VulkCore
#endif
