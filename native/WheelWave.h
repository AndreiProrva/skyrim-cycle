#pragma once
#include <Windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace cycle {
// A private Windows waveform handle. Never calls Skyrim audio or changes any
// device/session/master volume. All gain is applied to our own PCM samples.
class WheelWave {
    HWAVEOUT handle{};
    WAVEHDR header{};
    WAVEFORMATEX format{};
    std::vector<char> pcm;
    bool prepared{},playing{};
    static std::uint16_t u16(const char* p){return std::uint8_t(p[0])|(std::uint16_t(std::uint8_t(p[1]))<<8);}
    static std::uint32_t u32(const char* p){return u16(p)|(std::uint32_t(u16(p+2))<<16);}
public:
    WheelWave()=default;
    WheelWave(const WheelWave&)=delete;
    WheelWave& operator=(const WheelWave&)=delete;
    ~WheelWave(){if(!stop())(void)new std::vector<char>(std::move(pcm));} // retain driver-owned memory on device failure
    bool load(const std::filesystem::path& path,double gain) {
        if(handle||!std::isfinite(gain)||gain<0||gain>1)return false;
        std::ifstream f(path,std::ios::binary|std::ios::ate);
        auto length=f.tellg();if(!f||length<44||length>32*1024*1024)return false;
        std::vector<char> bytes(static_cast<std::size_t>(length));f.seekg(0);f.read(bytes.data(),bytes.size());
        if(!f||std::memcmp(bytes.data(),"RIFF",4)||std::memcmp(bytes.data()+8,"WAVE",4)||u32(bytes.data()+4)!=bytes.size()-8)return false;
        WAVEFORMATEX parsed{};std::vector<char> samples;bool haveFormat=false;
        for(std::size_t off=12;off+8<=bytes.size();) {
            auto size=u32(bytes.data()+off+4);auto data=off+8;
            if(size>bytes.size()-data)return false;
            if(!std::memcmp(bytes.data()+off,"fmt ",4)) {
                if(size<16)return false;
                auto* p=bytes.data()+data;parsed.wFormatTag=u16(p);parsed.nChannels=u16(p+2);parsed.nSamplesPerSec=u32(p+4);
                parsed.nAvgBytesPerSec=u32(p+8);parsed.nBlockAlign=u16(p+12);parsed.wBitsPerSample=u16(p+14);haveFormat=true;
            }
            if(!std::memcmp(bytes.data()+off,"data",4))samples.assign(bytes.begin()+data,bytes.begin()+data+size);
            off=data+size+(size&1);
            if(off>bytes.size())return false;
        }
        if(!haveFormat||parsed.wFormatTag!=WAVE_FORMAT_PCM||parsed.wBitsPerSample!=16||
            (parsed.nChannels!=1&&parsed.nChannels!=2)||parsed.nSamplesPerSec<8000||parsed.nSamplesPerSec>96000||
            parsed.nBlockAlign!=parsed.nChannels*2||parsed.nAvgBytesPerSec!=parsed.nSamplesPerSec*parsed.nBlockAlign||
            samples.empty()||samples.size()%parsed.nBlockAlign)return false;
        for(std::size_t i=0;i<samples.size();i+=2) {
            std::int16_t value;std::memcpy(&value,samples.data()+i,2);
            value=static_cast<std::int16_t>(std::lround(value*gain));std::memcpy(samples.data()+i,&value,2);
        }
        format=parsed;pcm=std::move(samples);return true;
    }
    MMRESULT start() {
        if(playing)return MMSYSERR_NOERROR;
        if(pcm.empty())return WAVERR_BADFORMAT;
        if(handle&&!stop())return MMSYSERR_ALLOCATED;
        auto result=waveOutOpen(&handle,WAVE_MAPPER,&format,0,0,CALLBACK_NULL);
        if(result!=MMSYSERR_NOERROR){handle=nullptr;return result;}
        header={};header.lpData=pcm.data();header.dwBufferLength=static_cast<DWORD>(pcm.size());
        result=waveOutPrepareHeader(handle,&header,sizeof(header));
        if(result!=MMSYSERR_NOERROR){stop();return result;}
        prepared=true;header.dwFlags|=WHDR_BEGINLOOP|WHDR_ENDLOOP;header.dwLoops=0xFFFFFFFF;
        result=waveOutWrite(handle,&header,sizeof(header));
        if(result!=MMSYSERR_NOERROR){stop();return result;}
        playing=true;return result;
    }
    bool stop() {
        if(!handle){playing=false;return true;}
        if(waveOutReset(handle)!=MMSYSERR_NOERROR)return false;
        playing=false;
        if(prepared) {
            if(waveOutUnprepareHeader(handle,&header,sizeof(header))!=MMSYSERR_NOERROR)return false;
            prepared=false;
        }
        if(waveOutClose(handle)!=MMSYSERR_NOERROR)return false;
        handle=nullptr;return true;
    }
    bool active()const{return playing;}
    HWAVEOUT nativeHandle()const{return handle;}
    const std::vector<char>& samples()const{return pcm;}
    unsigned sampleRate()const{return format.nSamplesPerSec;}
    unsigned channels()const{return format.nChannels;}
};
}
