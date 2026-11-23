#include "vulkcore/Renderer/ParticleSystem.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/gtc/constants.hpp>

namespace VulkCore {

VkVertexInputBindingDescription ParticleVertex::bindingDescription() {
    VkVertexInputBindingDescription description{};
    description.binding = 0;
    description.stride = sizeof(ParticleVertex);
    description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return description;
}

std::array<VkVertexInputAttributeDescription, 3> ParticleVertex::attributeDescriptions() {
    std::array<VkVertexInputAttributeDescription, 3> descriptions{};

    descriptions[0].binding = 0;
    descriptions[0].location = 0;
    descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    descriptions[0].offset = offsetof(ParticleVertex, position);

    descriptions[1].binding = 0;
    descriptions[1].location = 1;
    descriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
    descriptions[1].offset = offsetof(ParticleVertex, texCoord);

    descriptions[2].binding = 0;
    descriptions[2].location = 2;
    descriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    descriptions[2].offset = offsetof(ParticleVertex, color);

    return descriptions;
}

ParticleSystem::ParticleSystem() {
    setSettings(settings_);
}

const ParticleSystemSettings& ParticleSystem::settings() const {
    return settings_;
}

ParticleSystemSettings& ParticleSystem::settings() {
    return settings_;
}

bool ParticleFlipbook::valid() const {
    return columns > 0 && columns <= 16 && rows > 0 && rows <= 16 &&
        frames > 0 && frames <= columns * rows &&
        cellPixels >= 8 && cellPixels <= 256 && gutterPixels < cellPixels / 2 &&
        std::isfinite(width) && width > 0.0f && width <= 500.0f;
}

ParticleSystemSettings ParticleSystem::sanitizeSettings(ParticleSystemSettings settings) {
    if (!settings.flipbook.valid()) settings.flipbook = {};
    settings.maxParticles = std::clamp(
        settings.maxParticles,
        1u,
        kMaximumSupportedParticles
    );
    settings.duration = std::clamp(settings.duration, 0.05f, settings.flipbook.frames ? 120.0f : 60.0f);
    settings.emissionRate = std::clamp(settings.emissionRate, 0.0f, 2000.0f);
    settings.startLifetime = std::clamp(settings.startLifetime, 0.02f, 60.0f);
    settings.lifetimeRandomness = std::clamp(settings.lifetimeRandomness, 0.0f, 1.0f);
    settings.startSpeed = std::clamp(settings.startSpeed, 0.0f, 20.0f);
    settings.speedRandomness = std::clamp(settings.speedRandomness, 0.0f, 1.0f);
    settings.startSize = std::clamp(settings.startSize, 0.00001f, 2.0f);
    settings.endSize = std::clamp(settings.endSize, 0.00001f, 2.0f);
    settings.rotationSpeedDegrees = std::clamp(
        settings.rotationSpeedDegrees,
        -720.0f,
        720.0f
    );
    settings.gravityModifier = std::clamp(settings.gravityModifier, -4.0f, 4.0f);
    settings.spreadDegrees = std::clamp(settings.spreadDegrees, 0.0f, 89.0f);
    settings.shapeRadius = std::clamp(settings.shapeRadius, 0.0f, 2.0f);
    settings.startColor = glm::clamp(settings.startColor, glm::vec4(0.0f), glm::vec4(1.0f));
    settings.endColor = glm::clamp(settings.endColor, glm::vec4(0.0f), glm::vec4(1.0f));
    if (glm::length(settings.emissionDirection) < 0.001f) {
        settings.emissionDirection = glm::vec3(0.0f, 0.0f, 1.0f);
    } else {
        settings.emissionDirection = glm::normalize(settings.emissionDirection);
    }
    return settings;
}

void ParticleSystem::setSettings(const ParticleSystemSettings& settings) {
    settings_ = sanitizeSettings(settings);
    if (particles_.size() > settings_.maxParticles) {
        particles_.resize(settings_.maxParticles);
    }
    if (!settings_.enabled) {
        particles_.clear();
        vertices_.clear();
        playing_ = false;
    } else if (settings_.playOnAwake && particles_.empty() && !playing_) {
        playing_ = true;
    }
}

void ParticleSystem::play() {
    if (!settings_.enabled) {
        return;
    }
    playing_ = true;
}

void ParticleSystem::stop() {
    playing_ = false;
    particles_.clear();
    vertices_.clear();
    emissionAccumulator_ = 0.0f;
    emissionTime_ = 0.0f;
}

void ParticleSystem::restart() {
    if (!settings_.enabled) {
        return;
    }
    particles_.clear();
    vertices_.clear();
    emissionAccumulator_ = 0.0f;
    emissionTime_ = 0.0f;
    playing_ = true;
}

void ParticleSystem::setEnabled(bool enabled) {
    settings_.enabled = enabled;
    if (!enabled) {
        stop();
    } else if (settings_.playOnAwake) {
        play();
    }
}

bool ParticleSystem::playing() const {
    return playing_;
}

float ParticleSystem::randomRange(float minimum, float maximum) {
    std::uniform_real_distribution<float> distribution(minimum, maximum);
    return distribution(randomEngine_);
}

glm::vec3 ParticleSystem::randomUnitVector() {
    const float z = randomRange(-1.0f, 1.0f);
    const float angle = randomRange(0.0f, glm::two_pi<float>());
    const float radius = std::sqrt(std::max(0.0f, 1.0f - z * z));
    return glm::vec3(radius * std::cos(angle), radius * std::sin(angle), z);
}

glm::vec3 ParticleSystem::spawnDirection() {
    const glm::vec3 direction = settings_.emissionDirection;
    const glm::vec3 helper = std::abs(direction.z) < 0.99f
        ? glm::vec3(0.0f, 0.0f, 1.0f)
        : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 tangent = glm::normalize(glm::cross(helper, direction));
    const glm::vec3 bitangent = glm::normalize(glm::cross(direction, tangent));
    const float spread = glm::radians(settings_.spreadDegrees);
    const float radialAngle = spread * std::sqrt(randomRange(0.0f, 1.0f));
    const float azimuth = randomRange(0.0f, glm::two_pi<float>());
    const glm::vec3 radial = tangent * std::cos(azimuth) + bitangent * std::sin(azimuth);
    return glm::normalize(
        direction * std::cos(radialAngle) + radial * std::sin(radialAngle)
    );
}

void ParticleSystem::emitParticle() {
    if (particles_.size() >= settings_.maxParticles) {
        return;
    }

    Particle particle;
    particle.lifetime = settings_.startLifetime * randomRange(
        1.0f - settings_.lifetimeRandomness,
        1.0f + settings_.lifetimeRandomness
    );
    particle.velocity = spawnDirection() * settings_.startSpeed * randomRange(
        1.0f - settings_.speedRandomness,
        1.0f + settings_.speedRandomness
    );
    particle.position = settings_.position;
    if (settings_.shape == ParticleSystemSettings::Shape::Tornado) {
        const float heightFactor = randomRange(0.0f, 1.0f);
        particle.tornadoAngleRadians = randomRange(0.0f, glm::two_pi<float>());
        particle.tornadoAngularSpeed = randomRange(7.0f, 11.0f);
        particle.tornadoHeight = settings_.shapeRadius * 2.4f * heightFactor;
        particle.tornadoVerticalSpeed = settings_.startSpeed * randomRange(0.75f, 1.25f);
        const float radius = settings_.shapeRadius * (0.18f + 0.82f * heightFactor);
        particle.position += glm::vec3(
            std::cos(particle.tornadoAngleRadians) * radius,
            particle.tornadoHeight,
            std::sin(particle.tornadoAngleRadians) * radius
        );
    } else if (settings_.shape == ParticleSystemSettings::Shape::Ring) {
        const float angle = randomRange(0.0f, glm::two_pi<float>());
        const float radius = settings_.shapeRadius * randomRange(0.35f, 1.0f);
        const glm::vec3 radial(std::cos(angle), 0.0f, std::sin(angle));
        particle.position += radial * radius;
        particle.velocity = (
            radial + glm::vec3(0.0f, randomRange(0.45f, 0.85f), 0.0f)
        ) * settings_.startSpeed * randomRange(
            1.0f - settings_.speedRandomness,
            1.0f + settings_.speedRandomness
        );
    } else if (settings_.shape == ParticleSystemSettings::Shape::Sphere) {
        particle.position += randomUnitVector() * settings_.shapeRadius *
            std::cbrt(randomRange(0.0f, 1.0f));
    } else if (settings_.shape == ParticleSystemSettings::Shape::Cone) {
        particle.position += randomUnitVector() * settings_.shapeRadius *
            std::sqrt(randomRange(0.0f, 1.0f));
    }
    particle.rotationSpeedRadians = glm::radians(settings_.rotationSpeedDegrees);
    particle.rotationRadians = settings_.shape == ParticleSystemSettings::Shape::Ring
        ? randomRange(-0.18f, 0.18f)
        : randomRange(0.0f, glm::two_pi<float>());
    particles_.push_back(particle);
}

void ParticleSystem::update(float deltaSeconds) {
    if (!settings_.enabled) {
        particles_.clear();
        vertices_.clear();
        return;
    }

    const float delta = std::clamp(deltaSeconds, 0.0f, 0.1f);
    if (settings_.flipbook.frames) {
        if (playing_) {
            emissionTime_ += delta;
            if (emissionTime_ >= settings_.duration) {
                if (settings_.looping) emissionTime_ = std::fmod(emissionTime_, settings_.duration);
                else playing_ = false;
            }
        }
        return;
    }
    for (std::size_t index = 0; index < particles_.size();) {
        Particle& particle = particles_[index];
        particle.age += delta;
        if (particle.age >= particle.lifetime) {
            particles_[index] = particles_.back();
            particles_.pop_back();
            continue;
        }
        if (settings_.shape == ParticleSystemSettings::Shape::Tornado) {
            particle.tornadoAngleRadians += particle.tornadoAngularSpeed * delta;
            particle.tornadoHeight += particle.tornadoVerticalSpeed * delta;
            const float heightFactor = std::clamp(
                particle.tornadoHeight / std::max(settings_.shapeRadius * 3.0f, 0.0001f),
                0.0f,
                1.0f
            );
            const float radius = settings_.shapeRadius * (0.16f + 0.84f * heightFactor);
            particle.position = settings_.position + glm::vec3(
                std::cos(particle.tornadoAngleRadians) * radius,
                particle.tornadoHeight,
                std::sin(particle.tornadoAngleRadians) * radius
            );
        } else {
            particle.position += particle.velocity * delta;
            particle.velocity += glm::vec3(0.0f, 0.0f, -1.0f) *
                settings_.gravityModifier * delta;
        }
        particle.rotationRadians += particle.rotationSpeedRadians * delta;
        ++index;
    }

    if (!playing_) {
        return;
    }

    emissionTime_ += delta;
    if (!settings_.looping && emissionTime_ >= settings_.duration) {
        playing_ = false;
        return;
    }

    if (settings_.emissionRate <= std::numeric_limits<float>::epsilon()) {
        return;
    }
    emissionAccumulator_ += settings_.emissionRate * delta;
    while (emissionAccumulator_ >= 1.0f && particles_.size() < settings_.maxParticles) {
        emitParticle();
        emissionAccumulator_ -= 1.0f;
    }
    if (particles_.size() >= settings_.maxParticles) {
        emissionAccumulator_ = std::min(emissionAccumulator_, 1.0f);
    }
}

void ParticleSystem::buildVertices(const glm::vec3& cameraRight, const glm::vec3& cameraUp) {
    vertices_.clear();
    vertices_.reserve(particles_.size() * kVerticesPerParticle);

    const glm::vec3 right = glm::length(cameraRight) < 0.001f
        ? glm::vec3(1.0f, 0.0f, 0.0f)
        : glm::normalize(cameraRight);
    const glm::vec3 up = glm::length(cameraUp) < 0.001f
        ? glm::vec3(0.0f, 0.0f, 1.0f)
        : glm::normalize(cameraUp);
    const auto& book = settings_.flipbook;
    if (book.frames) {
        if (!playing_) return;
        const float progress = std::clamp(emissionTime_ / settings_.duration, 0.0f, 1.0f);
        const auto frame = std::min(static_cast<std::uint32_t>(progress * book.frames), book.frames - 1);
        const glm::vec2 grid(book.columns, book.rows);
        const glm::vec2 cell(frame % book.columns, frame / book.columns);
        const float padding = (static_cast<float>(book.gutterPixels) + 0.5f) / book.cellPixels;
        constexpr std::array<glm::vec2, 6> corners{{{0, 1}, {1, 1}, {1, 0}, {1, 0}, {0, 0}, {0, 1}}};
        for (const auto& corner : corners) {
            const glm::vec2 uv = (cell + glm::mix(glm::vec2(padding), glm::vec2(1.0f - padding), corner)) / grid;
            const glm::vec3 offset = right * (corner.x - 0.5f) + up * (0.5f - corner.y);
            vertices_.push_back({settings_.position + offset * book.width, uv, glm::vec4(1.0f)});
        }
        return;
    }

    const glm::vec3 forward = glm::normalize(glm::cross(right, up));
    std::vector<const Particle*> renderParticles;
    renderParticles.reserve(particles_.size());
    for (const Particle& particle : particles_) {
        renderParticles.push_back(&particle);
    }
    if (settings_.blendMode == ParticleSystemSettings::BlendMode::Alpha) {
        // Alpha blending is order dependent. The camera basis lets us sort
        // back-to-front without coupling the simulation to a renderer camera.
        std::sort(
            renderParticles.begin(),
            renderParticles.end(),
            [&forward](const Particle* left, const Particle* rightParticle) {
                return glm::dot(left->position, forward) <
                    glm::dot(rightParticle->position, forward);
            }
        );
    }

    for (const Particle* particlePointer : renderParticles) {
        const Particle& particle = *particlePointer;
        const float normalizedAge = std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);
        const float size = glm::mix(settings_.startSize, settings_.endSize, normalizedAge);
        const glm::vec4 color = glm::mix(
            settings_.startColor,
            settings_.endColor,
            normalizedAge
        );
        const float cosine = std::cos(particle.rotationRadians);
        const float sine = std::sin(particle.rotationRadians);
        const glm::vec3 rotatedRight = (right * cosine - up * sine) * size;
        const glm::vec3 rotatedUp = (right * sine + up * cosine) * size;

        const glm::vec3 bottomLeft = particle.position - rotatedRight - rotatedUp;
        const glm::vec3 bottomRight = particle.position + rotatedRight - rotatedUp;
        const glm::vec3 topRight = particle.position + rotatedRight + rotatedUp;
        const glm::vec3 topLeft = particle.position - rotatedRight + rotatedUp;
        vertices_.insert(vertices_.end(), {
            {bottomLeft, glm::vec2(0.0f, 1.0f), color},
            {bottomRight, glm::vec2(1.0f, 1.0f), color},
            {topRight, glm::vec2(1.0f, 0.0f), color},
            {topRight, glm::vec2(1.0f, 0.0f), color},
            {topLeft, glm::vec2(0.0f, 0.0f), color},
            {bottomLeft, glm::vec2(0.0f, 1.0f), color}
        });
    }
}

const std::vector<ParticleVertex>& ParticleSystem::vertices() const {
    return vertices_;
}

std::size_t ParticleSystem::aliveCount() const {
    return particles_.size();
}

} // namespace VulkCore
