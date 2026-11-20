#include "RendererImplementation.hpp"

#ifndef VULKCORE_OHOS
#include "../Platform/EditorFileDialog.hpp"
#include "vulkcore/Terrain/Terrain.hpp"

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
    editorOpenConfirmed_ = false;
    toolUiVisible_ = true;
}

void Renderer::openEditorFilePicker(const char* title, bool scene,
                                    const std::filesystem::path& directory,
                                    std::function<void(const std::filesystem::path&)> accept) {
#if defined(__APPLE__) || defined(_WIN32)
    const auto path = pickEditorFile(title, scene, directory);
    if (!path.empty()) accept(path);
#else
    editorFilePicker_ = {title, scene, directory, {}, std::move(accept)};
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
    ImGui::TextWrapped("%s", folder.string().c_str());
    if (ImGui::Button("Parent folder") && folder.has_parent_path()) {
        picker.directory = folder.parent_path();
    }
    std::vector<std::filesystem::path> folders, files;
    std::error_code error;
    for (std::filesystem::directory_iterator entry(folder, error), end;
         !error && entry != end; entry.increment(error)) {
        std::error_code typeError;
        if (entry->is_directory(typeError)) folders.push_back(entry->path());
        else if (entry->is_regular_file(typeError) && (!picker.scene || isSceneResourcePath(entry->path()))) {
            files.push_back(entry->path());
        }
    }
    std::sort(folders.begin(), folders.end());
    std::sort(files.begin(), files.end());
    std::filesystem::path selected;
    ImGui::BeginChild("Files", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.0f));
    for (const auto& directory : folders) {
        const auto label = directory.filename().string() + "/";
        if (ImGui::Selectable(label.c_str())) picker.directory = directory;
    }
    for (const auto& file : files) {
        if (ImGui::Selectable(file.filename().string().c_str())) selected = file;
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
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("VulkCore Game Editor", nullptr, flags);
    ImGui::PopStyleVar(3);

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

    const auto openScene = [&] {
        openEditorFilePicker("Open Scene", true, currentScenePath_.empty() ?
            scenesDirectory_ : currentScenePath_.parent_path(),
            [this](const auto& path) { queueSceneOpen(path); });
    };
    const auto saveScene = [&] {
        if (!currentScenePath_.empty() && saveSceneResource(currentScenePath_)) editorDirty_ = false;
    };
    const bool modalOpen = ImGui::IsPopupOpen(nullptr,
        ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    const auto& io = ImGui::GetIO();
#ifdef __APPLE__
    const bool shortcut = io.KeySuper;
#else
    const bool shortcut = io.KeyCtrl;
#endif
    if (shortcut && !io.WantTextInput && !modalOpen) {
        if (ImGui::IsKeyPressed(ImGuiKey_O, false)) openScene();
        if (ImGui::IsKeyPressed(ImGuiKey_S, false)) saveScene();
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + margin, origin.y + margin));
    constexpr ImGuiWindowFlags toolbarFlags = ImGuiWindowFlags_MenuBar |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::BeginChild("SceneToolbar", ImVec2(width, toolbarHeight),
            ImGuiChildFlags_Borders, toolbarFlags) && ImGui::BeginMenuBar()) {
        if (ImGui::MenuItem("Open Scene...")) openScene();
        if (ImGui::MenuItem("Save", nullptr, false, !currentScenePath_.empty())) saveScene();
        if (ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("Capture screenshot", "F12")) screenshotFromToolbar_ = true;
            if (ImGui::MenuItem("Hide editor", "F1")) toolUiVisible_ = false;
            if (playerController_.active() && ImGui::MenuItem("Debug flight", "F2",
                playerController_.debugModeEnabled())) {
                playerController_.setDebugModeEnabled(!playerController_.debugModeEnabled());
            }
            if (terrainScene_ && ImGui::MenuItem("Terrain frustum culling", nullptr,
                terrainFrustumCullingEnabled_)) {
                terrainFrustumCullingEnabled_ = !terrainFrustumCullingEnabled_;
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        const std::string title = currentScenePath_.empty() ? "Drop a .scene file to open" :
            currentScenePath_.filename().string() + (editorDirty_ ? " *" : "");
        ImGui::TextDisabled("%s", title.c_str());
        ImGui::EndMenuBar();
    }

    ImGui::EndChild();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(7, 7));
    ImGui::SetCursorScreenPos(ImVec2(origin.x + margin, panelY));
    if (ImGui::BeginChild("Outliner", ImVec2(left, panelHeight), ImGuiChildFlags_Borders)) drawGameOutliner();
    ImGui::EndChild();
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

    if (!pendingSceneOpen_.empty() && editorDirty_ && !editorOpenConfirmed_) {
        ImGui::OpenPopup("Open another scene?");
    }
    if (ImGui::BeginPopupModal("Open another scene?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The current scene has unsaved changes.");
        if (ImGui::Button("Save and open")) {
            saveScene();
            if (!editorDirty_) {
                editorOpenConfirmed_ = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard changes")) {
            editorOpenConfirmed_ = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            pendingSceneOpen_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    drawEditorFilePicker();
    drawEditorShaderDialog();
    // The root covers the game. Hovering a child or a popup belongs to the editor.
    const bool hovered = ImGui::IsWindowHovered();
    ImGui::PopStyleColor(5);
    ImGui::End();
    return hovered;
}

void Renderer::drawGameOutliner() {
    ImGui::SeparatorText("Scene");
    const auto select = [&](const char* label, EditorSelection selection) {
        if (ImGui::Selectable(label, editorSelection_ == selection)) {
            editorSelection_ = selection;
            selectedModelNode_ = nullptr;
            editorPart_.reset();
        }
    };
    select("Environment", EditorSelection::Environment);
    if (!currentScenePath_.empty()) select("Skybox", EditorSelection::Skybox);

    const auto selectNode = [&](ModelNode& node, std::optional<std::uint32_t> part = std::nullopt) {
        editorSelection_ = EditorSelection::Node;
        editorPart_ = part;
        selectedModelNode_ = &node;
    };
    const auto drawRoot = [&](ModelNode& root) {
        if (root.name.empty() || (root.children.empty() && root.meshPartIndices.empty() &&
            root.objectType != SceneObjectType::ParticleSystem)) return;
        ImGui::PushID(static_cast<int>(root.nodeId));
        const bool grass = root.nodeId == kGrassSceneNodeId;
        const bool expand = grass || !root.children.empty() || !root.meshPartIndices.empty();
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
        if (!expand) flags |= ImGuiTreeNodeFlags_Leaf;
        if (editorSelection_ == EditorSelection::Node && selectedModelNode_ == &root && !editorPart_) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        const bool open = ImGui::TreeNodeEx("##object", flags, "%s", root.name.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) selectNode(root);
        if (open) {
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
            if (grass) select("LOD1 Ground Texture", EditorSelection::GrassGround);
            ImGui::TreePop();
        }
        ImGui::PopID();
    };
    drawRoot(meshData_.hierarchy);
    for (auto& object : meshData_.additionalNodes) drawRoot(object);
    for (const auto effectId : {kPlayerLightningParticleNodeId, kPlayerFlameParticleNodeId}) {
        if (!particleSystems_.contains(effectId)) continue;
        const char* label = effectId == kPlayerLightningParticleNodeId
            ? "Lightning Slash (Q)" : "Flame Nova (E)";
        if (ImGui::Selectable(label, editorSelection_ == EditorSelection::PlayerEffect &&
            editorPlayerEffect_ == effectId)) {
            editorSelection_ = EditorSelection::PlayerEffect;
            editorPlayerEffect_ = effectId;
            selectedModelNode_ = nullptr;
            editorPart_.reset();
        }
    }
}

void Renderer::drawEditorTexture(const char* label, const std::filesystem::path& path,
                                 const std::filesystem::path& defaultPath,
                                 std::function<void(const std::filesystem::path&)> accept) {
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    const auto& displayedPath = path.empty() ? defaultPath : path;
    ImGui::TextWrapped("%s", displayedPath.empty() ? "Procedural" : displayedPath.filename().string().c_str());
    if (!displayedPath.empty()) ImGui::SetItemTooltip("%s", displayedPath.string().c_str());
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
    if (ImGui::Button("Replace...")) openEditorFilePicker(label, false,
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

void Renderer::drawGameInspector() {
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
        const auto drawGrassHeight = [&] {
            float height = grassHeightScale_ * kDefaultGrassHeight;
            if (ImGui::SliderFloat("Average height", &height,
                0.25f * kDefaultGrassHeight, 3.0f * kDefaultGrassHeight, "%.2f m",
                ImGuiSliderFlags_AlwaysClamp)) {
                grassHeightScale_ = height / kDefaultGrassHeight;
                editorDirty_ = true;
            }
        };
        const auto drawGrassGround = [&] {
            drawEditorTexture("LOD1 ground texture", grassGroundTexturePath_, {},
                [this](const auto& path) { applyGrassGroundTexture(path); });
            if (const auto* terrain = findSceneNodeById(meshData_, kTerrainSceneNodeId)) {
                drawFirstMaterial(*terrain, "terrain");
            }
        };
        if (editorSelection_ == EditorSelection::GrassGround) {
            ImGui::SeparatorText("Grass / LOD1");
            drawGrassGround();
            return;
        }
        if (editorSelection_ == EditorSelection::PlayerEffect) {
            ImGui::SeparatorText(editorPlayerEffect_ == kPlayerLightningParticleNodeId
                ? "Lightning Slash" : "Flame Nova");
            drawEditorShader(ShaderStage::Vertex, "shader_particle.vert", "particle");
            drawEditorShader(ShaderStage::Fragment, "shader_particle.frag", "particle");
            drawParticleSystemControls(editorPlayerEffect_, editorDirty_);
            return;
        }
        if (!selectedModelNode_) {
            ImGui::TextWrapped("Select an object in the scene.");
            return;
        }
        auto& node = *selectedModelNode_;
        ImGui::TextWrapped("%s", node.name.c_str());
        editorDirty_ |= ImGui::Checkbox("Visible", &node.renderEnabled);
        if (node.objectType == SceneObjectType::ParticleSystem) {
            ImGui::SeparatorText("Graphics Pipeline");
            drawEditorShader(ShaderStage::Vertex, "shader_particle.vert", "particle");
            drawEditorShader(ShaderStage::Fragment, "shader_particle.frag", "particle");
            drawParticleSystemControls(node.nodeId, editorDirty_, &node);
            return;
        }
        if (node.nodeId == kGrassSceneNodeId) {
            ImGui::SeparatorText("Grass / LOD0");
            drawGrassHeight();
            if (!node.children.empty()) drawFirstMaterial(node.children.front(), "grass");
            ImGui::SeparatorText("Grass / LOD1");
            drawGrassGround();
            return;
        }
        if (const auto lod = grassLodForLayer(node.nodeId - kGrassLodNodeBase)) {
            if (*lod == GrassLod::NearMesh) {
                drawGrassHeight();
            }
            drawFirstMaterial(node, "grass");
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
        if (node.nodeId == kPlayerTornadoMeshNodeId) {
            drawFirstMaterial(node, "tornado");
            return;
        }
        ImGui::SeparatorText("Transform");
        editorDirty_ |= ImGui::DragFloat3("Position", &node.transformTranslation.x, 0.005f);
        editorDirty_ |= ImGui::DragFloat3("Rotation", &node.transformRotationDegrees.x, 1.0f);
        editorDirty_ |= ImGui::SliderFloat("Scale", &node.transformScaleFactor,
            kMinimumModelScaleFactor, kMaximumModelScaleFactor);
        if (ImGui::CollapsingHeader("Animation")) {
            drawSkeletalAnimationControls(node, editorDirty_);
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
    draw();
    ImGui::PopItemWidth();
}

} // namespace VulkCore
#endif
