#include "RendererImplementation.hpp"

#include <utility>

#include "vulkcore/Utils/SbfresLoader.hpp"

namespace VulkCore {
namespace {

constexpr float kViewerFov = 45.0f;
constexpr float kMinViewerDistance = 0.2f;
constexpr float kMaxViewerDistance = 60.0f;

std::string utf8Filename(const std::filesystem::path& path) {
    const auto name = path.filename().u8string();
    return {reinterpret_cast<const char*>(name.data()), name.size()};
}

bool hasTriangleSurface(const MeshData& mesh, const ModelNode& node) {
    if (!node.renderEnabled) return false;
    for (const auto index : node.meshPartIndices) {
        const auto& part = mesh.parts[index];
        for (std::size_t i = part.firstIndex; i < std::size_t(part.firstIndex) + part.indexCount; i += 3) {
            const glm::dvec3 a(mesh.vertices[mesh.indices[i]].position);
            const glm::dvec3 b(mesh.vertices[mesh.indices[i + 1]].position);
            const glm::dvec3 c(mesh.vertices[mesh.indices[i + 2]].position);
            const auto normal = glm::cross(b - a, c - a);
            if (glm::dot(normal, normal) > 0.0) return true;
        }
    }
    return std::any_of(node.children.begin(), node.children.end(),
        [&](const ModelNode& child) { return hasTriangleSurface(mesh, child); });
}

bool drawShapePicker(const ModelNode& model, std::optional<std::size_t>& selected) {
    const auto name = [&](std::size_t i) {
        const auto& shape = model.children[i];
        return shape.name.empty() ? "Unnamed shape" : shape.name.c_str();
    };
    ImGui::SetNextItemWidth(-1.0f);
    if (!ImGui::BeginCombo("##FSHP shape", selected ? name(*selected) : "All shapes")) return false;
    const auto previous = selected;
    if (ImGui::Selectable("All shapes", !previous)) selected.reset();
    if (!previous) ImGui::SetItemDefaultFocus();
    for (std::size_t i = 0; i < model.children.size(); ++i) {
        const auto& shape = model.children[i];
        if (shape.meshPartIndices.empty()) continue; // FSKL nodes are also children of the FMDL.
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(name(i), previous == i)) selected = i;
        if (previous == i) ImGui::SetItemDefaultFocus();
        ImGui::PopID();
    }
    ImGui::EndCombo();
    return selected != previous;
}

} // namespace

double renderer_detail::modelBoundsRadius(const ModelNode& node) {
    return glm::length(glm::dvec3(node.boundsMaximum) - glm::dvec3(node.boundsMinimum)) * 0.5;
}

std::size_t renderer_detail::defaultPreviewModel(const MeshData& mesh, bool preferAnimated) {
    const auto& models = mesh.hierarchy.children;
    std::size_t selected = models.size();
    double bestRadius = -1.0;
    for (std::size_t i = 0; i < models.size(); ++i) {
        if (!models[i].hasBounds) continue;
        const bool animated = preferAnimated && std::any_of(mesh.skeletalAnimationInstances.begin(),
            mesh.skeletalAnimationInstances.end(),
            [&](const auto& instance) { return instance.rootNodeId == models[i].nodeId; });
        if (animated) return i;
        const double radius = modelBoundsRadius(models[i]);
        if (radius > bestRadius) {
            selected = i;
            bestRadius = radius;
        }
    }
    if (selected == models.size()) throw std::runtime_error("The file contains no mesh to display.");
    return selected;
}

bool renderer_detail::drawPreviewModelPicker(const MeshData& mesh, std::size_t& selected) {
    const auto& models = mesh.hierarchy.children;
    const auto& current = models[selected];
    ImGui::SetNextItemWidth(-1.0f);
    if (!ImGui::BeginCombo("##FMDL model", current.name.empty() ? "Unnamed model" : current.name.c_str())) {
        return false;
    }
    const auto previous = selected;
    for (std::size_t i = 0; i < models.size(); ++i) {
        const auto& model = models[i];
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(model.name.empty() ? "Unnamed model" : model.name.c_str(), i == previous,
                model.hasBounds ? ImGuiSelectableFlags_None : ImGuiSelectableFlags_Disabled)) {
            selected = i;
        }
        if (i == previous) ImGui::SetItemDefaultFocus();
        ImGui::PopID();
    }
    ImGui::EndCombo();
    return selected != previous;
}

void Renderer::queueModelViewerDrop(std::vector<std::filesystem::path> paths) {
    if (mode_ == Mode::ModelViewer) viewerDropPaths_ = std::move(paths);
}

void Renderer::loadModelViewerDrop() {
    if (viewerDropPaths_.empty()) return;
    const auto paths = std::exchange(viewerDropPaths_, {});
    std::filesystem::path modelPath;
    std::filesystem::path texturePath;
    std::filesystem::path animationPath;
    MeshData imported;
    SbfresTextureArchive archive;
    std::size_t initialModel = 0;
    try {
        for (const auto& path : paths) {
            if (!isBfresPath(path)) {
                throw std::runtime_error("Expected an SBFRES or BFRES file: " + utf8Filename(path));
            }
            auto content = BfresContent::Other;
            if (isBfresTextureSidecarPath(path)) {
                content = BfresContent::Texture;
            } else if (paths.size() > 1 && isBfresAnimationPath(path)) {
                content = BfresContent::SkeletalAnimation;
            } else {
                content = inspectBfresContent(path);
            }
            std::filesystem::path* destination = nullptr;
            switch (content) {
            case BfresContent::Model: destination = &modelPath; break;
            case BfresContent::Texture: destination = &texturePath; break;
            case BfresContent::SkeletalAnimation: destination = &animationPath; break;
            case BfresContent::Other:
                throw std::runtime_error("No model, texture, or skeletal animation found: " + utf8Filename(path));
            }
            if (!destination->empty()) {
                throw std::runtime_error("Drop at most one model, one texture package, and one animation BFRES.");
            }
            *destination = path;
        }
        if (!modelPath.empty()) {
            if (texturePath.empty()) texturePath = findBfresTextureSidecar(modelPath);
            imported = loadSbfresMesh(modelPath, texturePath, animationPath, false, &archive);
            initialModel = defaultPreviewModel(imported);
        } else if (!texturePath.empty() && animationPath.empty()) {
            archive = SbfresTextureArchive(texturePath);
        } else {
            throw std::runtime_error("Drop a model with its animation BFRES, or open a texture package by itself.");
        }
    } catch (const std::exception& error) {
        viewerError_ = error.what();
        return;
    }

    // Keep the last successful preview if opening a new file fails.
    destroyViewerTexture();
    viewerTextures_ = {};
    viewerTextures_.archive = std::move(archive);
    if (!viewerTextures_.archive.entries().empty()) {
        viewerTextures_.filename = utf8Filename(texturePath.empty() ? modelPath : texturePath);
        viewerTextures_.reload = true;
    }
    viewerShowingTextures_ = modelPath.empty();
    viewerError_.clear();

    if (!modelPath.empty()) {
        meshData_ = std::move(imported);
        viewerFilename_ = utf8Filename(modelPath);
        viewerAnimationFilename_ = utf8Filename(animationPath);
        selectViewerModel(initialModel);
        rebuildSceneGeometryResources();
    }
}

void Renderer::selectViewerModel(std::size_t index) {
    auto& models = meshData_.hierarchy.children;
    for (std::size_t i = 0; i < models.size(); ++i) {
        models[i].renderEnabled = i == index;
    }
    viewerModelIndex_ = index;
    selectViewerShape(std::nullopt);
}

void Renderer::selectViewerShape(std::optional<std::size_t> shape) {
    auto& model = meshData_.hierarchy.children[viewerModelIndex_];
    viewerPoints_.clear();
    for (std::size_t i = 0; i < model.children.size(); ++i) {
        auto& child = model.children[i];
        // Keep skeleton nodes active for animation even while isolating an FSHP.
        child.renderEnabled = child.meshPartIndices.empty() || !shape || *shape == i;
        if (child.renderEnabled && !child.meshPartIndices.empty() && child.hasBounds &&
            child.boundsMinimum == child.boundsMaximum) {
            // A point-shaped FSHP has a single position across its indexed vertices.
            const auto& part = meshData_.parts[child.meshPartIndices.front()];
            viewerPoints_.push_back({part.nodeId, meshData_.indices[part.firstIndex]});
        }
    }
    viewerShapeIndex_ = shape;
    viewerHasSurface_ = hasTriangleSurface(meshData_, model);
    fitViewerModel();
}

void Renderer::fitViewerModel() {
    const auto& group = meshData_.hierarchy.children[viewerModelIndex_];
    const auto& model = viewerShapeIndex_ ? group.children[*viewerShapeIndex_] : group;
    modelCenter_ = model.boundsMinimum * 0.5f + model.boundsMaximum * 0.5f;
    const double radius = modelBoundsRadius(model);
    modelScale_ = radius > 0.0 ? static_cast<float>(1.0 / std::max(1e-9, radius)) : 1.0f;
    const VkExtent2D extent = swapChain_->extent();
    const float aspect = extent.height == 0 ? 1.0f : static_cast<float>(extent.width) / extent.height;
    const float halfFov = glm::radians(kViewerFov * 0.5f);
    const float fitAngle = std::atan(std::tan(halfFov) * std::min(1.0f, aspect));
    const float distance = std::min(kMaxViewerDistance, 1.12f / std::sin(fitAngle));

    authoredView_ = {};
    authoredView_.enabled = true;
    authoredView_.orbitEnabled = true;
    authoredView_.orbitPitchLimits = {-85.0f, 85.0f};
    authoredView_.fov = kViewerFov;
    cameraTarget_ = glm::vec3(0.0f);
    cameraPosition_ = distance * glm::normalize(glm::vec3(0.62f, 0.28f, 1.0f));
    cameraBasisInitialized_ = false;
    cameraOrbitDragging_ = false;
}

void Renderer::drawViewerPoints(const glm::mat4& clipFromScene) {
    if (viewerShowingTextures_ || viewerPoints_.empty()) return;

    // Project with the mesh camera and keep the dots legible at any zoom level.
    const auto* viewport = ImGui::GetMainViewport();
    auto* drawList = ImGui::GetBackgroundDrawList();
    constexpr float radius = 5.0f;
    for (const auto& point : viewerPoints_) {
        const auto& position = meshData_.vertices[point.vertexIndex].position;
        const auto clip = clipFromScene * transformForSceneNode(point.nodeId) * glm::vec4(position, 1.0f);
        if (!(clip.w > 0.0f && clip.z >= 0.0f && clip.z <= clip.w &&
              std::abs(clip.x) <= clip.w && std::abs(clip.y) <= clip.w)) continue;
        const glm::vec2 ndc = glm::vec2(clip) / clip.w;
        const ImVec2 center(viewport->Pos.x + (0.5f * ndc.x + 0.5f) * viewport->Size.x,
            viewport->Pos.y + (0.5f * ndc.y + 0.5f) * viewport->Size.y);
        drawList->AddCircleFilled(center, radius, IM_COL32(255, 0, 0, 255));
    }
}

void Renderer::destroyViewerTexture() {
    if (!viewerTextures_.image) return;
    context_->waitIdle();
    ImGui_ImplVulkan_RemoveTexture(viewerTextures_.descriptor);
    viewerTextures_.descriptor = VK_NULL_HANDLE;
    viewerTextures_.image.reset();
}

void Renderer::updateViewerTexture() {
    auto& preview = viewerTextures_;
    if (!viewerShowingTextures_ || !std::exchange(preview.reload, false)) return;
    destroyViewerTexture();
    preview.error.clear();
    try {
        auto pixels = preview.archive.decodeLayer(preview.index, preview.layer);
        if (!preview.showAlpha) {
            for (std::size_t i = 3; i < pixels.data.size(); i += 4) pixels.data[i] = 255;
        }
        auto image = std::make_unique<Texture>(*context_, pixels.data, pixels.width, pixels.height, false,
            pixels.srgb ? TextureColorSpace::Srgb : TextureColorSpace::Linear);
        preview.descriptor = ImGui_ImplVulkan_AddTexture(image->imageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (!preview.descriptor) throw std::runtime_error("Could not create a texture preview descriptor.");
        preview.image = std::move(image);
    } catch (const std::exception& error) {
        preview.error = error.what();
    }
}

void Renderer::drawViewerTextures() {
    auto& preview = viewerTextures_;
    const auto entries = preview.archive.entries();
    ImGui::TextWrapped("%s", preview.filename.c_str());
    ImGui::Text("Texture (%zu)", entries.size());
    ImGui::SetNextItemWidth(std::min(350.0f, ImGui::GetContentRegionAvail().x));
    if (ImGui::BeginCombo("##BNTX texture", entries[preview.index].name.c_str(), ImGuiComboFlags_HeightLarge)) {
        for (std::size_t i = 0; i < entries.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const bool selected = preview.index == i;
            if (ImGui::Selectable(entries[i].name.c_str(), selected) && !selected) {
                preview.index = i;
                preview.layer = 0;
                preview.reload = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    const auto& entry = entries[preview.index];
    ImGui::Text("%u x %u  |  %u layer(s)  |  %u mips  |  Format 0x%04X",
        entry.width, entry.height, entry.layers, static_cast<unsigned>(entry.mipLevels), entry.format);
    if (entry.layers > 1) {
        ImGui::BeginDisabled(preview.layer == 0);
        if (ImGui::ArrowButton("##Previous layer", ImGuiDir_Left)) {
            --preview.layer;
            preview.reload = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(preview.layer == entry.layers - 1);
        if (ImGui::ArrowButton("##Next layer", ImGuiDir_Right)) {
            ++preview.layer;
            preview.reload = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(185.0f);
        const std::uint32_t minLayer = 0;
        const auto maxLayer = entry.layers - 1;
        if (ImGui::SliderScalar("Layer", ImGuiDataType_U32, &preview.layer, &minLayer, &maxLayer,
                "%u", ImGuiSliderFlags_AlwaysClamp)) {
            preview.reload = true;
        }
    }
    if (ImGui::Checkbox("Show alpha", &preview.showAlpha)) preview.reload = true;
    ImGui::SameLine();
    constexpr std::string_view channelNames = "01RGBA";
    std::array<char, 5> swizzle{};
    for (std::size_t i = 0; i < entry.components.size(); ++i) {
        const auto c = entry.components[i];
        swizzle[i] = c < channelNames.size() ? channelNames[c] : '?';
    }
    ImGui::TextDisabled("Mip 0  |  RGBA = %s", swizzle.data());
    ImGui::Separator();
    if (preview.reload) {
        ImGui::TextDisabled("Loading layer %u", preview.layer);
    } else if (!preview.error.empty()) {
        ImGui::TextWrapped("%s", preview.error.c_str());
    } else if (preview.image) {
        const auto available = ImGui::GetContentRegionAvail();
        const float width = static_cast<float>(entry.width);
        const float height = static_cast<float>(entry.height);
        const float scale = std::min(available.x / width, available.y / height);
        if (scale > 0.0f) {
            const ImVec2 position = ImGui::GetCursorScreenPos();
            const ImVec2 size(width * scale, height * scale);
            ImGui::SetCursorScreenPos(ImVec2(position.x + (available.x - size.x) * 0.5f,
                position.y + (available.y - size.y) * 0.5f));
            ImGui::Image((ImTextureID)preview.descriptor, size);
        }
    }
}

bool Renderer::drawModelViewerUi() {
    const bool hasModel = !meshData_.hierarchy.children.empty();
    const bool hasTextures = !viewerTextures_.archive.entries().empty();
    const bool textureView = hasTextures && viewerShowingTextures_;
    const bool hasContent = hasModel || hasTextures;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 position = hasContent ? ImVec2(viewport->WorkPos.x + 12.0f, viewport->WorkPos.y + 12.0f)
                                       : viewport->GetWorkCenter();
    ImGui::SetNextWindowPos(position, ImGuiCond_Always,
        hasContent ? ImVec2(0.0f, 0.0f) : ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(textureView ? ImVec2(viewport->WorkSize.x - 24.0f, viewport->WorkSize.y - 24.0f)
                                        : ImVec2(335.0f, 0.0f));
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    if (textureView) ImGui::SetNextWindowBgAlpha(1.0f);
    if (ImGui::Begin("SBFRES Viewer", nullptr, flags | (textureView ? 0 : ImGuiWindowFlags_AlwaysAutoResize))) {
        if (hasModel && hasTextures) {
            if (ImGui::RadioButton("Models", !viewerShowingTextures_)) viewerShowingTextures_ = false;
            ImGui::SameLine();
            if (ImGui::RadioButton("Textures", viewerShowingTextures_)) viewerShowingTextures_ = true;
            ImGui::Separator();
        }
        if (!viewerError_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.62f, 0.46f, 1.0f));
            ImGui::TextWrapped("%s", viewerError_.c_str());
            ImGui::PopStyleColor();
            ImGui::Spacing();
        }
        if (textureView) {
            drawViewerTextures();
        } else if (hasModel) {
            ImGui::TextWrapped("%s", viewerFilename_.c_str());
            auto& models = meshData_.hierarchy.children;
            ImGui::Text("Model group (%zu)", models.size());
            auto index = viewerModelIndex_;
            if (drawPreviewModelPicker(meshData_, index)) selectViewerModel(index);
            auto& model = models[viewerModelIndex_];
            const auto shapeCount = std::count_if(model.children.begin(), model.children.end(),
                [](const auto& node) { return !node.meshPartIndices.empty(); });
            ImGui::Text("Shape (%zu)", static_cast<std::size_t>(shapeCount));
            auto shape = viewerShapeIndex_;
            if (drawShapePicker(model, shape)) selectViewerShape(shape);
            if (ImGui::Button("Fit view (F)")) fitViewerModel();
            ImGui::TextDisabled("Left drag to orbit / Scroll to zoom");
            if (!viewerPoints_.empty()) {
                ImGui::TextDisabled("Point shapes are shown in red.");
            } else if (!viewerHasSurface_) {
                ImGui::TextWrapped("The selected geometry has no non-degenerate triangles.");
            }
            if (!viewerAnimationFilename_.empty()) {
                ImGui::TextWrapped("Animation: %s", viewerAnimationFilename_.c_str());
            }
            bool shouldSaveSettings = false;
            drawSkeletalAnimationControls(model, shouldSaveSettings);
        } else {
            ImGui::TextUnformatted("Drop an SBFRES or BFRES file here");
            ImGui::TextWrapped("Open a model or a texture package, or drop a model together with its companions.");
        }
    }
    const bool hoveringPanel = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
    ImGui::End();

    const ImGuiIO& io = ImGui::GetIO();
    const bool viewportHovered = !hoveringPanel && !io.WantCaptureMouse;
    if (!hasModel || textureView || viewerShowingTextures_) return false;
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false)) fitViewerModel();
    updateCameraOrbit(viewportHovered);
    if (viewportHovered && io.MouseWheel != 0.0f && fixedFrameOutput_.empty()) {
        const glm::vec3 offset = cameraPosition_ - cameraTarget_;
        const float distance = glm::length(offset);
        const float zoom = std::exp(-0.16f * io.MouseWheel);
        const float newDistance = std::clamp(distance * zoom, kMinViewerDistance, kMaxViewerDistance);
        cameraPosition_ = cameraTarget_ + offset * (newDistance / distance);
    }
    return viewportHovered;
}

} // namespace VulkCore
