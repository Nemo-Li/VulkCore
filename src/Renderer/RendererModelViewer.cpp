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
    std::size_t initialModel = 0;
    try {
        for (const auto& path : paths) {
            if (!isBfresPath(path)) {
                throw std::runtime_error("Expected an SBFRES or BFRES file: " + utf8Filename(path));
            }
            std::filesystem::path* destination = nullptr;
            if (isBfresTextureSidecarPath(path)) {
                destination = &texturePath;
            } else if (isBfresAnimationPath(path)) {
                destination = &animationPath;
            } else {
                destination = &modelPath;
            }
            if (!destination->empty()) {
                throw std::runtime_error("Drop one model, one .Tex companion, and one animation BFRES.");
            }
            *destination = path;
        }
        if (modelPath.empty()) {
            throw std::runtime_error("Drop the model file along with its optional .Tex and animation files.");
        }
        imported = loadSbfresMesh(modelPath, texturePath, animationPath);
        initialModel = defaultPreviewModel(imported);
    } catch (const std::exception& error) {
        viewerError_ = error.what();
        return;
    }

    // Only replace the current view after the complete CPU import succeeds.
    meshData_ = std::move(imported);
    viewerFilename_ = utf8Filename(modelPath);
    viewerAnimationFilename_ = animationPath.empty() ? std::string{} : utf8Filename(animationPath);
    viewerError_.clear();
    selectViewerModel(initialModel);
    rebuildSceneGeometryResources();
}

void Renderer::selectViewerModel(std::size_t index) {
    auto& models = meshData_.hierarchy.children;
    for (std::size_t i = 0; i < models.size(); ++i) {
        models[i].renderEnabled = i == index;
    }
    viewerModelIndex_ = index;
    fitViewerModel();
}

void Renderer::fitViewerModel() {
    const auto& model = meshData_.hierarchy.children[viewerModelIndex_];
    modelCenter_ = model.boundsMinimum * 0.5f + model.boundsMaximum * 0.5f;
    modelScale_ = static_cast<float>(1.0 / std::max(1e-9, modelBoundsRadius(model)));
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

bool Renderer::drawModelViewerUi() {
    const bool hasModel = !meshData_.hierarchy.children.empty();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 position = hasModel ? ImVec2(viewport->WorkPos.x + 12.0f, viewport->WorkPos.y + 12.0f)
                                     : viewport->GetWorkCenter();
    ImGui::SetNextWindowPos(position, ImGuiCond_Always,
        hasModel ? ImVec2(0.0f, 0.0f) : ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(335.0f, 0.0f));
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoFocusOnAppearing;
    if (ImGui::Begin("SBFRES Viewer", nullptr, flags)) {
        if (hasModel) {
            ImGui::TextWrapped("%s", viewerFilename_.c_str());
            auto& models = meshData_.hierarchy.children;
            auto index = viewerModelIndex_;
            if (drawPreviewModelPicker(meshData_, index)) selectViewerModel(index);
            if (ImGui::Button("Fit view (F)")) fitViewerModel();
            ImGui::TextDisabled("Left drag to orbit / Scroll to zoom");
            if (!viewerAnimationFilename_.empty()) {
                ImGui::TextWrapped("Animation: %s", viewerAnimationFilename_.c_str());
            }
            bool shouldSaveSettings = false;
            drawSkeletalAnimationControls(models[viewerModelIndex_], shouldSaveSettings);
        } else {
            ImGui::TextUnformatted("Drop an SBFRES or BFRES model here");
            ImGui::TextWrapped(
                "Keep its .Tex file alongside the model, or drop the model, texture, and animation BFRES together."
            );
        }
        if (!viewerError_.empty()) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.62f, 0.46f, 1.0f));
            ImGui::TextWrapped("%s", viewerError_.c_str());
            ImGui::PopStyleColor();
        }
    }
    const bool hoveringPanel = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
    ImGui::End();

    const ImGuiIO& io = ImGui::GetIO();
    const bool viewportHovered = !hoveringPanel && !io.WantCaptureMouse;
    if (!hasModel) return viewportHovered;
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
