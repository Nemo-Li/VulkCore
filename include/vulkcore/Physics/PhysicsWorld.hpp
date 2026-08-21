#pragma once

#include <memory>

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

    // Player positions use the bottom center of the upright capsule, matching
    // the renderer's existing player mesh coordinate convention.
    void createPlayer(const glm::vec3& footPosition, float height, float radius);
    void removePlayer();
    bool hasPlayer() const;

    void setPlayerHorizontalVelocity(const glm::vec2& velocity);
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
