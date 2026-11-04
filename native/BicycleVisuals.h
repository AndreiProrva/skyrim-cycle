#pragma once
#include <algorithm>
#include <cmath>
#include <array>
#include <cstdint>
namespace cycle {
constexpr float headlampFovRadians=52.f*3.14159265358979323846f/180.f;
class ExitSeatAnchor {
    std::uint32_t bike{};float height{};
public:
    void reset(){bike=0;height=0;}
    void seated(std::uint32_t id,float z){if(id&&std::isfinite(z)){bike=id;height=z;}}
    float exitHeight(std::uint32_t id,float current)const{return id&&id==bike?height:current;}
};
inline bool offsetVisualRoot(float* translation,const std::array<float,3>& delta,float scale) {
    if(!translation||!std::isfinite(scale)||scale<=0)return false;
    for(float d:delta)if(!std::isfinite(d))return false;
    for(int i=0;i<3;++i)translation[i]+=delta[i]/scale;
    return true;
}
inline std::array<std::array<float,3>,3> headlampRotation() {
    const float pitch=14.f*3.14159265358979323846f/180.f;
    const float c=std::cos(pitch),s=std::sin(pitch);
    return {{{0,0,1},{c,s,0},{-s,c,0}}};
}
inline float mountLowering(float pelvisHeight,float saddleHeight) {
    if(!std::isfinite(pelvisHeight)||!std::isfinite(saddleHeight))return 0;
    const float excess=pelvisHeight-saddleHeight;
    // Reject implausible input instead of repeatedly subtracting a maximum
    // offset on repeated scene updates. Compare both heights in WORLD space,
    // after paired animation mapping, rather than using local pose tracks.
    return excess>0&&excess<=210.f?excess:0.f;
}
}
