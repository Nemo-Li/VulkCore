#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <random>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

namespace VulkCore {

// Particle vertices are already expanded to camera-facing quads on the CPU.
// Texture sampling is configured per Particle System GameObject; the vertex
// stays compact because all sprite appearance data is shared by its batch.
struct ParticleVertex {
    glm::vec3 position{0.0f};
    glm::vec2 texCoord{0.0f};
    glm::vec4 color{1.0f};

    static VkVertexInputBindingDescription bindingDescription();
    static std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions();
};

struct ParticleFlipbook {
    std::uint32_t columns = 0;
    std::uint32_t rows = 0;
    std::uint32_t frames = 0;
    std::uint32_t cellPixels = 0;
    std::uint32_t gutterPixels = 0;
    float width = 1.0f;

    bool valid() const;
};

struct ParticleSystemSettings {
    enum class Shape : std::uint8_t {
        Point = 0,
        Sphere,
        Cone,
        Tornado,
        Ring,
    };

    enum class BlendMode : std::uint8_t {
        Alpha = 0,
        Additive,
    };

    bool enabled = true;
    bool playOnAwake = true;
    bool looping = true;
    std::uint32_t maxParticles = 512;
    float duration = 5.0f;
    float emissionRate = 28.0f;
    float startLifetime = 2.4f;
    float lifetimeRandomness = 0.25f;
    float startSpeed = 0.62f;
    float speedRandomness = 0.22f;
    float startSize = 0.085f;
    float endSize = 0.012f;
    float rotationSpeedDegrees = 35.0f;
    float gravityModifier = 0.35f;
    glm::vec3 position{0.0f, 0.0f, 0.35f};
    glm::vec3 emissionDirection{0.0f, 0.0f, 1.0f};
    float spreadDegrees = 24.0f;
    float shapeRadius = 0.08f;
    Shape shape = Shape::Cone;
    BlendMode blendMode = BlendMode::Additive;
    std::filesystem::path texturePath;
    ParticleFlipbook flipbook;
    glm::vec4 startColor{1.0f, 0.38f, 0.06f, 0.95f};
    glm::vec4 endColor{1.0f, 0.04f, 0.005f, 0.0f};
};

class ParticleSystem {
public:
    static constexpr std::uint32_t kMaximumSupportedParticles = 2048;
    static constexpr std::size_t kVerticesPerParticle = 6;

    ParticleSystem();

    const ParticleSystemSettings& settings() const;
    ParticleSystemSettings& settings();
    void setSettings(const ParticleSystemSettings& settings);

    void play();
    void stop();
    void restart();
    void setEnabled(bool enabled);
    bool playing() const;

    void update(float deltaSeconds);
    void buildVertices(const glm::vec3& cameraRight, const glm::vec3& cameraUp);

    const std::vector<ParticleVertex>& vertices() const;
    std::size_t aliveCount() const;

    static ParticleSystemSettings sanitizeSettings(ParticleSystemSettings settings);

private:
    struct Particle {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        float age = 0.0f;
        float lifetime = 1.0f;
        float size = 0.0f;
        float rotationRadians = 0.0f;
        float rotationSpeedRadians = 0.0f;
        float tornadoAngleRadians = 0.0f;
        float tornadoAngularSpeed = 0.0f;
        float tornadoHeight = 0.0f;
        float tornadoVerticalSpeed = 0.0f;
    };

    float randomRange(float minimum, float maximum);
    glm::vec3 randomUnitVector();
    glm::vec3 spawnDirection();
    void emitParticle();

    ParticleSystemSettings settings_{};
    std::vector<Particle> particles_;
    std::vector<ParticleVertex> vertices_;
    std::mt19937 randomEngine_{0xC0FFEEu};
    float emissionAccumulator_ = 0.0f;
    float emissionTime_ = 0.0f;
    bool playing_ = true;
};

} // namespace VulkCore
