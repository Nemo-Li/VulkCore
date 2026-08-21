#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "vulkcore/Renderer/Vertex.hpp"

struct GLFWwindow;

namespace VulkCore {

struct CapsuleMeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

CapsuleMeshData createCapsuleMesh(
    float height,
    float radius,
    std::uint32_t radialSegments = 24,
    std::uint32_t hemisphereSegments = 8
);

class TerrainScene;

class PlayerController {
public:
    static constexpr float kBotwLinkHeight = 1.58f;
    static constexpr float kBotwLinkRadius = 0.27f;

    void clear();
    void reset(
        const TerrainScene& terrain,
        const std::optional<glm::vec3>& spawnPosition = std::nullopt
    );
    void update(
        GLFWwindow* window,
        float deltaSeconds,
        const TerrainScene& terrain,
        bool keyboardCaptured,
        bool mouseCaptured
    );

    bool active() const { return active_; }
    const glm::vec3& position() const { return position_; }
    const glm::vec3& spawnPosition() const { return spawnPosition_; }
    glm::vec3 cameraTargetWorld() const;
    glm::vec3 cameraPositionWorld(const TerrainScene& terrain, float deltaSeconds);

private:
    glm::vec3 horizontalForward() const;

    bool active_ = false;
    bool grounded_ = true;
    bool jumpWasDown_ = false;
    bool lookDragging_ = false;
    double lastCursorX_ = 0.0;
    double lastCursorY_ = 0.0;
    glm::vec3 spawnPosition_{0.0f};
    glm::vec3 position_{0.0f};
    glm::vec3 velocity_{0.0f};
    float yawRadians_ = 0.0f;
    float pitchRadians_ = -0.28f;
    bool cameraCollisionStateInitialized_ = false;
    float cameraDistanceFraction_ = 1.0f;
    float cameraHeightOffset_ = 0.0f;
};

} // namespace VulkCore
