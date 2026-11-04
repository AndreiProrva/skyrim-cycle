#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cycle {
class AudioCadenceWindow {
    double elapsed{},angle{},value{};
public:
    void reset(){elapsed=angle=value=0;}
    double sample(double delta,double dt) {
        if(!std::isfinite(delta)||!std::isfinite(dt)||dt<=0||dt>.25){reset();return 0;}
        elapsed+=dt;angle+=std::abs(delta);
        if(elapsed>=.12){value=60*angle/(6.283185307179586*elapsed);elapsed=angle=0;}
        return value;
    }
};
struct RideSoundClips {std::vector<float> start,loop;};
class RideSoundMixer {
public:
    enum class Stage {idle,start,loop};
private:
    struct Voice {Stage stage{Stage::idle};double position{};};
    RideSoundClips clips;
    Voice voice,previous;
    bool wasMoving{};
    int fadeLeft{},fadeFrames;
    double pitch{1},targetPitch{1},reference,alpha;
    float lastValue{},correction{};
    const std::vector<float>& data(Stage stage)const {
        if(stage==Stage::start)return clips.start;
        return clips.loop;
    }
    float read(Voice& v) {
        if(v.stage==Stage::idle)return 0;
        auto& samples=data(v.stage);
        if(samples.empty()||v.position>=samples.size())return 0;
        auto i=static_cast<std::size_t>(v.position);
        auto next=i+1<samples.size()?i+1:(v.stage==Stage::loop?0:i);
        auto fraction=static_cast<float>(v.position-i);
        float out=samples[i]+fraction*(samples[next]-samples[i]);
        v.position+=v.stage==Stage::loop?pitch:1;
        if(v.stage==Stage::loop&&v.position>=samples.size())v.position-=samples.size();
        return out;
    }
    void transition(Stage next) {
        previous=voice;auto preview=previous;
        correction=lastValue-read(preview);
        voice={next,0};fadeLeft=fadeFrames;
    }
public:
    unsigned starts{},stops{},loopEntries{};
    explicit RideSoundMixer(RideSoundClips assets,double referenceRPM=60,int rate=44100):clips(std::move(assets)),
        fadeFrames(std::max(1,rate/50)),reference(std::max(referenceRPM,1.)),alpha(1-std::exp(-1./(.18*rate))){}
    void cancel(){voice={};previous={};fadeLeft=0;wasMoving=false;pitch=targetPitch=1;lastValue=correction=0;}
    void control(bool moving,bool eligible,double rpm) {
        if(!eligible){cancel();return;} // pause, loading and dismount NEVER play the end recording
        if(moving&&!wasMoving){transition(Stage::start);++starts;}
        else if(!moving&&wasMoving){transition(Stage::idle);++stops;}
        wasMoving=moving;
        if(moving&&std::isfinite(rpm))targetPitch=std::clamp(1+.15*(std::abs(rpm)/reference-1),.92,1.20);
    }
    void render(std::int16_t* output,std::size_t count,double gain) {
        gain=std::clamp(gain,0.,1.);
        for(std::size_t i=0;i<count;++i) {
            if(voice.stage==Stage::start&&voice.position>=std::max<double>(0,clips.start.size()-std::min<std::size_t>(fadeFrames,clips.start.size()))) {
                transition(Stage::loop);++loopEntries;
            }
            pitch+=(targetPitch-pitch)*alpha;
            float value=read(voice);
            if(fadeLeft>0) {
                float amount=1.f-static_cast<float>(fadeLeft)/fadeFrames;
                value=(read(previous)+correction)*(1-amount)+value*amount;
                if(--fadeLeft==0)previous={};
            }
            lastValue=value;
            output[i]=static_cast<std::int16_t>(std::lround(std::clamp(value*gain,-1.,.999969)*32768));
        }
    }
    bool silent()const{return voice.stage==Stage::idle&&fadeLeft==0;}
    Stage stage()const{return voice.stage;}
    double loopPosition()const{return voice.position;}
    double playbackRate()const{return pitch;}
};
}
