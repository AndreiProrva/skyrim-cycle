#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cycle {
constexpr int defaultGear=2;
constexpr int validGear(int gear) { return std::clamp(gear,1,8); }
constexpr double gearSpeed(int gear) { return 10.0+4.0*validGear(gear); }
constexpr double gearRatio(int gear) { return gearSpeed(gear)/18.0; }
// Physical keyboard scan codes; no dependency on the selected keyboard layout.
constexpr int gearKey(std::uint32_t code) {
    return code>=0x02 && code<=0x09 ? int(code)-1:0;
}
struct RideTelemetry { bool visible{}; int gear{defaultGear}; double rpm{},kmh{}; bool lampOn{}; };
class RideMeter {
    double since{},turn{},distance{};
    bool initialized{};
public:
    double rpm{},kmh{};
    void reset() { *this={}; }
    void sample(double radians,double metres,double dt,double now,bool paused) {
        if(paused || !std::isfinite(radians) || !std::isfinite(metres) || dt<=0 || dt>.25) {reset();return;}
        if(!initialized) { initialized=true;since=now-dt; }
        turn+=std::abs(radians);distance+=std::abs(metres);
        if(now-since>=.2) {
            rpm=turn/(2*3.14159265358979323846)*60/(now-since);
            kmh=distance*3.6/(now-since);
            since=now;turn=distance=0;
        }
    }
};
}
