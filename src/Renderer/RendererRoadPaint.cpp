#include "RendererImplementation.hpp"

#ifndef VULKCORE_OHOS
#include "vulkcore/Physics/PhysicsWorld.hpp"
#include "vulkcore/Terrain/Terrain.hpp"
#endif

namespace VulkCore {

bool Renderer::roadEditorActive() const {
#ifndef VULKCORE_OHOS
    return mode_ == Mode::Scene && sceneEditMode_ && terrainScene_ && roadBrushEnabled_ &&
        fixedFrameOutput_.empty();
#else
    return false;
#endif
}

void Renderer::bindRoadIndex(std::uint32_t frame) {
    const auto& buffer = *roadFrameBuffers_[frame].buffer;
    const VkDescriptorBufferInfo info{buffer.handle(), 0, buffer.size()};
    const auto materialCount = meshData_.materials.size();
    for (std::size_t i = 0; i < materialCount; ++i) {
        const VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
            descriptorSets_[frame * materialCount + i], kTerrainRoadBinding, 0, 1,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &info, nullptr};
        vkUpdateDescriptorSets(context_->device(), 1, &write, 0, nullptr);
    }
}

void Renderer::updateRoadIndex() {
    const auto revision = roadPaint_.revision();
    auto& frame = roadFrameBuffers_[currentFrame_];
    if (frame.revision == revision) return;
    if (roadIndexRevision_ != revision) {
        roadIndex_ = roadPaint_.buildGpuIndex();
        roadIndexRevision_ = revision;
    }
    const VkDeviceSize size = sizeof(std::uint32_t) * roadIndex_.size();
    // This frame's fence has completed. Other frames keep their own snapshots.
    if (frame.buffer->size() < size) {
        frame.buffer = std::make_unique<Buffer>(*context_, size + size / 2,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        bindRoadIndex(currentFrame_);
    }
    frame.buffer->upload(roadIndex_.data(), size);
    frame.revision = revision;
}

#ifndef VULKCORE_OHOS
namespace {

bool roadInputAvailable(bool viewportOwnsInput) {
    const auto& io = ImGui::GetIO();
    return !io.AppFocusLost && !io.WantTextInput && (viewportOwnsInput || !ImGui::IsAnyItemActive()) &&
        !ImGui::GetDragDropPayload() &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

bool insideRoadViewport(const glm::vec4& viewport, ImVec2 point) {
    return point.x >= viewport.x && point.x < viewport.z &&
        point.y >= viewport.y && point.y < viewport.w;
}

} // namespace

void Renderer::drawRoadBrushControls() {
    ImGui::SeparatorText("Road Brush");
    constexpr const char* surfaces[] = {"Erase", "Mud", "Gravel", "Stone paving"};
    int surface = static_cast<int>(roadBrushSurface_);
    if (ImGui::Combo("Surface", &surface, surfaces, IM_ARRAYSIZE(surfaces))) {
        roadBrushSurface_ = static_cast<RoadSurface>(surface);
    }
    ImGui::SliderFloat("Radius", &roadBrushRadius_, RoadPaint::kMinimumRadius,
        RoadPaint::kMaximumRadius, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
    constexpr const char* views[] = {"Perspective", "Top-down"};
    int view = roadTopDown_ ? 1 : 0;
    if (ImGui::Combo("View", &view, views, IM_ARRAYSIZE(views))) roadTopDown_ = view == 1;
    ImGui::TextWrapped("Left drag: paint\nRight drag: look\nWASD: move\nWheel: zoom");
    if (roadPaint_.full()) ImGui::TextWrapped("This scene has reached its road stroke limit.");
}

void Renderer::updateRoadEditorCamera(float deltaSeconds, bool viewportHovered) {
    if (!roadEditorActive()) {
        if (roadEditorCamera_) {
            if (!playerController_.active()) {
                cameraPosition_ = roadEditorCamera_->originalPosition;
                cameraTarget_ = roadEditorCamera_->originalTarget;
                cameraBasisInitialized_ = false;
            }
            roadEditorCamera_.reset();
        }
        roadBrushDragging_ = false;
        roadStrokeHasPoint_ = false;
        return;
    }

    const auto& terrain = *terrainScene_;
    const auto toRenderUnits = [&](const glm::vec3& point) { return (point - modelCenter_) * modelScale_; };
    const auto toTerrainUnits = [&](const glm::vec3& point) { return point / modelScale_ + modelCenter_; };
    const bool hasPlayer = playerController_.active();
    const auto& boundsMin = terrain.boundsMinimum();
    const auto& boundsMax = terrain.boundsMaximum();
    const float maxDistance = std::max(60.0f, glm::length(boundsMax - boundsMin));
    if (!roadEditorCamera_) {
        const glm::vec3 offset = cameraPosition_ - cameraTarget_;
        const float distance = glm::length(offset);
        const float yaw = std::atan2(offset.x, offset.z);
        const float pitch = distance > 0.0f ? std::asin(std::clamp(offset.y / distance, -1.0f, 1.0f)) : 0.0f;
        roadEditorCamera_ = RoadEditorCamera{cameraPosition_, cameraTarget_, yaw,
            std::clamp(pitch, glm::radians(38.0f), glm::radians(85.0f)),
            hasPlayer ? 24.0f : std::clamp(distance / modelScale_, 12.0f, maxDistance)};
        if (hasPlayer) cameraTarget_ = toRenderUnits(playerController_.position());
    }

    auto& camera = *roadEditorCamera_;
    const auto& io = ImGui::GetIO();
    const bool available = roadInputAvailable(roadViewportOwnsInput_) && pendingSceneChange_ == EditorSceneChange::None;
    const bool overScene = available && viewportHovered && insideRoadViewport(sceneEditorViewport_, io.MousePos);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) camera.dragging = overScene;
    if (!available || !ImGui::IsMouseDown(ImGuiMouseButton_Right)) camera.dragging = false;
    if (camera.dragging && !ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        constexpr float radiansPerPixel = 0.005f;
        camera.yaw = std::remainder(camera.yaw - io.MouseDelta.x * radiansPerPixel, glm::two_pi<float>());
        if (!roadTopDown_) {
            camera.pitch = std::clamp(camera.pitch + io.MouseDelta.y * radiansPerPixel,
                glm::radians(-45.0f), glm::radians(85.0f));
        }
    }
    if (overScene && !roadBrushDragging_ && io.MouseWheel != 0.0f) {
        camera.distance = std::clamp(camera.distance * std::exp(-0.14f * io.MouseWheel), 4.0f, maxDistance);
    }

    const glm::vec3 back(std::sin(camera.yaw), 0.0f, std::cos(camera.yaw));
    const glm::vec3 right(back.z, 0.0f, -back.x);
    if ((overScene || camera.dragging) && io.KeyMods == 0) {
        const auto axis = [](ImGuiKey positive, ImGuiKey negative) {
            return static_cast<float>(ImGui::IsKeyDown(positive)) - static_cast<float>(ImGui::IsKeyDown(negative));
        };
        const glm::vec3 movement = axis(ImGuiKey_D, ImGuiKey_A) * right - axis(ImGuiKey_W, ImGuiKey_S) * back;
        if (movement != glm::vec3(0.0f)) {
            const float speed = camera.distance * 0.85f;
            glm::vec3 pivot = toTerrainUnits(cameraTarget_) + glm::normalize(movement) * (speed * deltaSeconds);
            pivot.x = std::clamp(pivot.x, boundsMin.x, boundsMax.x);
            pivot.z = std::clamp(pivot.z, boundsMin.z, boundsMax.z);
            if (const auto height = terrain.renderedHeightAtWorld({pivot.x, pivot.z})) pivot.y = *height;
            cameraTarget_ = toRenderUnits(pivot);
        }
    }

    if (roadTopDown_) {
        const float height = std::max(boundsMax.y + 10.0f, toTerrainUnits(cameraTarget_).y + camera.distance);
        cameraPosition_ = {cameraTarget_.x, (height - modelCenter_.y) * modelScale_, cameraTarget_.z};
        cameraUp_ = -back;
    } else {
        const glm::vec3 offset = camera.distance *
            (std::cos(camera.pitch) * back + glm::vec3(0.0f, std::sin(camera.pitch), 0.0f));
        glm::vec3 position = toTerrainUnits(cameraTarget_) + offset;
        if (const auto ground = terrain.renderedHeightAtWorld({position.x, position.z})) {
            position.y = std::max(position.y, *ground + 0.5f);
        }
        cameraPosition_ = toRenderUnits(position);
        cameraUp_ = glm::vec3(0.0f, 1.0f, 0.0f);
    }
    cameraBasisInitialized_ = true;
}

void Renderer::updateRoadBrush(const glm::mat4& clipFromScene, bool viewportHovered) {
    const auto& io = ImGui::GetIO();
    const bool available = roadInputAvailable(roadViewportOwnsInput_) && pendingSceneChange_ == EditorSceneChange::None &&
        !roadEditorCamera_->dragging && !ImGui::IsMouseDown(ImGuiMouseButton_Right) && io.KeyMods == 0;
    const bool overScene = available && viewportHovered && insideRoadViewport(sceneEditorViewport_, io.MousePos);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        roadBrushDragging_ = overScene;
        roadStrokeHasPoint_ = false;
    }
    if (!available || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) roadBrushDragging_ = false;
    if (!overScene) {
        roadStrokeHasPoint_ = false;
        return;
    }

    const auto* viewport = ImGui::GetMainViewport();
    const glm::dvec2 ndc(2.0 * (io.MousePos.x - viewport->Pos.x) / viewport->Size.x - 1.0,
        2.0 * (io.MousePos.y - viewport->Pos.y) / viewport->Size.y - 1.0);
    const glm::dmat4 sceneFromClip = glm::inverse(glm::dmat4(clipFromScene));
    const auto unproject = [&](double depth) {
        const glm::dvec4 point = sceneFromClip * glm::dvec4(ndc, depth, 1.0);
        return glm::dvec3(point) / point.w;
    };
    const glm::dvec3 nearPoint = unproject(0.0);
    const glm::vec3 direction(glm::normalize(unproject(0.99) - nearPoint));
    const auto hit = physicsWorld_->raycastTerrain(glm::vec3(nearPoint), direction, 100.0f / modelScale_);
    if (!hit) {
        roadStrokeHasPoint_ = false;
        return;
    }
    const glm::vec2 point(hit->x, hit->z);
    if (roadBrushDragging_ && roadPaint_.paint(roadBrushSurface_, roadBrushRadius_, point,
            roadStrokeHasPoint_)) {
        roadStrokeHasPoint_ = true;
        editorDirty_ = true;
    }

    auto* draw = ImGui::GetBackgroundDrawList();
    draw->PushClipRect({sceneEditorViewport_.x, sceneEditorViewport_.y},
        {sceneEditorViewport_.z, sceneEditorViewport_.w}, true);
    constexpr int segments = 48;
    std::optional<ImVec2> start;
    for (int i = 0; i <= segments; ++i) {
        const float angle = glm::two_pi<float>() * i / segments;
        const glm::vec2 edge = point + glm::vec2(std::cos(angle), std::sin(angle)) * roadBrushRadius_;
        const auto height = terrainScene_->renderedHeightAtWorld(edge);
        if (!height) {
            start.reset();
            continue;
        }
        const glm::vec4 clip = clipFromScene * glm::vec4(edge.x, *height + 0.03f, edge.y, 1.0f);
        if (clip.w <= 0.0f || clip.z < 0.0f || clip.z > clip.w) {
            start.reset();
            continue;
        }
        const ImVec2 end(viewport->Pos.x + (0.5f * clip.x / clip.w + 0.5f) * viewport->Size.x,
            viewport->Pos.y + (0.5f * clip.y / clip.w + 0.5f) * viewport->Size.y);
        if (start) {
            const ImU32 color = roadBrushSurface_ == RoadSurface::Erase ? IM_COL32(255, 125, 96, 255)
                : IM_COL32(255, 235, 166, 255);
            draw->AddLine(*start, end, IM_COL32(28, 23, 16, 220), 3.0f);
            draw->AddLine(*start, end, color, 1.4f);
        }
        start = end;
    }
    draw->PopClipRect();
}
#endif

} // namespace VulkCore
