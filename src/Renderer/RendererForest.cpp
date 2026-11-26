#include "RendererImplementation.hpp"

#include "vulkcore/Renderer/SceneProjection.hpp"
#include "vulkcore/Terrain/Terrain.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <iterator>
#include <random>

namespace VulkCore {
namespace {

float groveDensity(glm::vec2 position, float cellSize, std::uint32_t seed) {
    const auto grid = position / cellSize;
    const auto cell = glm::ivec2(glm::floor(grid));
    const auto weight = glm::smoothstep(glm::vec2(0), glm::vec2(1), glm::fract(grid));
    const auto sample = [seed](glm::ivec2 corner) {
        auto hash = static_cast<std::uint32_t>(corner.x) * 0x9e3779b9u ^
            static_cast<std::uint32_t>(corner.y) * 0x85ebca6bu ^ seed;
        hash = (hash ^ (hash >> 16)) * 0x7feb352du;
        hash = (hash ^ (hash >> 15)) * 0x846ca68bu;
        return static_cast<float>((hash ^ (hash >> 16)) >> 8) / 16777216.0f;
    };
    return glm::mix(glm::mix(sample(cell), sample(cell + glm::ivec2(1, 0)), weight.x),
        glm::mix(sample(cell + glm::ivec2(0, 1)), sample(cell + glm::ivec2(1, 1)), weight.x), weight.y);
}

} // namespace

void Renderer::buildForest(const TerrainScene& terrain) {
    const auto& patch = *forestPatch_;
    struct PreparedSpecies {
        const ForestSpecies* config;
        std::size_t modelIndex;
    };
    std::vector<PreparedSpecies> species;
    std::vector<float> weights;
    for (const auto& config : forestSpecies_) {
        try {
            auto mesh = loadSbfresMesh(config.model, {}, {}, false, nullptr, config.group);
            if (mesh.skinnedMeshCount != 0 || !mesh.hierarchy.hasBounds) {
                throw std::runtime_error("forest species requires a static model with bounds");
            }
            const auto vertexOffset = static_cast<std::uint32_t>(meshData_.vertices.size());
            const auto indexOffset = static_cast<std::uint32_t>(meshData_.indices.size());
            const auto materialOffset = static_cast<std::uint32_t>(meshData_.materials.size());
            meshData_.vertices.insert(meshData_.vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
            meshData_.bindPoseVertices.insert(meshData_.bindPoseVertices.end(), mesh.vertices.begin(), mesh.vertices.end());
            meshData_.skinSourceVertices.insert(meshData_.skinSourceVertices.end(), mesh.vertices.begin(), mesh.vertices.end());
            for (const auto index : mesh.indices) meshData_.indices.push_back(vertexOffset + index);
            meshData_.materials.insert(meshData_.materials.end(), std::make_move_iterator(mesh.materials.begin()),
                std::make_move_iterator(mesh.materials.end()));
            for (auto& part : mesh.parts) {
                part.firstIndex += indexOffset;
                part.materialIndex += materialOffset;
                for (auto& range : part.lowerLods) range.firstIndex += indexOffset;
            }
            species.push_back({&config, forestModels_.size()});
            weights.push_back(config.weight);
            forestModels_.push_back({{mesh.hierarchy.boundsMinimum, mesh.hierarchy.boundsMaximum}, std::move(mesh.parts)});
        } catch (const std::exception& error) {
            const auto warning = "Forest species " + config.group + ": " + error.what();
            sceneResourceWarnings_.push_back(warning);
            std::cerr << "warning: " << warning << '\n';
        }
    }
    if (species.empty()) return;

    const auto view = playerController_.cameraView(*physicsWorld_);
    const glm::vec3 forward3 = glm::normalize(view.target - view.position);
    const glm::vec2 forward = glm::normalize(glm::vec2(forward3.x, forward3.z));
    const glm::vec2 right(-forward.y, forward.x);
    const glm::vec2 eye(view.position.x, view.position.z);
    const glm::vec2 player(playerController_.position().x, playerController_.position().z);
    constexpr float aspect = static_cast<float>(kWindowWidth) / kWindowHeight;
    auto projection = sceneProjection(aspect, 0.02f, patch.farDistance + 60.0f, false, kPlayerSceneVerticalFov);
    projection[0][0] /= kPlayerHorizontalFovScale;
    const auto clipFromWorld = projection * glm::lookAt(view.position, view.target, glm::vec3(0, 1, 0));
    const float halfWidth = 1.0f / projection[0][0];

    std::mt19937 random(patch.seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::discrete_distribution<std::size_t> chooseSpecies(weights.begin(), weights.end());
    struct PlantedTree { glm::vec2 position; float spacing; };
    std::vector<PlantedTree> planted;
    planted.reserve(patch.treeCount);
    forestInstances_.reserve(patch.treeCount);
    const auto onLand = [&](glm::vec2 position, float height) {
        const auto water = terrain.waterHeightAtWorld(position);
        return (!water || height > *water + 0.2f) && !terrain.roadContains(position);
    };
    const auto visibleCrown = [&](glm::vec3 crown) {
        const glm::vec4 clip = clipFromWorld * glm::vec4(crown, 1.0f);
        if (clip.w <= 0.0f || std::abs(clip.x) > 0.92f * clip.w || std::abs(clip.y) > 0.95f * clip.w) return false;
        const float distance = glm::distance(view.position, crown);
        // Keep fine samples by the camera and canopy, with larger steps across the distant valley.
        for (float step = 3.0f; step < distance - 2.0f;) {
            const auto probe = glm::mix(view.position, crown, step / distance);
            if (const auto height = terrain.renderedHeightAtWorld({probe.x, probe.z}); height && *height > probe.y) {
                return false;
            }
            step += std::clamp(std::min(step, distance - step) * 0.04f, 2.5f, 14.0f);
        }
        return true;
    };

    // Allocate distinct quotas across the whole view so the distant slopes receive their own stands.
    constexpr std::array bandEnds{kForestLod1Distance - 6.0f, kForestLod2Distance - 6.0f,
        250.0f, 550.0f, 1050.0f, 1800.0f};
    constexpr std::array bandWeights{1.0f, 2.0f, 3.0f, 8.5f, 15.5f, 26.0f, 44.0f};
    std::array<float, bandWeights.size() + 1> distanceBreaks{};
    distanceBreaks.front() = patch.nearDistance;
    distanceBreaks.back() = patch.farDistance;
    for (std::size_t i = 0; i < bandEnds.size(); ++i) {
        distanceBreaks[i + 1] = std::clamp(bandEnds[i], patch.nearDistance, patch.farDistance);
    }
    float totalWeight = 0.0f;
    for (std::size_t band = 0; band < bandWeights.size(); ++band) {
        if (distanceBreaks[band] < distanceBreaks[band + 1]) totalWeight += bandWeights[band];
    }

    float cumulativeWeight = 0.0f;
    for (std::size_t band = 0; band < bandWeights.size(); ++band) {
        const float start = distanceBreaks[band];
        const float end = distanceBreaks[band + 1];
        if (start == end) continue;
        cumulativeWeight += bandWeights[band];
        const auto goal = static_cast<std::size_t>(std::lround(patch.treeCount * cumulativeWeight / totalWeight));
        const auto countBeforeBand = planted.size();
        const std::size_t attemptLimit = 60 * (goal - countBeforeBand);
        for (std::size_t attempt = 0; attempt < attemptLimit && planted.size() < goal; ++attempt) {
            const auto& selected = species[chooseSpecies(random)];
            const auto& model = forestModels_[selected.modelIndex];
            const float scale = std::lerp(selected.config->minimumScale, selected.config->maximumScale, unit(random));
            const float depth = std::lerp(start, end, unit(random));
            const float column = unit(random) * 2.0f - 1.0f;
            // Leave the player and the central approach visible in the foreground.
            if (band == 0 && std::abs(column) < 0.42f) continue;
            const auto position = eye + forward * depth + right * (depth * halfWidth * column);
            if (glm::distance(position, player) < 5.5f) continue;
            if (depth >= bandEnds[2]) {
                const float density = groveDensity(position, 135.0f, patch.seed);
                if (unit(random) > std::lerp(0.035f, 1.0f, glm::smoothstep(0.36f, 0.76f, density))) continue;
            }
            const auto height = terrain.renderedHeightAtWorld(position);
            if (!height || !onLand(position, *height)) continue;
            const float radius = 0.5f * std::max(model.bounds.maximum.x - model.bounds.minimum.x,
                model.bounds.maximum.z - model.bounds.minimum.z) * scale;
            const float spacing = std::max(3.0f, radius * 0.83f);
            if (std::any_of(planted.begin(), planted.end(), [&](const auto& tree) {
                    return glm::distance(position, tree.position) < spacing + tree.spacing;
                })) continue;
            const float rootClearance = std::clamp(radius * 0.35f, 1.25f, 2.75f);
            bool solidGround = true;
            for (const auto direction : std::array<glm::vec2, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
                const auto sample = position + direction * rootClearance;
                const auto ground = terrain.renderedHeightAtWorld(sample);
                if (!ground || std::abs(*ground - *height) > rootClearance * 0.7f || !onLand(sample, *ground)) {
                    solidGround = false;
                    break;
                }
            }
            const float crownY = *height + model.bounds.maximum.y * scale * 0.68f;
            if (!solidGround || !visibleCrown({position.x, crownY, position.y})) continue;
            const float yaw = unit(random) * 2.0f * glm::pi<float>();
            const auto transform = glm::translate(glm::mat4(1.0f), glm::vec3(position.x, *height, position.y)) *
                glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(scale));
            forestInstances_.push_back({selected.modelIndex, transform});
            planted.push_back({position, spacing});
        }
        std::cerr << "Forest depth " << start << "–" << end << " m: " << planted.size() - countBeforeBand << " trees\n";
    }
    std::cerr << "Planted " << forestInstances_.size() << " trees in the opening view from "
              << forestModels_.size() << " SBFRES model groups\n";
}

} // namespace VulkCore
