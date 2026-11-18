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

} // namespace VulkCore
