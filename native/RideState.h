#pragma once
#include <cmath>
#include <optional>
namespace cycle {
class RideGate {
    double since{-1};
    bool visualReady{};
public:
    void reset(){since=-1;visualReady=false;}
    bool poseReady()const{return visualReady;}
    bool sample(bool relation,bool onMount,bool seated,bool transition,double now) {
        if(!relation || !onMount || !seated || transition){reset();return false;}
        visualReady=true; // visible idle pose may bridge the control-settle delay
        if(since<0)since=now;
        return now-since>=.15;
    }
};
// Both ankles share a heel offset and any saddle translation. Their difference
// cancels those offsets. L is below the crank at phase zero in the authored rig.
inline std::optional<float> crankFromAnkles(float ly,float lz,float ry,float rz) {
    float y=ly-ry,z=rz-lz,r=std::hypot(y,z);
    if(!std::isfinite(r)||r<12 || r>36)return {};
    return std::atan2(y,z);
}
}
