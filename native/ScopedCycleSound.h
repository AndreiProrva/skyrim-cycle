#pragma once
#include "AudioPolicy.h"
#include "WheelStream.h"
#include <condition_variable>
#include <mutex>

namespace cycle::sound {
using Event=RE::BSAnimationGraphEvent;
using Sink=RE::BSTEventSink<Event>;
using Source=RE::BSTEventSource<Event>;
using Process=RE::BSEventNotifyControl(*)(Sink*,const Event*,Source*);
inline REL::Relocation<Process> originalCharacter,originalPlayer;
inline std::atomic<RE::TESNPC*> base{};
inline std::atomic<RE::FormID> currentBike{},rememberedBike{};
inline std::atomic_bool muteHorse{true};
inline bool suppress(const Event* event) {
    if(!muteHorse.load()||!event||!event->holder)return false;
    auto* actor=event->holder->As<RE::Actor>();if(!actor)return false;
    const char* tag=event->tag.c_str();auto kind=audioEventKind(tag?tag:"");
    if(!kind)return false; // NEVER block a paired-animation, IK or gameplay event
    auto* player=RE::PlayerCharacter::GetSingleton();
    // Resolve the relationship on the main controller task, not reentrantly
    // inside an engine event-source lock.
    auto selected=currentBike.load();bool related=selected!=0;
    // Exact current rider/vehicle pair, NOT every actor of the custom bike race.
    // A different horse, including another C3 actor, always forwards unchanged.
    ActorSoundScope scope{actor->GetFormID(),selected,actor==player,related,
        actor->GetActorBase()==base.load()?rememberedBike.load():0};
    if(!suppressSelectedBikeAudio(scope,tag?tag:"",event->payload.c_str()?event->payload.c_str():""))return false;
    static std::atomic_uint count{};
    if(count.fetch_add(1)<24)SKSE::log::info("Bike sound event muted: actor={:08X}, tag='{}', payload='{}'",actor->GetFormID(),tag?tag:"",event->payload.c_str()?event->payload.c_str():"");
    return true;
}
inline RE::BSEventNotifyControl characterEvent(Sink* self,const Event* event,Source* source) {
    if(suppress(event))return RE::BSEventNotifyControl::kContinue;
    return originalCharacter(self,event,source);
}
inline RE::BSEventNotifyControl playerEvent(Sink* self,const Event* event,Source* source) {
    if(suppress(event))return RE::BSEventNotifyControl::kContinue;
    return originalPlayer(self,event,source);
}
static_assert(std::is_same_v<decltype(&characterEvent),Process>);
static_assert(std::is_same_v<decltype(&playerEvent),Process>);
inline void installEvents() {
    // TESObjectREFR's BSAnimationGraphEvent sink is base #2, at +0x30;
    // ProcessEvent is slot 1 (after its virtual destructor). Preserve sink `this`.
    REL::Relocation<std::uintptr_t> character{RE::VTABLE_Character[2]};
    REL::Relocation<std::uintptr_t> player{RE::VTABLE_PlayerCharacter[2]};
    originalCharacter=character.write_vfunc(1,characterEvent);
    originalPlayer=player.write_vfunc(1,playerEvent);
}
struct WorkerState {
    std::atomic_uint mode{}; // 0=cancel, 1=eligible but stopped, 3=moving
    std::atomic<double> rpm{};
    std::atomic<RE::FormID> saveBike{};std::atomic_bool saveBikeDirty{};
    std::atomic_bool launched{};
    std::mutex mutex;std::condition_variable changed;
    bool enabled{};int volume{20};double referenceRPM{60};
};
inline WorkerState& state(){static auto* value=new WorkerState;return *value;}
inline void remember(RE::FormID id) {
    if(!id||rememberedBike.exchange(id)==id)return;
    auto& s=state();s.saveBike.store(id);s.saveBikeDirty.store(true);s.changed.notify_one();
    SKSE::log::info("Remembered player's bicycle audio source {:08X}",id);
}
inline void primeLastBike(RE::PlayerCharacter* player) {
    if(!player)return;
    if(auto bike=player->QLastRiddenMount().get();bike&&bike->GetActorBase()==base.load())remember(bike->GetFormID());
}
inline WheelMotionGate motionGate;
inline AudioCadenceWindow cadence;
inline void moving(double rotationDelta,double dt,bool eligible,double now) {
    auto& s=state();eligible=eligible&&s.enabled;
    double rpm=eligible?cadence.sample(rotationDelta,dt):0;
    if(!eligible)cadence.reset();
    bool moving=motionGate.sample(rpm,eligible,now);
    unsigned mode=eligible?(moving?3:1):0;
    s.rpm.store(rpm);
    if(s.mode.exchange(mode)!=mode)s.changed.notify_one();
}
inline void silence(){motionGate.reset();cadence.reset();auto& s=state();s.rpm.store(0);if(s.mode.exchange(0)!=0)s.changed.notify_one();}
inline void initialize(RE::TESNPC* bike) {
    base.store(bike);
    // Prefer paths relative to THIS DLL, including a physical MO2 mod path.
    // File loading on the audio worker then does not depend on its current dir.
    std::filesystem::path directory=L"Data/SKSE/Plugins";
    HMODULE module{};
    if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&characterEvent),&module)) {
        std::vector<wchar_t> path(32768);
        auto size=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));
        if(size&&size<path.size())directory=std::filesystem::path(std::wstring(path.data(),size)).parent_path();
    }
    auto config=directory/L"C3CycleAudio.ini";auto ini=config.c_str();
    auto audioDirectory=directory.parent_path().parent_path()/L"Sound/FX/SkyrimCycle";
    auto& s=state();if(s.launched.exchange(true))return;
    s.enabled=GetPrivateProfileIntW(L"Audio",L"WheelSound",1,ini)!=0;
    s.volume=std::clamp<int>(GetPrivateProfileIntW(L"Audio",L"WheelVolumePercent",20,ini),0,100);
    s.referenceRPM=std::clamp<int>(GetPrivateProfileIntW(L"Audio",L"WheelReferenceRPM",60,ini),5,200);
    wchar_t remembered[32]{};
    GetPrivateProfileStringW(L"Audio",L"RememberedPlayerBikeRef",L"0",remembered,32,ini);
    rememberedBike.store(static_cast<RE::FormID>(std::wcstoul(remembered,nullptr,16)));
    SKSE::log::info("Initial player's bicycle sound ref {:08X}",rememberedBike.load());
    muteHorse.store(GetPrivateProfileIntW(L"Audio",L"MuteBikeHorseSounds",1,ini)!=0);
    // Process-lifetime worker. No device/file operation runs inside a game frame.
    std::thread([&s,audioDirectory,config] {
        auto load=[&](const wchar_t* name) {
            auto clip=loadRideClip(audioDirectory/name);
            if(clip.empty())clip=loadRideClip(std::filesystem::path(L"Data/Sound/FX/SkyrimCycle")/name);
            return clip;
        };
        RideSoundClips clips{load(L"wheel_start.wav"),load(L"wheels.wav")};
        if(clips.start.empty()||clips.loop.empty()) {
            SKSE::log::error("Ride audio start/loop WAV missing or not mono PCM16/44100");return;
        }
        RideSoundMixer mixer(std::move(clips),s.referenceRPM);WheelStream stream;
        auto retry=std::chrono::steady_clock::time_point{};
        unsigned loggedStarts=0,loggedStops=0,loggedLoops=0;
        for(;;) {
            if(s.saveBikeDirty.exchange(false)) {
                wchar_t id[16]{};swprintf_s(id,L"%08X",s.saveBike.load());
                if(!WritePrivateProfileStringW(L"Audio",L"RememberedPlayerBikeRef",id,config.c_str()))SKSE::log::warn("Cannot persist player's bike sound ref");
            }
            unsigned mode=s.mode.load();double rpm=s.rpm.load();
            mixer.control(mode==3,mode!=0,rpm);
            if(mixer.starts!=loggedStarts){loggedStarts=mixer.starts;SKSE::log::info("Ride audio START #{}, cadence {:.1f} RPM",loggedStarts,rpm);}
            if(mixer.stops!=loggedStops){loggedStops=mixer.stops;SKSE::log::info("Ride audio FADE TO SILENCE #{} (no end clip)",loggedStops);}
            auto now=std::chrono::steady_clock::now();
            if(!mode)stream.stop(); // hard cancel: NO end recording on pause/dismount/load
            else if(!mixer.silent() && now>=retry) {
                auto result=stream.open();
                if(result==MMSYSERR_NOERROR)result=stream.pump(mixer,s.volume/100.0);
                if(result!=MMSYSERR_NOERROR){stream.stop();mixer.cancel();retry=now+std::chrono::seconds(2);SKSE::log::warn("Ride stream error {}",result);}
            } else if(mixer.silent()&&!stream.pending())stream.stop();
            if(mixer.loopEntries!=loggedLoops){loggedLoops=mixer.loopEntries;SKSE::log::info("Ride audio LOOP #{}, reference {} RPM",loggedLoops,s.referenceRPM);}
            std::unique_lock lock(s.mutex);s.changed.wait_for(lock,std::chrono::milliseconds(5),[&s,mode]{return s.mode.load()!=mode;});
        }
    }).detach();
    SKSE::log::info("Bike sound: actor event filter={}, separate Windows PCM output={}, gain={}%; start/loop, weak RPM modulation, no end recording; no Skyrim mixer calls",muteHorse.load(),s.enabled,s.volume);
}
}
