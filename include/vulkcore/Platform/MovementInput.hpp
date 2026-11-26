#pragma once

#include <algorithm>
#include <cmath>

namespace VulkCore {

struct MovementInput {
    float right = 0;
    float forward = 0;
};

inline MovementInput clampMovementInput(double right, double forward) {
    if (!std::isfinite(right) || !std::isfinite(forward)) return {};
    const double length = std::hypot(right, forward);
    if (!std::isfinite(length)) return {};
    const double divisor = std::max(1.0, length);
    return {static_cast<float>(right / divisor), static_cast<float>(forward / divisor)};
}

inline MovementInput gamepadStickInput(double right, double forward) {
    constexpr float deadZone = 0.20f;
    const MovementInput input = clampMovementInput(right, forward);
    const float length = std::hypot(input.right, input.forward);
    if (length <= deadZone) return {};
    const float strength = (std::min(1.0f, length) - deadZone) / (1.0f - deadZone);
    return {input.right / length * strength, input.forward / length * strength};
}

} // namespace VulkCore
