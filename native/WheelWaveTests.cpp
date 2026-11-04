#include "WheelWave.h"
#include <cassert>
#include <iostream>
int main(int argc,char** argv) {
    assert(argc==2);
    cycle::WheelWave silent;
    assert(silent.load(argv[1],0));
    assert(!silent.samples().empty());
    assert(std::all_of(silent.samples().begin(),silent.samples().end(),[](char c){return c==0;}));
    cycle::WheelWave invalid;
    assert(!invalid.load(argv[1],-1));assert(!invalid.load(argv[1],2));assert(!invalid.load("missing-wheel.wav",.2));
    auto bad=std::filesystem::current_path()/"WheelWave-invalid-test.wav";
    {std::ofstream f(bad,std::ios::binary);f.write("RIFFbad",7);}
    assert(!invalid.load(bad,.2));std::filesystem::remove(bad);
    WAVEFORMATEX format{WAVE_FORMAT_PCM,1,44100,88200,2,16,0};
    auto query=waveOutOpen(nullptr,WAVE_MAPPER,&format,0,0,WAVE_FORMAT_QUERY);
    if(query!=MMSYSERR_NOERROR) {
        std::cout<<"PASS WAV validation/gain; SKIP Windows device unavailable, code "<<query<<"\n";return 0;
    }
    auto result=silent.start();
    if(result!=MMSYSERR_NOERROR) {
        std::cout<<"PASS WAV validation/gain; SKIP Windows device open, code "<<result<<"\n";return 0;
    }
    auto handle=silent.nativeHandle();assert(handle&&silent.active());
    assert(silent.start()==MMSYSERR_NOERROR&&silent.nativeHandle()==handle);
    Sleep(25); // ZERO-amplitude samples only: exercise the driver without audible output.
    assert(silent.stop());assert(!silent.active()&&!silent.nativeHandle());assert(silent.stop());
    assert(silent.start()==MMSYSERR_NOERROR);assert(silent.stop());
    std::cout<<"PASS WAV validation/gain; silent Windows open/loop/idempotent start/reset/unprepare/close/restart\n";
}
