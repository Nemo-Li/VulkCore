#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include <glm/glm.hpp>

namespace VulkCore {

struct TerrainMeshData;

// Owns the PhysX SDK objects used by the current scene. Rendering-facing
// headers never expose PhysX types, which keeps the dependency behind a small
// engine boundary and makes a future backend replacement possible.
class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // Removes scene actors while retaining the initialized PhysX SDK.
    void clear();

    // Cooks each rendered terrain chunk into a static triangle-mesh collider.
    // Per-chunk AABBs and mesh BVHs keep broad/mid-phase rejection local.
    void setTerrain(const TerrainMeshData& mesh);

    // Distance a sphere can travel along a unit direction before touching terrain.
    float terrainSweepDistance(
        const glm::vec3& origin,
        const glm::vec3& direction,
        float maxDistance,
        float radius
    ) const;

    // Imported models use upright box colliders. Repositioning resets their velocity.
    // Patrolling models can drive the collider pose from their route.
    void placeGravityBox(std::uint32_t id, const glm::vec3& center, const glm::vec3& halfExtents,
        bool kinematic = false);
    void removeGravityBox(std::uint32_t id);
    std::optional<glm::vec3> gravityBoxRenderCenter(std::uint32_t id) const;

    // Player positions use the bottom center of the upright capsule, matching
    // the renderer's existing player mesh coordinate convention.
    void createPlayer(const glm::vec3& footPosition, float height, float radius);
    void removePlayer();
    bool hasPlayer() const;

    // Replaces debug movement with a friction-limited foot impulse request.
    void setPlayerFootTraction(
        const glm::vec2& direction,
        float targetSpeed,
        float contactStrength
    );
    // Applies the requested horizontal velocity on the ground and in the air.
    // A zero ascent speed leaves vertical motion to gravity and collisions.
    void setPlayerDebugMovement(const glm::vec2& horizontalVelocity, float ascentSpeed);
    bool jump(float verticalSpeed);
    void simulate(float deltaSeconds);

    glm::vec3 playerFootPosition() const;
    // Smooth rendering pose interpolated between the two latest fixed steps.
    glm::vec3 playerRenderFootPosition() const;
    glm::vec3 playerLinearVelocity() const;
    bool playerGrounded() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace VulkCore
