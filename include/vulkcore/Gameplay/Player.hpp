#pragma once

#include <cstdint>
#include <optional>
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

enum class PlayerAttack : std::uint8_t {
    None = 0,
    Tornado,
    LightningSlash,
    FlameNova,
};

struct PlayerCameraView {
    glm::vec3 position;
    glm::vec3 target;
};

class PlayerController {
public:
    static constexpr float kBotwLinkHeight = 1.58f;
    static constexpr float kBotwLinkRadius = 0.27f;
    static constexpr float kCameraDistance = 5.0f;

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
    void configureAttackAnimation(PlayerAttack attack, float durationSeconds);
    void update(
        Window* window,
        float deltaSeconds,
        PhysicsWorld& physics,
        bool keyboardCaptured,
        bool mouseCaptured
    );

    bool active() const { return active_; }
    bool clickToMoveEnabled() const { return clickToMove_; }
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
    bool attacking() const { return activeAttack_ != PlayerAttack::None; }
    bool attackTriggered() const { return triggeredAttack_ != PlayerAttack::None; }
    PlayerAttack activeAttack() const { return activeAttack_; }
    PlayerAttack triggeredAttack() const { return triggeredAttack_; }
    float attackAnimationTimeSeconds() const { return attackAnimationTimeSeconds_; }
    PlayerCameraView cameraView(const PhysicsWorld& physics) const;

private:
    glm::vec3 horizontalForward() const;
    float currentFootTraction() const;

    bool active_ = false;
    bool clickToMove_ = false;
    std::optional<glm::vec2> moveDestination_;
    bool debugModeEnabled_ = false;
    bool grounded_ = true;
    bool moving_ = false;
    bool jumpWasDown_ = false;
    bool tornadoAttackWasDown_ = false;
    bool lightningAttackWasDown_ = false;
    bool flameAttackWasDown_ = false;
    PlayerAttack activeAttack_ = PlayerAttack::None;
    PlayerAttack triggeredAttack_ = PlayerAttack::None;
    bool lookWasDown_ = false;
    bool lookDragging_ = false;
    double lastCursorX_ = 0.0;
    double lastCursorY_ = 0.0;
    glm::vec3 spawnPosition_{0.0f};
    glm::vec3 position_{0.0f};
    glm::vec3 velocity_{0.0f};
    float facingYawRadians_ = 0.0f;
    float runAnimationTimeSeconds_ = 0.0f;
    float runCycleDurationSeconds_ = 0.0f;
    float runGroundSpeed_ = 0.0f;
    float attackAnimationTimeSeconds_ = 0.0f;
    float tornadoAttackDurationSeconds_ = 0.85f;
    float lightningAttackDurationSeconds_ = 0.72f;
    float flameAttackDurationSeconds_ = 1.05f;
    std::vector<float> footTractionSamples_;
    float yawRadians_ = 0.0f;
    float cameraElevationRadians_ = 0.0f;
};

} // namespace VulkCore
