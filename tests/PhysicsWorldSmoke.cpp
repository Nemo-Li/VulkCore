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

    for (int step = 0; step < 180; ++step) {
        physics.setPlayerHorizontalVelocity(glm::vec2(2.0f, 0.0f));
        physics.simulate(1.0f / 60.0f);
    }
    physics.setPlayerHorizontalVelocity(glm::vec2(0.0f));
    if (!physics.playerGrounded() || physics.playerFootPosition().x < 2.0f) {
        std::cerr << "player did not cross the terrain chunk boundary: x="
                  << physics.playerFootPosition().x << '\n';
        return 2;
    }

    const glm::vec3 preJumpPosition = physics.playerFootPosition();
    if (!physics.jump(6.2f)) {
        std::cerr << "grounded player could not jump\n";
        return 3;
    }
    physics.simulate(1.0f / 60.0f);
    const glm::vec3 postJumpPosition = physics.playerFootPosition();
    if (postJumpPosition.y <= preJumpPosition.y) {
        std::cerr << "jump did not move the player upward\n";
        return 4;
    }

    physics.simulate(1.0f / 120.0f);
    const glm::vec3 renderPosition = physics.playerRenderFootPosition();
    if (renderPosition.y <= preJumpPosition.y ||
        renderPosition.y >= postJumpPosition.y) {
        std::cerr << "render pose was not interpolated between fixed steps: y="
                  << renderPosition.y << '\n';
        return 5;
    }

    return 0;
}
