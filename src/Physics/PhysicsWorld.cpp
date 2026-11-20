#include "vulkcore/Physics/PhysicsWorld.hpp"

#include "vulkcore/Terrain/Terrain.hpp"

#include <PxPhysicsAPI.h>
#include <cooking/PxCooking.h>
#include <extensions/PxDefaultCpuDispatcher.h>
#include <extensions/PxDefaultSimulationFilterShader.h>
#include <extensions/PxExtensionsAPI.h>
#include <extensions/PxRigidBodyExt.h>
#include <extensions/PxSimpleFactory.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

namespace VulkCore {

namespace {

constexpr float kFixedTimeStep = 1.0f / 60.0f;
constexpr float kMaximumAccumulatedTime = 0.25f;
constexpr float kGravity = 18.0f;
constexpr float kGroundProbeDistance = 0.12f;
constexpr float kMinimumGroundNormalY = 0.35f;
constexpr float kMaximumGroundedUpwardSpeed = 1.0f;
constexpr float kPlayerMass = 70.0f;
constexpr float kPlayerFootStaticFriction = 1.35f;
constexpr float kPlayerFootDynamicFriction = 1.05f;
constexpr float kMinimumAnimatedFootContact = 0.2f;

physx::PxVec3 toPhysX(const glm::vec3& value) {
    return physx::PxVec3(value.x, value.y, value.z);
}

glm::vec3 fromPhysX(const physx::PxVec3& value) {
    return glm::vec3(value.x, value.y, value.z);
}

std::uint32_t workerThreadCount() {
    const std::uint32_t hardwareThreads = std::thread::hardware_concurrency();
    if (hardwareThreads <= 1) {
        return 1;
    }
    return std::clamp(hardwareThreads - 1u, 1u, 4u);
}

} // namespace

struct PhysicsWorld::Impl {
    struct GravityBox {
        physx::PxRigidDynamic* actor = nullptr;
        glm::vec3 halfExtents{0.0f};
        glm::vec3 previousCenter{0.0f};
        glm::vec3 currentCenter{0.0f};
    };

    physx::PxDefaultAllocator allocator;
    physx::PxDefaultErrorCallback errorCallback;
    physx::PxFoundation* foundation = nullptr;
    physx::PxPhysics* physics = nullptr;
    physx::PxDefaultCpuDispatcher* dispatcher = nullptr;
    physx::PxScene* scene = nullptr;
    physx::PxMaterial* material = nullptr;
    physx::PxMaterial* playerFootMaterial = nullptr;
    std::vector<physx::PxRigidStatic*> terrainActors;
    std::vector<physx::PxTriangleMesh*> terrainMeshes;
    std::unordered_map<std::uint32_t, GravityBox> gravityBoxes;
    physx::PxRigidDynamic* playerActor = nullptr;
    float playerHeight = 0.0f;
    float playerRadius = 0.0f;
    float accumulator = 0.0f;
    glm::vec3 previousPlayerFootPosition{0.0f};
    glm::vec3 currentPlayerFootPosition{0.0f};
    bool playerPoseInitialized = false;
    bool extensionsInitialized = false;
    bool grounded = false;
    glm::vec2 playerMovementDirection{0.0f};
    float playerTargetSpeed = 0.0f;
    float playerFootContactStrength = 1.0f;
    std::optional<physx::PxVec3> playerDebugVelocity;

    Impl() {
        try {
            foundation = PxCreateFoundation(
                PX_PHYSICS_VERSION,
                allocator,
                errorCallback
            );
            if (foundation == nullptr) {
                throw std::runtime_error("failed to create the PhysX foundation");
            }

            physics = PxCreatePhysics(
                PX_PHYSICS_VERSION,
                *foundation,
                physx::PxTolerancesScale()
            );
            if (physics == nullptr) {
                throw std::runtime_error("failed to create the PhysX SDK");
            }

            extensionsInitialized = PxInitExtensions(*physics, nullptr);
            if (!extensionsInitialized) {
                throw std::runtime_error("failed to initialize the PhysX extensions");
            }

            dispatcher = physx::PxDefaultCpuDispatcherCreate(workerThreadCount());
            if (dispatcher == nullptr) {
                throw std::runtime_error("failed to create the PhysX CPU dispatcher");
            }

            physx::PxSceneDesc sceneDescription(physics->getTolerancesScale());
            sceneDescription.gravity = physx::PxVec3(0.0f, -kGravity, 0.0f);
            sceneDescription.cpuDispatcher = dispatcher;
            sceneDescription.filterShader = physx::PxDefaultSimulationFilterShader;
            sceneDescription.flags |= physx::PxSceneFlag::eENABLE_CCD;
            if (!sceneDescription.isValid()) {
                throw std::runtime_error("the PhysX scene description is invalid");
            }
            scene = physics->createScene(sceneDescription);
            if (scene == nullptr) {
                throw std::runtime_error("failed to create the PhysX scene");
            }

            material = physics->createMaterial(0.8f, 0.65f, 0.0f);
            if (material == nullptr) {
                throw std::runtime_error("failed to create the default PhysX material");
            }
            playerFootMaterial = physics->createMaterial(
                kPlayerFootStaticFriction,
                kPlayerFootDynamicFriction,
                0.0f
            );
            if (playerFootMaterial == nullptr) {
                throw std::runtime_error("failed to create the player foot material");
            }
            playerFootMaterial->setFrictionCombineMode(physx::PxCombineMode::eMAX);
        } catch (...) {
            shutdown();
            throw;
        }
    }

    ~Impl() {
        shutdown();
    }

    void shutdown() {
        clear();
        if (playerFootMaterial != nullptr) {
            playerFootMaterial->release();
            playerFootMaterial = nullptr;
        }
        if (material != nullptr) {
            material->release();
            material = nullptr;
        }
        if (scene != nullptr) {
            scene->release();
            scene = nullptr;
        }
        if (dispatcher != nullptr) {
            dispatcher->release();
            dispatcher = nullptr;
        }
        if (extensionsInitialized) {
            PxCloseExtensions();
            extensionsInitialized = false;
        }
        if (physics != nullptr) {
            physics->release();
            physics = nullptr;
        }
        if (foundation != nullptr) {
            foundation->release();
            foundation = nullptr;
        }
    }

    void removePlayer() {
        if (playerActor != nullptr) {
            playerActor->release();
            playerActor = nullptr;
        }
        playerHeight = 0.0f;
        playerRadius = 0.0f;
        grounded = false;
        previousPlayerFootPosition = glm::vec3(0.0f);
        currentPlayerFootPosition = glm::vec3(0.0f);
        playerPoseInitialized = false;
        playerMovementDirection = glm::vec2(0.0f);
        playerTargetSpeed = 0.0f;
        playerFootContactStrength = 1.0f;
        playerDebugVelocity.reset();
    }

    glm::vec3 rawPlayerFootPosition() const {
        if (playerActor == nullptr) {
            return glm::vec3(0.0f);
        }
        glm::vec3 position = fromPhysX(playerActor->getGlobalPose().p);
        position.y -= playerHeight * 0.5f;
        return position;
    }

    void removeTerrain() {
        for (physx::PxRigidStatic* actor : terrainActors) {
            actor->release();
        }
        terrainActors.clear();
        for (physx::PxTriangleMesh* mesh : terrainMeshes) {
            mesh->release();
        }
        terrainMeshes.clear();
    }

    void clear() {
        removePlayer();
        for (const auto& entry : gravityBoxes) {
            entry.second.actor->release();
        }
        gravityBoxes.clear();
        removeTerrain();
        accumulator = 0.0f;
    }

    void updateGroundedState() {
        grounded = false;
        if (scene == nullptr || playerActor == nullptr || playerHeight <= 0.0f) {
            return;
        }

        const physx::PxVec3 center = playerActor->getGlobalPose().p;
        if (playerActor->getLinearVelocity().y > kMaximumGroundedUpwardSpeed) {
            return;
        }
        const physx::PxQueryFilterData queryFilter(physx::PxQueryFlag::eSTATIC | physx::PxQueryFlag::eDYNAMIC);
        const float rayDistance = playerHeight * 0.5f + kGroundProbeDistance;
        const float offset = playerRadius * 0.7f;
        const std::array<physx::PxVec3, 5> probeOffsets{
            physx::PxVec3(0.0f, 0.0f, 0.0f),
            physx::PxVec3(offset, 0.0f, 0.0f),
            physx::PxVec3(-offset, 0.0f, 0.0f),
            physx::PxVec3(0.0f, 0.0f, offset),
            physx::PxVec3(0.0f, 0.0f, -offset)
        };
        for (const physx::PxVec3& probeOffset : probeOffsets) {
            physx::PxRaycastBuffer hit;
            const bool hasHit = scene->raycast(
                center + probeOffset,
                physx::PxVec3(0.0f, -1.0f, 0.0f),
                rayDistance,
                hit,
                physx::PxHitFlag::ePOSITION | physx::PxHitFlag::eNORMAL,
                queryFilter
            );
            if (hasHit && hit.hasBlock &&
                hit.block.normal.y >= kMinimumGroundNormalY) {
                grounded = true;
                return;
            }
        }
    }

    void applyPlayerDebugMovement(physx::PxVec3 desiredVelocity) {
        if (playerActor == nullptr) {
            return;
        }

        const physx::PxVec3 currentVelocity = playerActor->getLinearVelocity();
        if (desiredVelocity.y == 0.0f) {
            desiredVelocity.y = currentVelocity.y;
        }
        if (desiredVelocity != currentVelocity) {
            playerActor->setLinearVelocity(desiredVelocity, true);
        }
    }

    void applyPlayerFootTraction() {
        if (playerActor == nullptr || !grounded) {
            return;
        }

        const bool wantsMovement = playerTargetSpeed > 0.0f &&
            glm::dot(playerMovementDirection, playerMovementDirection) > 0.0f;
        const glm::vec2 desiredVelocity = wantsMovement
            ? playerMovementDirection * playerTargetSpeed
            : glm::vec2(0.0f);
        const physx::PxVec3 currentVelocity = playerActor->getLinearVelocity();
        glm::vec2 velocityChange = desiredVelocity - glm::vec2(
            currentVelocity.x,
            currentVelocity.z
        );
        const float velocityChangeLength = glm::length(velocityChange);
        if (velocityChangeLength <= std::numeric_limits<float>::epsilon()) {
            return;
        }

        // Coulomb friction bounds the impulse available from a planted foot.
        // Releasing the movement keys uses static friction to settle the body;
        // while running, the sampled animation contact controls how much of
        // the dynamic-friction budget the current stance can use.
        const float friction = wantsMovement
            ? kPlayerFootDynamicFriction
            : kPlayerFootStaticFriction;
        const float contactStrength = wantsMovement
            ? std::clamp(
                  playerFootContactStrength,
                  kMinimumAnimatedFootContact,
                  1.0f
              )
            : 1.0f;
        const float maximumVelocityChange =
            friction * kGravity * contactStrength * kFixedTimeStep;
        if (velocityChangeLength > maximumVelocityChange) {
            velocityChange *= maximumVelocityChange / velocityChangeLength;
        }
        playerActor->addForce(
            physx::PxVec3(velocityChange.x, 0.0f, velocityChange.y),
            physx::PxForceMode::eVELOCITY_CHANGE,
            true
        );
    }
};

PhysicsWorld::PhysicsWorld()
    : impl_(std::make_unique<Impl>()) {}

PhysicsWorld::~PhysicsWorld() = default;

void PhysicsWorld::clear() {
    impl_->clear();
}

void PhysicsWorld::setTerrain(const TerrainMeshData& mesh) {
    impl_->removeTerrain();
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        throw std::invalid_argument("terrain collision mesh cannot be empty");
    }
    if (mesh.indices.size() % 3 != 0) {
        throw std::invalid_argument("terrain collision indices must contain triangles");
    }
    if (mesh.vertices.size() > std::numeric_limits<physx::PxU32>::max() ||
        mesh.indices.size() / 3 > std::numeric_limits<physx::PxU32>::max()) {
        throw std::overflow_error("terrain collision mesh exceeds PhysX 32-bit limits");
    }

    struct IndexRange {
        std::size_t offset = 0;
        std::size_t count = 0;
    };
    std::vector<IndexRange> collisionRanges;
    collisionRanges.reserve(std::max<std::size_t>(mesh.chunks.size(), 1));
    for (const TerrainChunkMesh& chunk : mesh.chunks) {
        if (chunk.indexCount == 0) {
            continue;
        }
        if (chunk.indexOffset > mesh.indices.size() ||
            chunk.indexCount > mesh.indices.size() - chunk.indexOffset ||
            chunk.indexCount % 3 != 0) {
            throw std::invalid_argument("terrain chunk contains an invalid index range");
        }
        collisionRanges.push_back({chunk.indexOffset, chunk.indexCount});
    }
    if (collisionRanges.empty()) {
        collisionRanges.push_back({0, mesh.indices.size()});
    }
    impl_->terrainActors.reserve(collisionRanges.size());
    impl_->terrainMeshes.reserve(collisionRanges.size());

    try {
        for (const IndexRange& range : collisionRanges) {
            const auto rangeBegin = mesh.indices.begin() +
                static_cast<std::ptrdiff_t>(range.offset);
            const auto rangeEnd = rangeBegin + static_cast<std::ptrdiff_t>(range.count);
            const auto [minimumIndexIt, maximumIndexIt] =
                std::minmax_element(rangeBegin, rangeEnd);
            if (maximumIndexIt == rangeEnd || *maximumIndexIt >= mesh.vertices.size()) {
                throw std::invalid_argument("terrain collision index is out of bounds");
            }
            const std::uint32_t minimumIndex = *minimumIndexIt;
            const std::uint32_t maximumIndex = *maximumIndexIt;
            const std::size_t vertexCount =
                static_cast<std::size_t>(maximumIndex - minimumIndex) + 1;
            if (vertexCount > std::numeric_limits<physx::PxU32>::max() ||
                range.count / 3 > std::numeric_limits<physx::PxU32>::max()) {
                throw std::overflow_error(
                    "terrain chunk collision mesh exceeds PhysX 32-bit limits"
                );
            }

            std::vector<physx::PxVec3> vertices;
            vertices.reserve(vertexCount);
            for (std::uint32_t index = minimumIndex; index <= maximumIndex; ++index) {
                vertices.push_back(toPhysX(mesh.vertices[index].position));
            }
            std::vector<std::uint32_t> indices;
            indices.reserve(range.count);
            for (auto indexIt = rangeBegin; indexIt != rangeEnd; ++indexIt) {
                indices.push_back(*indexIt - minimumIndex);
            }

            physx::PxTriangleMeshDesc meshDescription;
            meshDescription.points.count = static_cast<physx::PxU32>(vertices.size());
            meshDescription.points.stride = sizeof(physx::PxVec3);
            meshDescription.points.data = vertices.data();
            meshDescription.triangles.count =
                static_cast<physx::PxU32>(indices.size() / 3);
            meshDescription.triangles.stride = 3 * sizeof(std::uint32_t);
            meshDescription.triangles.data = indices.data();
            if (!meshDescription.isValid()) {
                throw std::runtime_error(
                    "rendered terrain chunk cannot be represented as a PhysX mesh"
                );
            }

            physx::PxCookingParams cookingParameters(
                impl_->physics->getTolerancesScale()
            );
            cookingParameters.suppressTriangleMeshRemapTable = true;
            cookingParameters.midphaseDesc = physx::PxMeshMidPhase::eBVH34;
            physx::PxTriangleMeshCookingResult::Enum cookingResult =
                physx::PxTriangleMeshCookingResult::eFAILURE;
            physx::PxTriangleMesh* collisionMesh = PxCreateTriangleMesh(
                cookingParameters,
                meshDescription,
                impl_->physics->getPhysicsInsertionCallback(),
                &cookingResult
            );
            if (collisionMesh == nullptr ||
                cookingResult == physx::PxTriangleMeshCookingResult::eFAILURE) {
                if (collisionMesh != nullptr) {
                    collisionMesh->release();
                }
                throw std::runtime_error(
                    "PhysX failed to cook a terrain chunk collision mesh"
                );
            }

            physx::PxRigidStatic* collisionActor = impl_->physics->createRigidStatic(
                physx::PxTransform(physx::PxIdentity)
            );
            if (collisionActor == nullptr) {
                collisionMesh->release();
                throw std::runtime_error("PhysX failed to create a terrain chunk actor");
            }

            const physx::PxTriangleMeshGeometry terrainGeometry(
                collisionMesh,
                physx::PxMeshScale(),
                physx::PxMeshGeometryFlag::eDOUBLE_SIDED
            );
            physx::PxShape* terrainShape = impl_->physics->createShape(
                terrainGeometry,
                *impl_->material,
                true
            );
            if (terrainShape == nullptr) {
                collisionActor->release();
                collisionMesh->release();
                throw std::runtime_error(
                    "PhysX failed to create a terrain chunk collision shape"
                );
            }
            terrainShape->setContactOffset(0.04f);
            collisionActor->attachShape(*terrainShape);
            terrainShape->release();
            impl_->scene->addActor(*collisionActor);
            impl_->terrainActors.push_back(collisionActor);
            impl_->terrainMeshes.push_back(collisionMesh);
        }
    } catch (...) {
        impl_->removeTerrain();
        throw;
    }

    impl_->updateGroundedState();
}

float PhysicsWorld::terrainSweepDistance(
    const glm::vec3& origin,
    const glm::vec3& direction,
    float maxDistance,
    float radius
) const {
    // Terrain is static. Excluding dynamic bodies keeps the player capsule
    // from blocking a sweep that starts at the camera pivot inside it.
    const physx::PxQueryFilterData filter(physx::PxQueryFlag::eSTATIC);
    physx::PxSweepBuffer hit;
    impl_->scene->sweep(
        physx::PxSphereGeometry(radius),
        physx::PxTransform(toPhysX(origin)),
        toPhysX(direction),
        maxDistance,
        hit,
        physx::PxHitFlag::eDEFAULT,
        filter
    );
    return hit.hasBlock ? hit.block.distance : maxDistance;
}

void PhysicsWorld::placeGravityBox(std::uint32_t id, const glm::vec3& center, const glm::vec3& halfExtents) {
    const physx::PxTransform pose(toPhysX(center));
    const physx::PxBoxGeometry geometry(toPhysX(halfExtents));
    if (!pose.isValid() || !geometry.isValid()) {
        throw std::invalid_argument("invalid gravity box bounds");
    }

    auto& box = impl_->gravityBoxes[id];
    if (!box.actor) {
        box.actor = physx::PxCreateDynamic(*impl_->physics, pose, geometry, *impl_->material, 50.0f);
        if (!box.actor) {
            impl_->gravityBoxes.erase(id);
            throw std::runtime_error("PhysX failed to create a model gravity body");
        }
        box.actor->setRigidDynamicLockFlags(
            physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_X |
            physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y |
            physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z
        );
        box.actor->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_SPECULATIVE_CCD, true);
        impl_->scene->addActor(*box.actor);
    } else {
        if (box.halfExtents != halfExtents) {
            physx::PxShape* shape = nullptr;
            box.actor->getShapes(&shape, 1);
            shape->setGeometry(geometry);
            physx::PxRigidBodyExt::updateMassAndInertia(*box.actor, 50.0f);
        }
        box.actor->setGlobalPose(pose);
        box.actor->setLinearVelocity(physx::PxVec3(0.0f));
    }
    box.halfExtents = halfExtents;
    box.previousCenter = center;
    box.currentCenter = center;
}

void PhysicsWorld::removeGravityBox(std::uint32_t id) {
    const auto box = impl_->gravityBoxes.find(id);
    if (box != impl_->gravityBoxes.end()) {
        box->second.actor->release();
        impl_->gravityBoxes.erase(box);
    }
}

std::optional<glm::vec3> PhysicsWorld::gravityBoxRenderCenter(std::uint32_t id) const {
    const auto box = impl_->gravityBoxes.find(id);
    if (box == impl_->gravityBoxes.end()) return std::nullopt;
    const float interpolation = std::clamp(impl_->accumulator / kFixedTimeStep, 0.0f, 1.0f);
    return glm::mix(box->second.previousCenter, box->second.currentCenter, interpolation);
}

void PhysicsWorld::createPlayer(
    const glm::vec3& footPosition,
    float height,
    float radius
) {
    impl_->removePlayer();
    if (!std::isfinite(height) || !std::isfinite(radius) ||
        height <= 0.0f || radius <= 0.0f || height < radius * 2.0f) {
        throw std::invalid_argument("player capsule dimensions are invalid");
    }

    const glm::vec3 center = footPosition + glm::vec3(0.0f, height * 0.5f, 0.0f);
    impl_->playerActor = impl_->physics->createRigidDynamic(
        physx::PxTransform(toPhysX(center))
    );
    if (impl_->playerActor == nullptr) {
        throw std::runtime_error("PhysX failed to create the player actor");
    }

    const float halfCylinderHeight = std::max(0.0f, height * 0.5f - radius);
    const physx::PxCapsuleGeometry playerGeometry(radius, halfCylinderHeight);
    physx::PxShape* playerShape = impl_->physics->createShape(
        playerGeometry,
        *impl_->playerFootMaterial,
        true
    );
    if (playerShape == nullptr) {
        impl_->removePlayer();
        throw std::runtime_error("PhysX failed to create the player collision shape");
    }
    playerShape->setLocalPose(physx::PxTransform(
        physx::PxQuat(physx::PxHalfPi, physx::PxVec3(0.0f, 0.0f, 1.0f))
    ));
    playerShape->setContactOffset(0.04f);
    // Ground probes can hit placed models and should ignore the player's own capsule.
    playerShape->setFlag(physx::PxShapeFlag::eSCENE_QUERY_SHAPE, false);
    impl_->playerActor->attachShape(*playerShape);
    playerShape->release();

    impl_->playerActor->setMass(kPlayerMass);
    impl_->playerActor->setLinearDamping(0.0f);
    impl_->playerActor->setAngularDamping(1.0f);
    impl_->playerActor->setRigidDynamicLockFlags(
        physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_X |
        physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y |
        physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z
    );
    impl_->playerActor->setRigidBodyFlag(
        physx::PxRigidBodyFlag::eENABLE_CCD,
        true
    );
    impl_->scene->addActor(*impl_->playerActor);
    impl_->playerHeight = height;
    impl_->playerRadius = radius;
    impl_->previousPlayerFootPosition = impl_->rawPlayerFootPosition();
    impl_->currentPlayerFootPosition = impl_->previousPlayerFootPosition;
    impl_->playerPoseInitialized = true;
    impl_->updateGroundedState();
}

void PhysicsWorld::removePlayer() {
    impl_->removePlayer();
}

bool PhysicsWorld::hasPlayer() const {
    return impl_->playerActor != nullptr;
}

void PhysicsWorld::setPlayerFootTraction(
    const glm::vec2& direction,
    float targetSpeed,
    float contactStrength
) {
    if (impl_->playerDebugVelocity && impl_->playerActor != nullptr) {
        // Clear debug horizontal momentum before returning to normal locomotion.
        const float verticalSpeed = impl_->playerActor->getLinearVelocity().y;
        impl_->playerActor->setLinearVelocity(physx::PxVec3(0.0f, verticalSpeed, 0.0f), true);
    }
    impl_->playerDebugVelocity.reset();
    if (!std::isfinite(targetSpeed) || targetSpeed <= 0.0f ||
        !std::isfinite(direction.x) || !std::isfinite(direction.y) ||
        glm::length(direction) <= std::numeric_limits<float>::epsilon()) {
        impl_->playerMovementDirection = glm::vec2(0.0f);
        impl_->playerTargetSpeed = 0.0f;
        impl_->playerFootContactStrength = 1.0f;
        return;
    }
    impl_->playerMovementDirection = glm::normalize(direction);
    impl_->playerTargetSpeed = targetSpeed;
    impl_->playerFootContactStrength = std::isfinite(contactStrength)
        ? std::clamp(contactStrength, 0.0f, 1.0f)
        : 1.0f;
}

void PhysicsWorld::setPlayerDebugMovement(const glm::vec2& horizontalVelocity, float ascentSpeed) {
    impl_->playerDebugVelocity.emplace(
        std::isfinite(horizontalVelocity.x) ? horizontalVelocity.x : 0.0f,
        std::isfinite(ascentSpeed) ? std::max(0.0f, ascentSpeed) : 0.0f,
        std::isfinite(horizontalVelocity.y) ? horizontalVelocity.y : 0.0f
    );
}

bool PhysicsWorld::jump(float verticalSpeed) {
    if (impl_->playerActor == nullptr || !impl_->grounded || verticalSpeed <= 0.0f) {
        return false;
    }
    physx::PxVec3 linearVelocity = impl_->playerActor->getLinearVelocity();
    linearVelocity.y = verticalSpeed;
    impl_->playerActor->setLinearVelocity(linearVelocity, true);
    impl_->grounded = false;
    return true;
}

void PhysicsWorld::simulate(float deltaSeconds) {
    if (impl_->scene == nullptr) {
        return;
    }
    deltaSeconds = std::clamp(deltaSeconds, 0.0f, 0.1f);
    impl_->accumulator = std::min(
        impl_->accumulator + deltaSeconds,
        kMaximumAccumulatedTime
    );
    while (impl_->accumulator >= kFixedTimeStep) {
        if (impl_->playerDebugVelocity) {
            impl_->applyPlayerDebugMovement(*impl_->playerDebugVelocity);
        } else {
            impl_->applyPlayerFootTraction();
        }
        if (impl_->playerPoseInitialized) {
            impl_->previousPlayerFootPosition = impl_->currentPlayerFootPosition;
        }
        impl_->scene->simulate(kFixedTimeStep);
        if (!impl_->scene->fetchResults(true)) {
            throw std::runtime_error("PhysX failed to fetch simulation results");
        }
        if (impl_->playerPoseInitialized) {
            impl_->currentPlayerFootPosition = impl_->rawPlayerFootPosition();
        }
        for (auto& entry : impl_->gravityBoxes) {
            auto& box = entry.second;
            box.previousCenter = box.currentCenter;
            box.currentCenter = fromPhysX(box.actor->getGlobalPose().p);
        }
        impl_->updateGroundedState();
        impl_->accumulator -= kFixedTimeStep;
    }
    impl_->updateGroundedState();
}

glm::vec3 PhysicsWorld::playerFootPosition() const {
    return impl_->rawPlayerFootPosition();
}

glm::vec3 PhysicsWorld::playerRenderFootPosition() const {
    if (!impl_->playerPoseInitialized) {
        return impl_->rawPlayerFootPosition();
    }
    const float interpolation = std::clamp(
        impl_->accumulator / kFixedTimeStep,
        0.0f,
        1.0f
    );
    return glm::mix(
        impl_->previousPlayerFootPosition,
        impl_->currentPlayerFootPosition,
        interpolation
    );
}

glm::vec3 PhysicsWorld::playerLinearVelocity() const {
    return impl_->playerActor == nullptr
        ? glm::vec3(0.0f)
        : fromPhysX(impl_->playerActor->getLinearVelocity());
}

bool PhysicsWorld::playerGrounded() const {
    return impl_->grounded;
}

} // namespace VulkCore
