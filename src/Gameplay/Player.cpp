#include "vulkcore/Gameplay/Player.hpp"

#include "vulkcore/Platform/Window.hpp"

#include "vulkcore/Physics/PhysicsWorld.hpp"
#include "vulkcore/Terrain/Terrain.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace VulkCore {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kPlayerTurnSpeed = 10.0f;
constexpr float kJumpSpeed = 6.2f;
constexpr float kDebugMovementSpeedMultiplier = 4.0f;
constexpr float kDebugAscentSpeed = 6.0f;
constexpr float kPlayerTerrainClearance = 0.025f;
constexpr float kMouseSensitivity = 0.006f;
constexpr float kCameraTargetHeight = 0.92f;
constexpr float kCameraCollisionRadius = 0.25f;
// Negative orbit elevation lets the player tilt up toward the sky. The terrain sweep still limits the camera arm.
constexpr float kMinCameraElevation = -35.0f * kPi / 180.0f;
constexpr float kMaxCameraElevation = 65.0f * kPi / 180.0f;
constexpr float kInitialCameraElevation = 15.0f * kPi / 180.0f;
constexpr float kFallbackRunCycleSeconds = 0.9f;
constexpr float kFallbackRunStrideHeightRatio = 1.3f;

std::optional<float> playerGroundHeight(
    const TerrainScene& terrain,
    const glm::vec2& position
) {
    std::optional<float> maximumHeight = terrain.renderedHeightAtWorld(position);
    constexpr int kDirectionCount = 16;
    constexpr int kRingCount = 2;
    for (int ring = 1; ring <= kRingCount; ++ring) {
        const float radius = PlayerController::kBotwLinkRadius *
            static_cast<float>(ring) / static_cast<float>(kRingCount);
        for (int directionIndex = 0;
             directionIndex < kDirectionCount;
             ++directionIndex) {
            const float angle = 2.0f * kPi *
                static_cast<float>(directionIndex) /
                static_cast<float>(kDirectionCount);
            const glm::vec2 probePosition = position + radius * glm::vec2(
                std::cos(angle),
                std::sin(angle)
            );
            const std::optional<float> height =
                terrain.renderedHeightAtWorld(probePosition);
            if (height.has_value() &&
                (!maximumHeight.has_value() || *height > *maximumHeight)) {
                maximumHeight = height;
            }
        }
    }
    if (maximumHeight.has_value()) {
        *maximumHeight += kPlayerTerrainClearance;
    }
    return maximumHeight;
}

} // namespace

CapsuleMeshData createCapsuleMesh(
    float height,
    float radius,
    std::uint32_t radialSegments,
    std::uint32_t hemisphereSegments
) {
    height = std::max(height, 0.01f);
    radius = std::clamp(radius, 0.005f, height * 0.5f);
    radialSegments = std::max(radialSegments, 3u);
    hemisphereSegments = std::max(hemisphereSegments, 2u);

    struct Ring {
        float y = 0.0f;
        float radius = 0.0f;
        float normalY = 0.0f;
        float normalRadius = 0.0f;
    };
    std::vector<Ring> rings;
    rings.push_back({0.0f, 0.0f, -1.0f, 0.0f});

    for (std::uint32_t index = 1; index <= hemisphereSegments; ++index) {
        const float angle = -0.5f * kPi + 0.5f * kPi *
            static_cast<float>(index) / static_cast<float>(hemisphereSegments);
        rings.push_back({
            radius + std::sin(angle) * radius,
            std::cos(angle) * radius,
            std::sin(angle),
            std::cos(angle)
        });
    }

    const float cylinderHeight = height - radius * 2.0f;
    if (cylinderHeight > 0.0001f) {
        rings.push_back({height - radius, radius, 0.0f, 1.0f});
    }

    for (std::uint32_t index = 1; index <= hemisphereSegments; ++index) {
        const float angle = 0.5f * kPi *
            static_cast<float>(index) / static_cast<float>(hemisphereSegments);
        rings.push_back({
            height - radius + std::sin(angle) * radius,
            std::cos(angle) * radius,
            std::sin(angle),
            std::cos(angle)
        });
    }
    rings.push_back({height, 0.0f, 1.0f, 0.0f});

    CapsuleMeshData mesh;
    mesh.vertices.reserve(rings.size() * radialSegments);
    for (std::size_t ringIndex = 0; ringIndex < rings.size(); ++ringIndex) {
        const Ring& ring = rings[ringIndex];
        for (std::uint32_t segment = 0; segment < radialSegments; ++segment) {
            const float angle = 2.0f * kPi * static_cast<float>(segment) /
                static_cast<float>(radialSegments);
            const float cosine = std::cos(angle);
            const float sine = std::sin(angle);
            const glm::vec3 normal = glm::normalize(glm::vec3(
                cosine * ring.normalRadius,
                ring.normalY,
                sine * ring.normalRadius
            ));
            mesh.vertices.push_back({
                glm::vec3(cosine * ring.radius, ring.y, sine * ring.radius),
                normal,
                glm::vec2(
                    static_cast<float>(segment) / static_cast<float>(radialSegments),
                    static_cast<float>(ringIndex) / static_cast<float>(rings.size() - 1)
                ),
                glm::vec4(0.18f, 0.38f, 0.82f, 1.0f)
            });
        }
    }

    for (std::size_t ringIndex = 0; ringIndex + 1 < rings.size(); ++ringIndex) {
        for (std::uint32_t segment = 0; segment < radialSegments; ++segment) {
            const std::uint32_t nextSegment = (segment + 1) % radialSegments;
            const std::uint32_t first = static_cast<std::uint32_t>(
                ringIndex * radialSegments + segment
            );
            const std::uint32_t firstNext = static_cast<std::uint32_t>(
                ringIndex * radialSegments + nextSegment
            );
            const std::uint32_t second = static_cast<std::uint32_t>(
                (ringIndex + 1) * radialSegments + segment
            );
            const std::uint32_t secondNext = static_cast<std::uint32_t>(
                (ringIndex + 1) * radialSegments + nextSegment
            );
            mesh.indices.insert(mesh.indices.end(), {
                first, firstNext, second,
                second, firstNext, secondNext
            });
        }
    }
    return mesh;
}

void PlayerController::clear() {
    active_ = false;
    debugModeEnabled_ = false;
    grounded_ = true;
    moving_ = false;
    jumpWasDown_ = false;
    tornadoAttackWasDown_ = false;
    lightningAttackWasDown_ = false;
    flameAttackWasDown_ = false;
    activeAttack_ = PlayerAttack::None;
    triggeredAttack_ = PlayerAttack::None;
    lookWasDown_ = false;
    lookDragging_ = false;
    spawnPosition_ = glm::vec3(0.0f);
    position_ = glm::vec3(0.0f);
    velocity_ = glm::vec3(0.0f);
    facingYawRadians_ = 0.0f;
    runAnimationTimeSeconds_ = 0.0f;
    runCycleDurationSeconds_ = kFallbackRunCycleSeconds;
    runGroundSpeed_ = kBotwLinkHeight * kFallbackRunStrideHeightRatio /
        kFallbackRunCycleSeconds;
    footTractionSamples_.assign(1, 1.0f);
}

void PlayerController::reset(
    const TerrainScene& terrain,
    PhysicsWorld& physics,
    const std::optional<glm::vec3>& spawnPosition
) {
    if (runCycleDurationSeconds_ <= 0.0f || runGroundSpeed_ <= 0.0f) {
        configureRunGait(
            kFallbackRunCycleSeconds,
            kBotwLinkHeight * kFallbackRunStrideHeightRatio /
                kFallbackRunCycleSeconds,
            {1.0f}
        );
    }
    spawnPosition_ = terrain.centerPosition();
    if (spawnPosition.has_value()) {
        spawnPosition_.x = spawnPosition->x;
        spawnPosition_.z = spawnPosition->z;
    }
    const std::optional<float> groundHeight = playerGroundHeight(
        terrain,
        glm::vec2(spawnPosition_.x, spawnPosition_.z)
    );
    if (groundHeight.has_value()) {
        spawnPosition_.y = *groundHeight;
    }
    physics.createPlayer(spawnPosition_, kBotwLinkHeight, kBotwLinkRadius);
    position_ = physics.playerFootPosition();
    velocity_ = physics.playerLinearVelocity();
    grounded_ = physics.playerGrounded();
    moving_ = false;
    jumpWasDown_ = false;
    tornadoAttackWasDown_ = false;
    lightningAttackWasDown_ = false;
    flameAttackWasDown_ = false;
    activeAttack_ = PlayerAttack::None;
    triggeredAttack_ = PlayerAttack::None;
    attackAnimationTimeSeconds_ = 0.0f;
    lookWasDown_ = false;
    lookDragging_ = false;
    // Imported player models use local +Z as forward. The initial camera
    // looks toward world -Z, so start the visual facing the same direction.
    facingYawRadians_ = kPi;
    runAnimationTimeSeconds_ = 0.0f;
    yawRadians_ = 0.0f;
    cameraElevationRadians_ = kInitialCameraElevation;
    debugModeEnabled_ = false;
    active_ = true;
}

bool PlayerController::teleport(
    const TerrainScene& terrain,
    PhysicsWorld& physics,
    const glm::vec2& worldPosition
) {
    if (!active_ || !std::isfinite(worldPosition.x) ||
        !std::isfinite(worldPosition.y)) {
        return false;
    }
    const std::optional<float> groundHeight = playerGroundHeight(
        terrain,
        worldPosition
    );
    if (!groundHeight.has_value()) {
        return false;
    }

    physics.createPlayer(
        glm::vec3(worldPosition.x, *groundHeight, worldPosition.y),
        kBotwLinkHeight,
        kBotwLinkRadius
    );
    physics.setPlayerFootTraction(glm::vec2(0.0f), 0.0f, 1.0f);
    position_ = physics.playerFootPosition();
    velocity_ = physics.playerLinearVelocity();
    grounded_ = physics.playerGrounded();
    moving_ = false;
    activeAttack_ = PlayerAttack::None;
    triggeredAttack_ = PlayerAttack::None;
    attackAnimationTimeSeconds_ = 0.0f;
    runAnimationTimeSeconds_ = 0.0f;
    lookDragging_ = false;
    return true;
}

bool PlayerController::moveSpawn(const TerrainScene& terrain, PhysicsWorld& physics, const glm::vec2& worldPosition) {
    const bool wasDragging = lookDragging_;
    if (!teleport(terrain, physics, worldPosition)) return false;
    lookDragging_ = wasDragging;
    spawnPosition_ = position_;
    return true;
}

void PlayerController::configureRunGait(
    float cycleDurationSeconds,
    float groundSpeed,
    std::vector<float> footTractionSamples
) {
    if (!std::isfinite(cycleDurationSeconds) || cycleDurationSeconds <= 0.0f ||
        !std::isfinite(groundSpeed) || groundSpeed <= 0.0f) {
        return;
    }
    runCycleDurationSeconds_ = cycleDurationSeconds;
    runGroundSpeed_ = groundSpeed;
    footTractionSamples_ = std::move(footTractionSamples);
    if (footTractionSamples_.empty()) {
        footTractionSamples_.assign(1, 1.0f);
    }
    for (float& traction : footTractionSamples_) {
        traction = std::isfinite(traction)
            ? std::clamp(traction, 0.0f, 1.0f)
            : 1.0f;
    }
    runAnimationTimeSeconds_ = 0.0f;
}

void PlayerController::configureAttackAnimation(
    PlayerAttack attack,
    float durationSeconds
) {
    if (!std::isfinite(durationSeconds) || durationSeconds <= 0.0f) {
        return;
    }
    switch (attack) {
    case PlayerAttack::Tornado:
        tornadoAttackDurationSeconds_ = durationSeconds;
        break;
    case PlayerAttack::LightningSlash:
        lightningAttackDurationSeconds_ = durationSeconds;
        break;
    case PlayerAttack::FlameNova:
        flameAttackDurationSeconds_ = durationSeconds;
        break;
    case PlayerAttack::None:
        break;
    }
}

float PlayerController::currentFootTraction() const {
    if (footTractionSamples_.empty() || runCycleDurationSeconds_ <= 0.0f) {
        return 1.0f;
    }
    const float phase = std::clamp(
        runAnimationTimeSeconds_ / runCycleDurationSeconds_,
        0.0f,
        1.0f
    );
    const float samplePosition = phase *
        static_cast<float>(footTractionSamples_.size());
    const std::size_t leftIndex = static_cast<std::size_t>(samplePosition) %
        footTractionSamples_.size();
    const std::size_t rightIndex = (leftIndex + 1) % footTractionSamples_.size();
    const float factor = samplePosition - std::floor(samplePosition);
    return glm::mix(
        footTractionSamples_[leftIndex],
        footTractionSamples_[rightIndex],
        factor
    );
}

glm::vec3 PlayerController::horizontalForward() const {
    return glm::normalize(glm::vec3(
        std::sin(yawRadians_),
        0.0f,
        -std::cos(yawRadians_)
    ));
}

void PlayerController::update(
    Window* window,
    float deltaSeconds,
    PhysicsWorld& physics,
    bool keyboardCaptured,
    bool mouseCaptured
) {
    if (!active_) {
        return;
    }
    deltaSeconds = std::clamp(deltaSeconds, 0.0f, 0.1f);
    grounded_ = physics.playerGrounded();
    triggeredAttack_ = PlayerAttack::None;

    const bool tornadoAttackDown = window != nullptr &&
        keyPressed(window, Key::Tornado);
    const bool lightningAttackDown = window != nullptr &&
        keyPressed(window, Key::Lightning);
    const bool flameAttackDown = window != nullptr &&
        keyPressed(window, Key::Flame);
    if (!keyboardCaptured) {
        if (lightningAttackDown && !lightningAttackWasDown_) {
            triggeredAttack_ = PlayerAttack::LightningSlash;
        } else if (flameAttackDown && !flameAttackWasDown_) {
            triggeredAttack_ = PlayerAttack::FlameNova;
        } else if (tornadoAttackDown && !tornadoAttackWasDown_) {
            triggeredAttack_ = PlayerAttack::Tornado;
        }
    }
    tornadoAttackWasDown_ = tornadoAttackDown;
    lightningAttackWasDown_ = lightningAttackDown;
    flameAttackWasDown_ = flameAttackDown;
    if (triggeredAttack_ != PlayerAttack::None) {
        activeAttack_ = triggeredAttack_;
        attackAnimationTimeSeconds_ = 0.0f;
    }
    if (activeAttack_ != PlayerAttack::None) {
        attackAnimationTimeSeconds_ += deltaSeconds;
        float attackDuration = tornadoAttackDurationSeconds_;
        if (activeAttack_ == PlayerAttack::LightningSlash) {
            attackDuration = lightningAttackDurationSeconds_;
        } else if (activeAttack_ == PlayerAttack::FlameNova) {
            attackDuration = flameAttackDurationSeconds_;
        }
        if (attackAnimationTimeSeconds_ >= attackDuration) {
            attackAnimationTimeSeconds_ = attackDuration;
            activeAttack_ = PlayerAttack::None;
        }
    }

    const bool lookDown = lookPressed(window);
    if (!lookDown) {
        lookDragging_ = false;
    } else if (!lookWasDown_) {
        lookDragging_ = !mouseCaptured;
        if (lookDragging_) {
            cursorPosition(window, &lastCursorX_, &lastCursorY_);
        }
    } else if (lookDragging_) {
        double cursorX = 0.0;
        double cursorY = 0.0;
        cursorPosition(window, &cursorX, &cursorY);
        yawRadians_ = std::remainder(
            yawRadians_ - static_cast<float>(cursorX - lastCursorX_) * kMouseSensitivity,
            2.0f * kPi
        );
        cameraElevationRadians_ = std::clamp(
            cameraElevationRadians_ + static_cast<float>(cursorY - lastCursorY_) * kMouseSensitivity,
            kMinCameraElevation,
            kMaxCameraElevation
        );
        lastCursorX_ = cursorX;
        lastCursorY_ = cursorY;
    }
    lookWasDown_ = lookDown;

    const bool jumpDown = window != nullptr &&
        keyPressed(window, Key::Jump);
    if (!debugModeEnabled_ && !keyboardCaptured && jumpDown && !jumpWasDown_ && grounded_) {
        if (physics.jump(kJumpSpeed)) {
            grounded_ = false;
        }
    }
    jumpWasDown_ = jumpDown;

    glm::vec2 movementDirection2D(0.0f);
    moving_ = false;
    if (!attacking() && !keyboardCaptured && window != nullptr) {
        const bool moveLeft =
            keyPressed(window, Key::A) ||
            keyPressed(window, Key::Left);
        const bool moveRight =
            keyPressed(window, Key::D) ||
            keyPressed(window, Key::Right);
        const bool moveForward =
            keyPressed(window, Key::W) ||
            keyPressed(window, Key::Up);
        const bool moveBackward =
            keyPressed(window, Key::S) ||
            keyPressed(window, Key::Down);
        const MovementInput analog = analogMovement(window);
        const float horizontalInput = analog.right +
            static_cast<float>(moveRight) - static_cast<float>(moveLeft);
        const float forwardInput = analog.forward +
            static_cast<float>(moveForward) - static_cast<float>(moveBackward);
        const glm::vec3 forward = horizontalForward();
        const glm::vec3 right = glm::vec3(-forward.z, 0.0f, forward.x);
        const glm::vec3 inputDirection = forward * forwardInput + right * horizontalInput;
        if (glm::length(inputDirection) > 0.001f) {
            const glm::vec3 movementDirection = glm::normalize(inputDirection);
            movementDirection2D = glm::vec2(
                movementDirection.x,
                movementDirection.z
            );
            moving_ = true;
            const float targetFacingYaw = std::atan2(
                movementDirection.x,
                movementDirection.z
            );
            const float facingDelta = std::remainder(
                targetFacingYaw - facingYawRadians_,
                2.0f * kPi
            );
            const float maximumTurn = kPlayerTurnSpeed * deltaSeconds;
            facingYawRadians_ += std::clamp(
                facingDelta,
                -maximumTurn,
                maximumTurn
            );
            facingYawRadians_ = std::remainder(facingYawRadians_, 2.0f * kPi);
        }
    }
    const float speedMultiplier = debugModeEnabled_ ? kDebugMovementSpeedMultiplier : 1.0f;
    if (moving_ && runCycleDurationSeconds_ > 0.0f) {
        runAnimationTimeSeconds_ = std::fmod(
            runAnimationTimeSeconds_ + deltaSeconds * speedMultiplier,
            runCycleDurationSeconds_
        );
    } else {
        runAnimationTimeSeconds_ = 0.0f;
    }
    if (debugModeEnabled_) {
        physics.setPlayerDebugMovement(
            movementDirection2D * (runGroundSpeed_ * speedMultiplier),
            !keyboardCaptured && jumpDown ? kDebugAscentSpeed : 0.0f
        );
    } else {
        physics.setPlayerFootTraction(
            movementDirection2D,
            moving_ ? runGroundSpeed_ : 0.0f,
            moving_ ? currentFootTraction() : 1.0f
        );
    }
    physics.simulate(deltaSeconds);
    position_ = physics.playerRenderFootPosition();
    velocity_ = physics.playerLinearVelocity();
    grounded_ = physics.playerGrounded();
}

PlayerCameraView PlayerController::cameraView(const PhysicsWorld& physics) const {
    const glm::vec3 pivot = position_ + glm::vec3(0.0f, kCameraTargetHeight, 0.0f);
    const glm::vec3 orbitDirection =
        -horizontalForward() * std::cos(cameraElevationRadians_) +
        glm::vec3(0.0f, std::sin(cameraElevationRadians_), 0.0f);
    const float distance = physics.terrainSweepDistance(
        pivot, orbitDirection, kCameraDistance, kCameraCollisionRadius
    );
    const glm::vec3 eye = pivot + orbitDirection * distance;
    // Keep the view direction valid even when terrain forces the eye to the pivot.
    return {eye, eye - orbitDirection * kCameraDistance};
}

} // namespace VulkCore
