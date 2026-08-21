#include "vulkcore/Gameplay/Player.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "vulkcore/Physics/PhysicsWorld.hpp"
#include "vulkcore/Terrain/Terrain.hpp"

#include <algorithm>
#include <cmath>

namespace VulkCore {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kMovementSpeed = 4.5f;
constexpr float kJumpSpeed = 6.2f;
constexpr float kPlayerTerrainClearance = 0.025f;
constexpr float kMouseSensitivity = 0.006f;
constexpr float kCameraDistance = 5.0f;
constexpr float kCameraTargetHeight = 0.92f;
constexpr float kCameraPitchLimit = kPi / 12.0f;
constexpr float kInitialCameraPitch = -kCameraPitchLimit;

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
    grounded_ = true;
    jumpWasDown_ = false;
    lookDragging_ = false;
    spawnPosition_ = glm::vec3(0.0f);
    position_ = glm::vec3(0.0f);
    velocity_ = glm::vec3(0.0f);
}

void PlayerController::reset(
    const TerrainScene& terrain,
    PhysicsWorld& physics,
    const std::optional<glm::vec3>& spawnPosition
) {
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
    jumpWasDown_ = false;
    lookDragging_ = false;
    yawRadians_ = 0.0f;
    pitchRadians_ = kInitialCameraPitch;
    active_ = true;
}

glm::vec3 PlayerController::horizontalForward() const {
    return glm::normalize(glm::vec3(
        std::sin(yawRadians_),
        0.0f,
        -std::cos(yawRadians_)
    ));
}

void PlayerController::update(
    GLFWwindow* window,
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

    if (window != nullptr && !mouseCaptured &&
        glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        double cursorX = 0.0;
        double cursorY = 0.0;
        glfwGetCursorPos(window, &cursorX, &cursorY);
        if (!lookDragging_) {
            lookDragging_ = true;
        } else {
            yawRadians_ -= static_cast<float>(cursorX - lastCursorX_) * kMouseSensitivity;
            pitchRadians_ -= static_cast<float>(cursorY - lastCursorY_) * kMouseSensitivity;
            pitchRadians_ = std::clamp(
                pitchRadians_,
                -kCameraPitchLimit,
                kCameraPitchLimit
            );
        }
        lastCursorX_ = cursorX;
        lastCursorY_ = cursorY;
    } else {
        lookDragging_ = false;
    }

    const bool jumpDown = window != nullptr &&
        glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
    if (!keyboardCaptured && jumpDown && !jumpWasDown_ && grounded_) {
        if (physics.jump(kJumpSpeed)) {
            grounded_ = false;
        }
    }
    jumpWasDown_ = jumpDown;

    glm::vec2 horizontalVelocity(0.0f);
    if (!keyboardCaptured && window != nullptr) {
        const float horizontalInput =
            static_cast<float>(glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) -
            static_cast<float>(glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS);
        const float forwardInput =
            static_cast<float>(glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) -
            static_cast<float>(glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS);
        const glm::vec3 forward = horizontalForward();
        const glm::vec3 right = glm::vec3(-forward.z, 0.0f, forward.x);
        const glm::vec3 inputDirection = forward * forwardInput + right * horizontalInput;
        if (glm::length(inputDirection) > 0.001f) {
            const glm::vec3 movementVelocity =
                glm::normalize(inputDirection) * kMovementSpeed;
            horizontalVelocity = glm::vec2(
                movementVelocity.x,
                movementVelocity.z
            );
        }
    }
    physics.setPlayerHorizontalVelocity(horizontalVelocity);
    physics.simulate(deltaSeconds);
    position_ = physics.playerRenderFootPosition();
    velocity_ = physics.playerLinearVelocity();
    grounded_ = physics.playerGrounded();
}

glm::vec3 PlayerController::cameraTargetWorld() const {
    return position_ + glm::vec3(0.0f, kCameraTargetHeight, 0.0f);
}

glm::vec3 PlayerController::cameraPositionWorld() const {
    const glm::vec3 cameraTarget = cameraTargetWorld();
    const glm::vec3 viewDirection = glm::normalize(
        horizontalForward() * std::cos(pitchRadians_) +
        glm::vec3(0.0f, std::sin(pitchRadians_), 0.0f)
    );
    return cameraTarget - viewDirection * kCameraDistance;
}

} // namespace VulkCore
