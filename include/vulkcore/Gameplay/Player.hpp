#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "vulkcore/Renderer/Vertex.hpp"

#include "vulkcore/Platform/Window.hpp"

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
class PhysicsWorld;

enum class PlayerSkill : std::uint8_t { Q, W, E, R, None };
inline constexpr std::string_view kPlayerSkillKeys = "QWER";
inline constexpr auto kPlayerSkillCount = static_cast<std::size_t>(PlayerSkill::None);

struct PlayerCameraView {
    glm::vec3 position;
    glm::vec3 target;
};

class PlayerController {
public:
    static constexpr float kBotwLinkHeight = 1.58f;
    static constexpr float kBotwLinkRadius = 0.27f;
    static constexpr float kDefaultCameraFollowDistance = 2.0f;
    static constexpr float kMinCameraFollowDistance = 0.75f;
    static constexpr float kMaxCameraFollowDistance = 20.0f;
    static constexpr float kDefaultMoveSpeedMultiplier = 1.0f;
    static constexpr float kMinMoveSpeedMultiplier = 0.1f;
    static constexpr float kMaxMoveSpeedMultiplier = 5.0f;

    void clear();
    void reset(
        const TerrainScene& terrain,
        PhysicsWorld& physics,
        const std::optional<glm::vec3>& spawnPosition = std::nullopt,
        bool clickToMove = false
    );
    bool teleport(
        const TerrainScene& terrain,
        PhysicsWorld& physics,
        const glm::vec2& worldPosition
    );
    bool moveSpawn(const TerrainScene& terrain, PhysicsWorld& physics, const glm::vec2& worldPosition);
    void configureRunGait(
        float cycleDurationSeconds,
        float groundSpeed,
        std::vector<float> footTractionSamples
    );
    void configureSkillAnimation(PlayerSkill skill, float durationSeconds);
    void update(
        Window* window,
        float deltaSeconds,
        PhysicsWorld& physics,
        bool keyboardCaptured,
        bool mouseCaptured,
        const std::array<bool, kPlayerSkillCount>& skillEnabled
    );

    bool active() const { return active_; }
    bool clickToMoveEnabled() const { return clickToMove_; }
    float moveSpeedMultiplier() const { return moveSpeedMultiplier_; }
    void setMoveSpeedMultiplier(float multiplier);
    std::optional<float> cameraFollowDistance() const { return cameraFollowDistance_; }
    void setCameraFollowDistance(float distance);
    void recenterFollowCamera();
    void setMoveDestination(const glm::vec2& destination) { moveDestination_ = destination; }
    void cancelMove() { moveDestination_.reset(); }
    bool debugModeEnabled() const { return debugModeEnabled_; }
    void setDebugModeEnabled(bool enabled) { debugModeEnabled_ = enabled; }
    const glm::vec3& position() const { return position_; }
    const glm::vec3& spawnPosition() const { return spawnPosition_; }
    const glm::vec3& velocity() const { return velocity_; }
    bool grounded() const { return grounded_; }
    float facingYawRadians() const { return facingYawRadians_; }
    bool moving() const { return moving_; }
    float runAnimationTimeSeconds() const { return runAnimationTimeSeconds_; }
    bool attacking() const { return activeSkill_ != PlayerSkill::None; }
    PlayerSkill activeSkill() const { return activeSkill_; }
    PlayerSkill triggeredSkill() const { return triggeredSkill_; }
    float attackAnimationTimeSeconds() const { return attackAnimationTimeSeconds_; }
    PlayerCameraView cameraView(const PhysicsWorld& physics) const;

private:
    struct FollowCameraPose {
        float yaw;
        float elevation;
        float distance;
    };

    FollowCameraPose desiredFollowCameraPose() const;
    float followCameraClearance(const PhysicsWorld& physics, float yaw, float distance) const;
    void updateFollowCamera(const PhysicsWorld& physics, float deltaSeconds);
    glm::vec3 horizontalForward() const;
    float currentFootTraction() const;

    bool active_ = false;
    bool clickToMove_ = false;
    std::optional<glm::vec2> moveDestination_;
    bool debugModeEnabled_ = false;
    bool grounded_ = true;
    bool moving_ = false;
    bool jumpWasDown_ = false;
    std::array<bool, kPlayerSkillCount> skillWasDown_{};
    PlayerSkill activeSkill_ = PlayerSkill::None;
    PlayerSkill triggeredSkill_ = PlayerSkill::None;
    bool lookWasDown_ = false;
    bool lookDragging_ = false;
    bool cameraOrbitOverridden_ = false;
    double lastCursorX_ = 0.0;
    double lastCursorY_ = 0.0;
    glm::vec3 spawnPosition_{0.0f};
    glm::vec3 position_{0.0f};
    glm::vec3 velocity_{0.0f};
    float facingYawRadians_ = 0.0f;
    float runAnimationTimeSeconds_ = 0.0f;
    float runCycleDurationSeconds_ = 0.0f;
    float runGroundSpeed_ = 0.0f;
    float moveSpeedMultiplier_ = kDefaultMoveSpeedMultiplier;
    float attackAnimationTimeSeconds_ = 0.0f;
    static constexpr std::array<float, kPlayerSkillCount> kDefaultSkillAnimationSeconds{0.72f, 0.6f, 1.05f, 0.85f};
    std::array<float, kPlayerSkillCount> skillAnimationSeconds_ = kDefaultSkillAnimationSeconds;
    std::vector<float> footTractionSamples_;
    std::optional<float> cameraFollowDistance_;
    std::optional<FollowCameraPose> followCameraPose_;
    float yawRadians_ = 0.0f;
    float cameraElevationRadians_ = 0.0f;
};

} // namespace VulkCore
