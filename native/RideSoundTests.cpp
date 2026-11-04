#include "RideSoundMixer.h"
#include "AudioPolicy.h"
#include "WheelStream.h"
#include <cassert>
#include <iostream>
#include <fstream>
using Mixer=cycle::RideSoundMixer;
cycle::RideSoundClips fake() {
    return {std::vector<float>(300,.15f),std::vector<float>(5000,.2f)};
}
void render(Mixer& mixer,int n) {std::vector<std::int16_t> out(n);mixer.render(out.data(),out.size(),.2);}
int main(int argc,char** argv) {
    cycle::AudioCadenceWindow settlingRPM;cycle::WheelMotionGate settlingGate;
    Mixer settling(fake(),60,1000);
    for(int i=0;i<180;++i) {
        double observed=settlingRPM.sample(i==0?.024:0,.016); // actual mounted-save physics nudge
        settling.control(settlingGate.sample(observed,true,i*.016),true,observed);render(settling,16);
    }
    assert(settling.starts==0&&settling.stops==0&&settling.silent());
    Mixer idle(fake(),60,1000);
    for(int i=0;i<10000;++i){idle.control(false,true,0);render(idle,1);}
    assert(idle.silent()&&idle.starts==0&&idle.stops==0); // initial standing MUST NOT play end
    idle.control(true,true,60);render(idle,100);assert(idle.stage()==Mixer::Stage::start&&idle.starts==1);
    idle.control(false,true,0);assert(idle.stage()==Mixer::Stage::idle&&idle.stops==1);render(idle,25);assert(idle.silent());
    render(idle,500);
    for(int i=0;i<10000;++i){idle.control(false,true,0);render(idle,1);}
    assert(idle.silent()&&idle.stops==1); // end once only, then silence
    idle.control(true,true,60);render(idle,350);assert(idle.stage()==Mixer::Stage::loop);
    auto ends=idle.stops;idle.control(false,false,0);render(idle,500);assert(idle.silent()&&idle.stops==ends); // pause/dismount cancels
    idle.control(true,true,60);render(idle,350);idle.control(false,true,0);render(idle,30);
    idle.control(true,true,90);render(idle,50);assert(idle.stage()==Mixer::Stage::start); // resume during outro
    Mixer slow(fake(),60,1000),fast(fake(),60,1000);
    slow.control(true,true,30);fast.control(true,true,120);render(slow,1200);render(fast,1200);
    assert(std::abs(slow.playbackRate()-.925)<.002&&std::abs(fast.playbackRate()-1.15)<.002);
    cycle::AudioCadenceWindow rpm;
    double cadence=0;
    for(int i=0;i<120;++i)cadence=rpm.sample((6.283185307179586*90/60)/120,1./120);
    assert(std::abs(cadence-90)<.001);
    rpm.reset();double mean=0;
    for(int i=0;i<240;++i) {
        cadence=rpm.sample(i%2?0:2*(6.283185307179586*90/60)/120,1./120);
        if(i>=120)mean+=cadence;
    }
    assert(std::abs(mean/120-90)<.01); // cadence average survives alternating frame positions
    for(int i=0;i<50;++i)cadence=rpm.sample(0,1./120);
    assert(cadence==0);rpm.reset();assert(rpm.sample(1,1)==0);
    std::cout<<"PASS sound FSM: idle silence, start-loop, no outro, 20ms stop fade, zero never retriggers, pause/dismount cancel, restart and weak RPM tempo\n";
    if(argc<2)return 0;
    std::filesystem::path directory=argv[1];
    cycle::RideSoundClips actual{cycle::loadRideClip(directory/"wheel_start.wav"),cycle::loadRideClip(directory/"wheels.wav")};
    assert(actual.start.size()>=44100*3&&actual.loop.size()>44100);
    // Build a reviewable offline preview: idle -> intro -> 60 RPM -> 120 RPM -> stop.
    Mixer preview(actual,60);std::vector<std::int16_t> data;
    auto append=[&](double seconds){auto old=data.size();data.resize(old+std::size_t(seconds*44100));preview.render(data.data()+old,data.size()-old,.2);};
    preview.control(false,true,0);append(.5);
    preview.control(true,true,60);append(actual.start.size()/44100.+3);
    preview.control(true,true,120);append(3);
    preview.control(false,true,0);append(1);
    auto out=directory/"ride_preview.wav";
    std::ofstream file(out,std::ios::binary);
    auto u16=[&](std::uint16_t n){file.write(reinterpret_cast<const char*>(&n),2);};
    auto u32=[&](std::uint32_t n){file.write(reinterpret_cast<const char*>(&n),4);};
    file.write("RIFF",4);u32(36+data.size()*2);file.write("WAVEfmt ",8);u32(16);u16(1);u16(1);u32(44100);u32(88200);u16(2);u16(16);file.write("data",4);u32(data.size()*2);file.write(reinterpret_cast<const char*>(data.data()),data.size()*2);file.close();
    // Exercise the ACTUAL Windows streaming buffer lifecycle at zero amplitude.
    for(auto* clip:{&actual.start,&actual.loop})std::fill(clip->begin(),clip->end(),0.f);
    Mixer silent(std::move(actual),60);silent.control(true,true,60);cycle::WheelStream stream;
    auto result=stream.open();
    if(result!=MMSYSERR_NOERROR){std::cout<<"SKIP streaming device: "<<result<<"\n";return 0;}
    assert(stream.pump(silent,0)==MMSYSERR_NOERROR);assert(stream.pending());
    Sleep(25);assert(stream.pump(silent,0)==MMSYSERR_NOERROR);
    silent.control(false,false,0);assert(stream.stop());assert(!stream.active());
    assert(stream.open()==MMSYSERR_NOERROR);assert(stream.stop());
    std::cout<<"PASS real WAV assets, offline preview, silent Windows triple-buffer write/reuse/reset/close/reopen\n";
}
