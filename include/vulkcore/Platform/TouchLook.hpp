#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>

namespace VulkCore {

enum class TouchPhase { Down, Up, Move, Cancel };

class TouchLook {
public:
    bool active() const { return fingerId_ >= 0; }
    double x() const { return x_; }
    double y() const { return y_; }
    void reset() { fingerId_ = -1; }

    // Accept the XComponent event shape without depending on SDK headers, so
    // native multi-touch event sequences can also be replayed by desktop tests.
    template<class Event>
    void handle(TouchPhase phase, const Event& event, double surfaceWidth) {
        if (phase == TouchPhase::Cancel) {
            reset();
            return;
        }
        if (phase == TouchPhase::Up && event.id == fingerId_) {
            reset();
            return;
        }
        if (phase == TouchPhase::Down && !active() && surfaceWidth > 0 &&
            event.id >= 0 && event.x > surfaceWidth * 0.5 &&
            std::isfinite(event.x) && std::isfinite(event.y)) {
            fingerId_ = event.id;
        }
        if (!active()) return;

        if (event.id == fingerId_) {
            setPosition(event.x, event.y);
            return;
        }
        const auto count = std::min<std::size_t>(event.numPoints, std::size(event.touchPoints));
        for (std::size_t index = 0; index < count; ++index) {
            const auto& point = event.touchPoints[index];
            if (point.id == fingerId_) {
                // XComponent sets isPressed only on DOWN, including in its
                // touchPoints array. MOVE positions must not depend on it.
                setPosition(point.x, point.y);
                break;
            }
        }
    }

private:
    void setPosition(double x, double y) {
        if (std::isfinite(x) && std::isfinite(y)) {
            x_ = x;
            y_ = y;
        }
    }

    int fingerId_ = -1;
    double x_ = 0;
    double y_ = 0;
};

} // namespace VulkCore
