#include "vulkcore/Physics/PhysicsWorld.hpp"
#include "vulkcore/Terrain/Terrain.hpp"

#include <cmath>
#include <iostream>

namespace {

VulkCore::TerrainMeshData makeGroundPlane() {
    VulkCore::TerrainMeshData mesh;
    mesh.vertices = {
        {glm::vec3(-10.0f, 0.0f, -10.0f)},
        {glm::vec3(-10.0f, 0.0f, 10.0f)},
        {glm::vec3(0.0f, 0.0f, -10.0f)},
        {glm::vec3(0.0f, 0.0f, 10.0f)},
        {glm::vec3(0.0f, 0.0f, -10.0f)},
        {glm::vec3(0.0f, 0.0f, 10.0f)},
        {glm::vec3(10.0f, 2.0f, -10.0f)},
        {glm::vec3(10.0f, 2.0f, 10.0f)}
    };
    mesh.indices = {
        0, 1, 2, 2, 1, 3,
        4, 5, 6, 6, 5, 7
    };
    VulkCore::TerrainChunkMesh leftChunk;
    leftChunk.indexOffset = 0;
    leftChunk.indexCount = 6;
    leftChunk.boundsMinimum = glm::vec3(-10.0f, 0.0f, -10.0f);
    leftChunk.boundsMaximum = glm::vec3(0.0f, 0.0f, 10.0f);
    VulkCore::TerrainChunkMesh rightChunk;
    rightChunk.indexOffset = 6;
    rightChunk.indexCount = 6;
    rightChunk.boundsMinimum = glm::vec3(0.0f, 0.0f, -10.0f);
    rightChunk.boundsMaximum = glm::vec3(10.0f, 2.0f, 10.0f);
    mesh.chunks = {leftChunk, rightChunk};
    return mesh;
}

} // namespace

int main() {
    VulkCore::PhysicsWorld physics;
    physics.setTerrain(makeGroundPlane());
    physics.createPlayer(glm::vec3(-2.0f, 2.0f, 0.0f), 1.58f, 0.27f);

    for (int step = 0; step < 180; ++step) {
        physics.simulate(1.0f / 60.0f);
    }

    const glm::vec3 restingPosition = physics.playerFootPosition();
    if (!physics.playerGrounded() || std::abs(restingPosition.y) > 0.08f) {
        std::cerr << "player did not settle on the terrain: y="
                  << restingPosition.y << '\n';
        return 1;
    }

    // The camera sweep should stop before its center reaches the rising slope.
    constexpr float kCameraSweepDistance = 5.0f;
    constexpr float kCameraRadius = 0.25f;
    const float slopeDistance = physics.terrainSweepDistance(
        glm::vec3(0.0f, 0.92f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        kCameraSweepDistance,
        kCameraRadius
    );
    const float unobstructedDistance = physics.terrainSweepDistance(
        restingPosition + glm::vec3(0.0f, 0.92f, 0.0f),
        glm::vec3(-1.0f, 0.0f, 0.0f),
        kCameraSweepDistance,
        kCameraRadius
    );
    const float initialOverlapDistance = physics.terrainSweepDistance(
        glm::vec3(-2.0f, 0.125f, 0.0f),
        glm::vec3(-1.0f, 0.0f, 0.0f),
        kCameraSweepDistance,
        kCameraRadius
    );
    if (std::abs(slopeDistance - 3.325f) > 0.02f ||
        unobstructedDistance != kCameraSweepDistance || initialOverlapDistance != 0.0f) {
        std::cerr << "unexpected camera clearance: slope=" << slopeDistance
                  << ", clear=" << unobstructedDistance
                  << ", overlap=" << initialOverlapDistance << '\n';
        return 8;
    }

    physics.setPlayerFootTraction(glm::vec2(1.0f, 0.0f), 2.0f, 1.0f);
    physics.simulate(1.0f / 60.0f);
    const float firstTractionStepSpeed = physics.playerLinearVelocity().x;
    if (firstTractionStepSpeed <= 0.0f || firstTractionStepSpeed >= 2.0f) {
        std::cerr << "foot traction did not accelerate progressively: vx="
                  << firstTractionStepSpeed << '\n';
        return 2;
    }
    for (int step = 1; step < 180; ++step) {
        physics.simulate(1.0f / 60.0f);
    }
    physics.setPlayerFootTraction(glm::vec2(0.0f), 0.0f, 1.0f);
    if (!physics.playerGrounded() || physics.playerFootPosition().x < 2.0f) {
        std::cerr << "player did not cross the terrain chunk boundary: x="
                  << physics.playerFootPosition().x << '\n';
        return 3;
    }
    for (int step = 0; step < 60; ++step) {
        physics.simulate(1.0f / 60.0f);
    }
    if (std::abs(physics.playerLinearVelocity().x) > 0.1f) {
        std::cerr << "static foot friction did not stop the player: vx="
                  << physics.playerLinearVelocity().x << '\n';
        return 4;
    }

    const glm::vec3 preJumpPosition = physics.playerFootPosition();
    if (!physics.jump(6.2f)) {
        std::cerr << "grounded player could not jump\n";
        return 5;
    }
    physics.simulate(1.0f / 60.0f);
    const glm::vec3 postJumpPosition = physics.playerFootPosition();
    if (postJumpPosition.y <= preJumpPosition.y) {
        std::cerr << "jump did not move the player upward\n";
        return 6;
    }

    physics.simulate(1.0f / 120.0f);
    const glm::vec3 renderPosition = physics.playerRenderFootPosition();
    if (renderPosition.y <= preJumpPosition.y ||
        renderPosition.y >= postJumpPosition.y) {
        std::cerr << "render pose was not interpolated between fixed steps: y="
                  << renderPosition.y << '\n';
        return 7;
    }

    return 0;
}
