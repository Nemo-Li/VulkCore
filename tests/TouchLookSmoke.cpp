#include "vulkcore/Platform/TouchLook.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
// Match the native event fields, including XComponent's false isPressed on MOVE.
struct Point {
    int id = -1;
    float x = 0, y = 0;
    bool isPressed = false;
};
struct Event {
    int id = -1;
    float x = 0, y = 0;
    std::array<Point, 10> touchPoints{};
    std::uint32_t numPoints = 0;
};
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
Event event(int id, float x, float y) {
    return {id, x, y, {{{id, x, y, false}}}, 1};
}
}

int main() {
    using VulkCore::TouchPhase;
    try {
        VulkCore::TouchLook look;
        look.handle(TouchPhase::Down, event(1, 200, 300), 1920);
        require(!look.active(), "Left-side touch started camera rotation");
        look.handle(TouchPhase::Down, event(1, 1400, 300), 0);
        require(!look.active(), "Empty viewport started camera rotation");

        look.handle(TouchPhase::Down, event(4, 1400, 300), 1920);
        require(look.active() && look.x() == 1400 && look.y() == 300, "Right-side touch was not captured");
        look.handle(TouchPhase::Move, event(4, 1500, 350), 1920);
        require(look.x() == 1500 && look.y() == 350,
            "MOVE with isPressed=false did not update the camera cursor");

        // Another finger can move or lift while the look finger keeps moving.
        auto multi = event(8, 100, 200);
        multi.numPoints = 2;
        multi.touchPoints[1] = {4, 1600, 400, false};
        look.handle(TouchPhase::Move, multi, 1920);
        require(look.x() == 1600 && look.y() == 400,
            "Multi-touch MOVE lost the look finger when isPressed=false");
        look.handle(TouchPhase::Up, multi, 1920);
        require(look.active(), "Lifting a different finger ended camera rotation");
        look.handle(TouchPhase::Down, event(9, 1800, 600), 1920);
        require(look.x() == 1600 && look.y() == 400, "A second right-side finger stole the camera");
        look.handle(TouchPhase::Move, event(9, 1850, 650), 1920);
        require(look.x() == 1600 && look.y() == 400, "Unrelated finger moved the camera");

        auto changedOnly = event(4, 1700, 500);
        changedOnly.numPoints = 0;
        look.handle(TouchPhase::Move, changedOnly, 1920);
        require(look.x() == 1700 && look.y() == 500, "Changed touch coordinates were ignored without a point array");
        look.handle(TouchPhase::Move, event(4, 800, 450), 1920);
        require(look.active() && look.x() == 800, "Dragging across the screen center lost the look finger");
        look.handle(TouchPhase::Up, event(4, 800, 450), 1920);
        require(!look.active(), "Lifting the look finger left camera rotation active");
        look.handle(TouchPhase::Move, event(4, 900, 550), 1920);
        require(!look.active() && look.x() == 800, "Orphan MOVE restarted or moved camera rotation");

        look.handle(TouchPhase::Down, event(2, 1300, 400), 1920);
        require(look.active() && look.x() == 1300, "A new gesture could not start after release");
        auto invalid = event(2, std::numeric_limits<float>::quiet_NaN(), 500);
        look.handle(TouchPhase::Move, invalid, 1920);
        require(look.x() == 1300 && look.y() == 400, "Invalid coordinates corrupted camera input");
        auto oversized = event(7, 100, 100);
        oversized.numPoints = 100;
        look.handle(TouchPhase::Move, oversized, 1920);
        require(look.x() == 1300, "Unrelated multi-touch event changed the camera cursor");
        look.handle(TouchPhase::Cancel, event(7, 100, 100), 1920);
        require(!look.active(), "CANCEL left the look finger active");
        look.handle(TouchPhase::Down, event(3, 1500, 500), 1920);
        look.reset();
        require(!look.active(), "Background/surface reset retained camera input");

        std::cout << "Touch look MOVE, multi-touch and lifecycle checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
