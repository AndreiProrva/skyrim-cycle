#pragma once
#include <string>
#include <string_view>
namespace cycle {
inline bool isCyclingRiderClip(std::string_view path) {
    std::string name(path);
    for(char& c:name) {if(c>='A' && c<='Z')c+=32;if(c=='\\')c='/';}
    if(name.find("horse_rider/")==std::string::npos)return false;
    auto slash=name.find_last_of('/');auto base=std::string_view(name).substr(slash+1);
    return base.starts_with("idle") || base.starts_with("walkforward") ||
        base.starts_with("trotforward") || base.starts_with("runforward") ||
        base.starts_with("turn") || base=="sprintforward.hkx";
}
}
