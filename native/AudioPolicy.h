#pragma once
#include <string>
#include <string_view>
#include <optional>
#include <cstdint>
namespace cycle {
class WheelMotionGate {
    double first{-1},last{-1};unsigned samples{};bool running{};
public:
    void reset(){first=last=-1;samples=0;running=false;}
    bool sample(double speed,bool eligible,double now) {
        if(!eligible||!std::isfinite(speed)||!std::isfinite(now)){reset();return false;}
        if(last>=0&&now<last){reset();return false;}
        if(speed>.8) {
            if(last<0||now-last>.25){first=now;samples=0;}
            last=now;++samples;
            if(samples>=3&&now-first>=.15)running=true;
        }
        // Grace can KEEP an existing loop alive; it must never START one at zero.
        if(last<0||now-last>=.25)running=false;
        return running;
    }
};
enum class AudioEventKind {play,foot};
inline std::optional<AudioEventKind> audioEventKind(std::string_view event) {
    std::string key(event);for(auto& c:key)if(c>='A'&&c<='Z')c+=32;
    if(key=="soundplay"||key=="npcsoundplay"||key.starts_with("soundplay.")||key.starts_with("npcsoundplay."))return AudioEventKind::play;
    if(key=="footfront"||key=="footback"||key=="footleft"||key=="footright" ||
       key=="footfrontleft"||key=="footfrontright"||key=="footbackleft"||key=="footbackright")return AudioEventKind::foot;
    return {};
}
inline bool muteBikeAudio(bool bike,bool player,bool ridingBike,AudioEventKind kind,std::string_view payload) {
    if(bike)return true;
    if(!player||!ridingBike||kind!=AudioEventKind::play)return false;
    std::string text(payload);for(auto& c:text)if(c>='A'&&c<='Z')c+=32;
    return text.find("npchorse")!=std::string::npos;
}
struct ActorSoundScope {
    std::uint32_t sourceId{},playerMountId{};
    bool sourceIsPlayer{},playerMountIsOurBike{};
    std::uint32_t rememberedPlayerBikeId{};
};
inline bool suppressSelectedBikeAudio(ActorSoundScope scope,std::string_view tag,std::string_view payload) {
    auto kind=audioEventKind(tag);if(!kind)return false;
    bool selected=(scope.playerMountIsOurBike&&scope.playerMountId!=0&&scope.sourceId==scope.playerMountId)||
        (scope.rememberedPlayerBikeId!=0&&scope.sourceId==scope.rememberedPlayerBikeId);
    std::string annotation=std::string(tag)+"."+std::string(payload);
    return muteBikeAudio(selected,scope.sourceIsPlayer,scope.playerMountIsOurBike,*kind,annotation);
}
class WheelLoopGate {
    bool owned{};double retryAt{};
public:
    enum Action {none,start,stop};
    Action sample(bool wanted,double now) {
        if(!wanted && owned){owned=false;return stop;}
        if(wanted && !owned && now>=retryAt){owned=true;return start;}
        return none;
    }
    void failed(double now){owned=false;retryAt=now+1;}
};
}
