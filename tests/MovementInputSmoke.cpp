#include "vulkcore/Platform/MovementInput.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool near(float value, float expected) { return std::abs(value - expected) < 0.00001f; }
}

int main() {
    try {
        const auto stopped = VulkCore::clampMovementInput(0, 0);
        require(stopped.right == 0 && stopped.forward == 0, "Centered joystick moved the player");
        const auto partial = VulkCore::clampMovementInput(0.3, 0.7);
        require(near(partial.right, 0.3f) && near(partial.forward, 0.7f), "Continuous direction snapped to a key axis");
        const auto outside = VulkCore::clampMovementInput(3, 4);
        require(near(outside.right, 0.6f) && near(outside.forward, 0.8f), "Clamping changed the joystick angle");
        require(near(std::hypot(outside.right, outside.forward), 1), "Joystick input exceeded its movement radius");
        const auto backwardLeft = VulkCore::clampMovementInput(-3, -4);
        require(near(backwardLeft.right, -0.6f) && near(backwardLeft.forward, -0.8f), "Negative directions were reversed");
        for (const double invalid : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
            const auto invalidRight = VulkCore::clampMovementInput(invalid, 1);
            const auto invalidForward = VulkCore::clampMovementInput(1, invalid);
            require(invalidRight.right == 0 && invalidRight.forward == 0 &&
                invalidForward.right == 0 && invalidForward.forward == 0, "Invalid input reached player movement");
        }
        const auto drift = VulkCore::gamepadStickInput(0.12, -0.12);
        require(drift.right == 0 && drift.forward == 0, "Gamepad stick drift passed the dead zone");
        const auto diagonal = VulkCore::gamepadStickInput(0.15, -0.15);
        require(diagonal.right > 0 && near(diagonal.right, -diagonal.forward), "Dead zone was not radial");
        const auto halfTilt = VulkCore::gamepadStickInput(0, 0.6);
        require(halfTilt.right == 0 && near(halfTilt.forward, 0.5f), "Gamepad stick did not scale beyond the dead zone");
        const auto fullTilt = VulkCore::gamepadStickInput(-1, 1);
        require(fullTilt.right < 0 && fullTilt.forward > 0 &&
            near(std::hypot(fullTilt.right, fullTilt.forward), 1), "Full diagonal gamepad tilt exceeded unit speed");
        std::cout << "Analog movement direction, dead zone and input validation checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
