#pragma once
#include "WheelWave.h"
#include "RideSoundMixer.h"
#include <array>

namespace cycle {
inline std::vector<float> loadRideClip(const std::filesystem::path& path) {
    WheelWave file;
    if(!file.load(path,1)||file.sampleRate()!=44100||file.channels()!=1)return {};
    std::vector<float> samples(file.samples().size()/2);
    for(std::size_t i=0;i<samples.size();++i){std::int16_t s;std::memcpy(&s,file.samples().data()+i*2,2);samples[i]=s/32768.f;}
    return samples;
}
// Three short reusable buffers, one private Windows output, software resampling.
// No playback-rate device extension or Skyrim mixer function is required.
class WheelStream {
    static constexpr std::size_t frames=882; // 20 ms, at most 60 ms queued
    HWAVEOUT handle{};
    std::array<WAVEHDR,3> headers{};
    std::array<std::array<std::int16_t,frames>,3> buffers{};
    std::array<bool,3> prepared{},queued{};
public:
    WheelStream()=default;
    WheelStream(const WheelStream&)=delete;
    ~WheelStream(){stop();}
    MMRESULT open() {
        if(handle&&std::all_of(prepared.begin(),prepared.end(),[](bool p){return p;}))return MMSYSERR_NOERROR;
        if(handle&&!stop())return MMSYSERR_ALLOCATED;
        WAVEFORMATEX format{WAVE_FORMAT_PCM,1,44100,88200,2,16,0};
        auto result=waveOutOpen(&handle,WAVE_MAPPER,&format,0,0,CALLBACK_NULL);
        if(result!=MMSYSERR_NOERROR){handle=nullptr;return result;}
        for(std::size_t i=0;i<3;++i) {
            headers[i]={};headers[i].lpData=reinterpret_cast<LPSTR>(buffers[i].data());headers[i].dwBufferLength=frames*2;
            result=waveOutPrepareHeader(handle,&headers[i],sizeof(WAVEHDR));
            if(result!=MMSYSERR_NOERROR){stop();return result;}prepared[i]=true;
        }
        return MMSYSERR_NOERROR;
    }
    MMRESULT pump(RideSoundMixer& mixer,double gain) {
        for(std::size_t i=0;i<3;++i) {
            if(queued[i]&&!(headers[i].dwFlags&WHDR_DONE))continue;
            queued[i]=false;if(mixer.silent())continue;
            mixer.render(buffers[i].data(),frames,gain);
            auto result=waveOutWrite(handle,&headers[i],sizeof(WAVEHDR));
            if(result!=MMSYSERR_NOERROR)return result;
            queued[i]=true;
        }
        return MMSYSERR_NOERROR;
    }
    bool pending()const {
        for(std::size_t i=0;i<3;++i)if(queued[i]&&!(headers[i].dwFlags&WHDR_DONE))return true;
        return false;
    }
    bool stop() {
        if(!handle)return true;
        if(waveOutReset(handle)!=MMSYSERR_NOERROR)return false;
        for(std::size_t i=0;i<3;++i)if(prepared[i]) {
            if(waveOutUnprepareHeader(handle,&headers[i],sizeof(WAVEHDR))!=MMSYSERR_NOERROR)return false;
            prepared[i]=queued[i]=false;
        }
        if(waveOutClose(handle)!=MMSYSERR_NOERROR)return false;
        handle=nullptr;return true;
    }
    bool active()const{return handle!=nullptr;}
};
}
