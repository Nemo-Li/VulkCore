#include "RendererImplementation.hpp"

#include <utility>

#include "vulkcore/Utils/SbfresLoader.hpp"

namespace VulkCore {
namespace {

constexpr float kViewerFov = 45.0f;
constexpr float kMinViewerDistance = 0.2f;
constexpr float kMaxViewerDistance = 60.0f;

// Use double for fit calculations so that squaring a large model's extent
// cannot overflow before the geometry is normalized for display.
double boundsRadius(const ModelNode& node) {
    return glm::length(glm::dvec3(node.boundsMaximum) - glm::dvec3(node.boundsMinimum)) * 0.5;
}

std::string utf8Filename(const std::filesystem::path& path) {
    const auto name = path.filename().u8string();
    return {reinterpret_cast<const char*>(name.data()), name.size()};
}

} // namespace

void Renderer::queueModelViewerDrop(std::vector<std::filesystem::path> paths) {
    if (mode_ == Mode::ModelViewer) viewerDropPaths_ = std::move(paths);
}

void Renderer::loadModelViewerDrop() {
    if (viewerDropPaths_.empty()) return;
    const auto paths = std::exchange(viewerDropPaths_, {});
    std::filesystem::path modelPath;
    std::filesystem::path texturePath;
    MeshData imported;
    std::size_t initialModel = 0;
    try {
        for (const auto& path : paths) {
            if (!isBfresPath(path)) {
                throw std::runtime_error("Expected an SBFRES or BFRES file: " + utf8Filename(path));
            }
            auto& destination = isBfresTextureSidecarPath(path) ? texturePath : modelPath;
            if (!destination.empty()) {
                throw std::runtime_error("Drop one model with at most one .Tex companion.");
            }
            destination = path;
        }
        if (modelPath.empty()) {
            throw std::runtime_error("Drop the model file along with its .Tex companion.");
        }
        imported = loadSbfresMesh(modelPath, texturePath);
        double largestRadius = -1.0;
        for (std::size_t i = 0; i < imported.hierarchy.children.size(); ++i) {
            const auto& node = imported.hierarchy.children[i];
            if (!node.hasBounds) continue;
            const double radius = boundsRadius(node);
            if (radius > largestRadius) {
                largestRadius = radius;
                initialModel = i;
            }
        }
        if (largestRadius < 0.0) throw std::runtime_error("The file contains no mesh to display.");
    } catch (const std::exception& error) {
        viewerError_ = error.what();
        return;
    }

    // Only replace the current view after the complete CPU import succeeds.
    meshData_ = std::move(imported);
    viewerFilename_ = utf8Filename(modelPath);
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
    modelScale_ = static_cast<float>(1.0 / std::max(1e-9, boundsRadius(model)));
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
            const auto& models = meshData_.hierarchy.children;
            const auto& current = models[viewerModelIndex_];
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##FMDL model", current.name.empty() ? "Unnamed model" : current.name.c_str())) {
                for (std::size_t i = 0; i < models.size(); ++i) {
                    const auto& model = models[i];
                    const bool selected = i == viewerModelIndex_;
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::Selectable(model.name.empty() ? "Unnamed model" : model.name.c_str(), selected,
                            model.hasBounds ? ImGuiSelectableFlags_None : ImGuiSelectableFlags_Disabled)) {
                        selectViewerModel(i);
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            if (ImGui::Button("Fit view (F)")) fitViewerModel();
            ImGui::TextDisabled("Left drag to orbit / Scroll to zoom");
        } else {
            ImGui::TextUnformatted("Drop an SBFRES or BFRES model here");
            ImGui::TextWrapped("Keep its .Tex file alongside the model, or drop both files together.");
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
