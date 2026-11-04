#pragma once
#include <cmath>

namespace cycle {
constexpr double pi = 3.14159265358979323846;
constexpr double radiusUnits = 0.385 * 70.0;
struct Point { double x{}, y{}, z{}; };
class Motion {
public:
    double angle{};
    double crankAngle{};
    double wheelDelta{},crankDelta{};
    bool initialized{};
    Point previous{};
    double previousTime{};
    void reset() { *this = {}; }
    double sample(Point position, double heading, double now, bool paused, double ratio=1.0) {
        wheelDelta=crankDelta=0;
        if (!initialized) { initialized=true; previous=position; previousTime=now; return angle; }
        const auto dt=now-previousTime;
        const auto dx=position.x-previous.x, dy=position.y-previous.y;
        previous=position; previousTime=now;
        const auto distance=std::hypot(dx,dy);
        if (paused || dt<=0 || dt>.25 || !std::isfinite(distance) ||
            distance>2000.0*dt+21.0 || distance<.007) return angle;
        const auto forward=dx*std::sin(heading)+dy*std::cos(heading);
        const double delta=-(forward<0 ? -distance:distance)/radiusUnits;
        wheelDelta=delta;
        crankDelta=delta/(std::isfinite(ratio)&&ratio>0?ratio:1.0);
        angle=std::remainder(angle+delta,2*pi);
        crankAngle=std::remainder(crankAngle+crankDelta,2*pi);
        return angle;
    }
    double fraction() const {
        auto value=std::fmod(-crankAngle/(2*pi),1.0);
        return value<0 ? value+1:value;
    }
};
}
