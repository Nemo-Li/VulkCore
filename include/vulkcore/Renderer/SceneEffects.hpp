#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace VulkCore {

struct HdrSettings {
    bool enabled = false;
    int bloomLevels = 0;
    float bloomThreshold = 0.0f;
    float bloomSmoothing = 0.5f;
    float bloomIntensity = 1.0f;
    float bloomRadius = 0.85f;
    float exposure = 1.0f; // Applied by tone-mapped surface shaders.
    bool smaa = false;
    glm::vec3 clearColor{0.0f};
};

struct DistanceFogSettings {
    bool enabled = false;
    float startDistance = 0.0f;
    float halfDistance = 1.0f;
    float maxOpacity = 0.0f;
    glm::vec3 horizonColor{0.0f};
    glm::vec3 upperColor{0.0f};
    float floatingMistOpacity = 0.0f;
};

struct AuthoredView {
    bool enabled = false;
    glm::vec3 target{0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float fov = 45.0f;
    float minimumAspect = 0.0f;
    bool orbitEnabled = false;
    glm::vec2 orbitPitchLimits{0.0f, 82.0f};
};

struct TransitionSettings {
    bool enabled = false;
    bool autoplay = false;
    float delay = 0.0f;
    float duration = 1.0f;
    float dwell = 1.2f;
    float activeFov = 0.0f;
    float distanceOffset = 0.0f;
};

struct TransitionPlayer {
    float progress = 0.0f;
    float flowTime = 0.0f;
    bool loop = false;
    bool flow = true;
    bool moving = false;
    bool active = false;
    float from = 0.0f;
    float to = 0.0f;
    float elapsed = 0.0f;
    float remainingDwell = 0.0f;

    void play(bool forward, const TransitionSettings& settings) {
        from = progress;
        to = forward ? 1.0f : 0.0f;
        elapsed = forward ? -settings.delay : 0.0f;
        remainingDwell = settings.dwell;
        active = forward;
        moving = std::abs(from - to) >= 0.000001f;
    }

    void seek(float value) {
        progress = std::clamp(value, 0.0f, 1.0f);
        active = progress > 0.0f;
        moving = false;
        loop = false;
    }

    void update(float seconds, const TransitionSettings& settings) {
        if (flow) flowTime += seconds;
        if (moving) {
            elapsed += seconds;
            const float t = std::clamp(elapsed / settings.duration, 0.0f, 1.0f);
            const float eased = t < 0.5f
                ? 4.0f * t * t * t
                : 1.0f - 0.5f * (2.0f - 2.0f * t) * (2.0f - 2.0f * t) * (2.0f - 2.0f * t);
            progress = std::lerp(from, to, eased);
            moving = t < 1.0f;
        } else if (loop) {
            remainingDwell -= seconds;
            if (remainingDwell <= 0.0f) play(progress < 0.5f, settings);
        }
    }
};

} // namespace VulkCore
