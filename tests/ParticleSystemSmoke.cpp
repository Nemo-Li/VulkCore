#include "vulkcore/Renderer/ParticleSystem.hpp"

#include <cmath>
#include <iostream>

int main() {
    VulkCore::ParticleSystemSettings settings;
    settings.playOnAwake = false;
    settings.looping = false;
    settings.maxParticles = 128;
    settings.duration = 0.3f;
    settings.emissionRate = 100.0f;
    settings.startLifetime = 0.5f;
    settings.lifetimeRandomness = 0.0f;
    settings.startSpeed = 0.25f;
    settings.speedRandomness = 0.0f;
    settings.startSize = 0.08f;
    settings.endSize = 0.02f;
    settings.shapeRadius = 0.3f;
    settings.shape = VulkCore::ParticleSystemSettings::Shape::Tornado;

    VulkCore::ParticleSystem system;
    system.setSettings(settings);
    system.restart();
    system.update(0.1f);
    system.buildVertices(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    if (system.aliveCount() == 0 ||
        system.vertices().size() != system.aliveCount() *
            VulkCore::ParticleSystem::kVerticesPerParticle) {
        std::cerr << "tornado did not produce renderable billboard vertices\n";
        return 1;
    }
    for (const VulkCore::ParticleVertex& vertex : system.vertices()) {
        if (!std::isfinite(vertex.position.x) ||
            !std::isfinite(vertex.position.y) ||
            !std::isfinite(vertex.position.z)) {
            std::cerr << "tornado produced a non-finite vertex\n";
            return 2;
        }
    }

    for (int step = 0; step < 80; ++step) {
        system.update(0.02f);
    }
    if (system.aliveCount() != 0 || system.playing()) {
        std::cerr << "one-shot tornado did not finish cleanly\n";
        return 3;
    }

    settings.shape = VulkCore::ParticleSystemSettings::Shape::Ring;
    settings.duration = 0.2f;
    settings.startLifetime = 0.4f;
    system.setSettings(settings);
    system.restart();
    system.update(0.1f);
    system.buildVertices(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    if (system.aliveCount() == 0 || system.vertices().empty()) {
        std::cerr << "ring nova did not produce renderable billboard vertices\n";
        return 4;
    }
    // An effect sprite selects one atlas cell at a time through the same
    // particle buffers. Check that reaching the next frame does not emit a second quad.
    settings.duration = 0.4f;
    settings.position = glm::vec3(0.0f);
    settings.flipbook = {2, 2, 4, 192, 4, 2.0f};
    system.setSettings(settings);
    system.restart();
    const auto buildSprite = [&] {
        system.buildVertices(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    };
    buildSprite();
    if (system.vertices().size() != 6 || system.vertices().front().texCoord.x <= 0.0f ||
        system.vertices().front().texCoord.x >= 0.5f) {
        std::cerr << "effect sprite did not start inside its first atlas cell\n";
        return 5;
    }
    system.update(0.1f);
    buildSprite();
    if (system.vertices().size() != 6 || system.vertices().front().texCoord.x <= 0.5f) {
        std::cerr << "effect sprite did not advance to the next atlas cell\n";
        return 6;
    }
    for (int step = 0; step < 4; ++step) system.update(0.1f);
    buildSprite();
    if (system.playing() || !system.vertices().empty()) {
        std::cerr << "one-shot effect sprite did not stop at the end of its timeline\n";
        return 7;
    }
    return 0;
}
