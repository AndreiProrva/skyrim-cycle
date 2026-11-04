#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <RE/H/hkbClipGenerator.h>
#include <RE/H/hkbContext.h>
#include <RE/B/BShkbAnimationGraph.h>
#include <atomic>
#include <array>
#include <chrono>
#include <thread>
#include <string>
#include <fstream>
#include <memory>
#include <cstring>
#include <RE/H/hkbBehaviorGraph.h>
#include <RE/H/hkbGeneratorOutput.h>
#include "Motion.h"
#include "RideState.h"
#include "ParkAnchor.h"
#include <RE/E/ExtraStartingPosition.h>
#include <RE/E/ExtraPackageStartLocation.h>
#include "RiderClip.h"
#include "GenerateForward.h"
#include <RE/H/hkbBlendingTransitionEffect.h>
#include "ScopedCycleSound.h"
#include "GearControls.h"
#include "BicyclePairMotion.h"
#include "BicycleFrameLock.h"

namespace {
using Clock=std::chrono::steady_clock;
std::atomic_bool pending{},loading{},started{};
std::atomic<std::uint64_t> graphCalls{},matchedGraphs{};
cycle::RideGate rideGate;
std::atomic<std::uint64_t> synchronizedSamples{};
RE::TESNPC* bikeBase{};
cycle::Motion motion;
cycle::RideMeter rideMeter;
RE::FormID previousMount{};
struct Transform { float t[4],q[4],s[4]; };
static_assert(sizeof(Transform)==48);
std::array<Transform,99*31> poses;
struct Snapshot {float fraction;Transform root;std::array<RE::hkbCharacter*,8> characters{};};
std::atomic<std::shared_ptr<const Snapshot>> snapshot;
double lastBikeScan{},lastWatchLog{},onFootSince{-1};
RE::ActorHandle watchedBike;
cycle::ParkAnchor parkingAnchor;
RE::ActorHandle parkedBike;
RE::TESObjectCELL* parkedCell{};
RE::TESWorldSpace* parkedWorld{};
RE::NiPoint3 parkedAngle{};
using GraphGenerate=void(*)(RE::hkbBehaviorGraph*,const RE::hkbContext&,
    const RE::hkbGeneratorOutput**,RE::hkbGeneratorOutput&,float);
REL::Relocation<GraphGenerate> originalGenerate;
void clearGraphs(bool preserveExit=false) {
    snapshot.store(nullptr);cycle::pairMotion::clear();
    if(!preserveExit)cycle::frameLock::clear();
}
void publishRiderPose(RE::PlayerCharacter* player,RE::NiAVObject* root,float fraction) {
    auto* bikeRoot=root?root->GetObjectByName(RE::BSFixedString("NPC Root [Root]")):nullptr;
    auto* body=player->Get3D(false);
    auto* riderRoot=body?body->GetObjectByName(RE::BSFixedString("NPC Root [Root]")):nullptr;
    if(bikeRoot && riderRoot && riderRoot->parent) {
        RE::NiTransform saddle;saddle.translate={1.6737349f,-.9770063f,134.9184418f};
        auto local=riderRoot->parent->world.Invert()*(bikeRoot->world*saddle);
        RE::NiQuaternion q(local.rotate);
        Snapshot value{};value.fraction=fraction;
        value.root={{local.translate.x,local.translate.y,local.translate.z,0},{q.x,q.y,q.z,q.w},{local.scale,local.scale,local.scale,0}};
        RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
        if(player->GetAnimationGraphManager(manager) && manager) {
            RE::BSSpinLockGuard lock(manager->GetRuntimeData().updateLock);
            std::size_t i=0;
            for(auto& graph:manager->graphs)if(graph && i<value.characters.size())value.characters[i++]=&graph->characterInstance;
        }
        snapshot.store(std::make_shared<const Snapshot>(value));
    } else snapshot.store(nullptr);
}
bool loadPoses() {
    std::ifstream file("Data/SKSE/Plugins/C3CyclePose.bin",std::ios::binary);
    char magic[8];std::uint32_t bones{},frames{};
    file.read(magic,8);file.read(reinterpret_cast<char*>(&bones),4);file.read(reinterpret_cast<char*>(&frames),4);
    if(!file || std::memcmp(magic,"C3POSE14",8) || bones!=99 || frames!=31)return false;
    file.read(reinterpret_cast<char*>(poses.data()),sizeof(poses));
    if(!file || file.peek()!=std::char_traits<char>::eof())return false;
    for(auto& p:poses) {
        for(float v:p.t)if(!std::isfinite(v))return false;
        float norm=0;for(float v:p.q){if(!std::isfinite(v))return false;norm+=v*v;}
        if(norm<.99f||norm>1.01f)return false;
        for(float v:p.s)if(!std::isfinite(v))return false;
    }
    return true;
}
std::atomic<std::uint64_t> transitionSamples{};
using TransitionGenerate=void(*)(RE::hkbBlendingTransitionEffect*,const RE::hkbContext&,
    const RE::hkbGeneratorOutput**,RE::hkbGeneratorOutput&,float);
REL::Relocation<TransitionGenerate> originalTransition;
bool applyRiderPose(const RE::hkbContext& context,RE::hkbGeneratorOutput& output) {
    if(cycle::pairMotion::pairedEvaluation) {
        cycle::frameLock::ridePoseActive.store(false);
        return cycle::pairMotion::correctOutput(context,output);
    }
    if(!context.character)return false;
    auto state=snapshot.load();if(!state || !output.tracks)return false;
    bool ours=false;for(auto* character:state->characters)if(character==context.character)ours=true;
    if(!ours)return false;
    matchedGraphs.fetch_add(1,std::memory_order_relaxed);
    auto* tracks=output.tracks;auto& m=tracks->masterHeader;
    if(m.numTracks<3||m.numTracks>64||m.numBytes<64||m.numBytes>1048576) {
        static std::atomic_bool warned{};
        if(!warned.exchange(true))SKSE::log::warn("Final pose master rejected: tracks={}, bytes={}",m.numTracks,m.numBytes);
        return false;
    }
    auto& h=tracks->trackHeaders[2];
    if(h.capacity<99||h.numData!=99||h.elementSizeBytes!=48||h.type!=RE::hkbGeneratorOutput::TrackType::kQsTransform ||
       static_cast<int>(h.flags)!=0 || h.dataOffset<16+16*m.numTracks || h.dataOffset+99*48>m.numBytes) {
        static std::atomic_bool warned{};
        if(!warned.exchange(true))SKSE::log::warn("Final pose rejected safely: count={}, capacity={}, stride={}, flags={}, offset={}, bytes={}",h.numData,h.capacity,h.elementSizeBytes,static_cast<int>(h.flags),h.dataOffset,m.numBytes);
        return false;
    }
    std::array<Transform,99> result;
    float frame=std::clamp(state->fraction,0.f,1.f)*30;int a=std::min(int(frame),29);float f=frame-a;
    for(int bone=0;bone<99;++bone) {
        auto& x=poses[a*99+bone];auto& y=poses[(a+1)*99+bone];auto& z=result[bone];
        float dot=0;for(int j=0;j<4;++j)dot+=x.q[j]*y.q[j];
        float norm=0;
        for(int j=0;j<4;++j){z.t[j]=x.t[j]+f*(y.t[j]-x.t[j]);z.s[j]=x.s[j]+f*(y.s[j]-x.s[j]);
            z.q[j]=x.q[j]+f*((dot<0?-y.q[j]:y.q[j])-x.q[j]);norm+=z.q[j]*z.q[j];}
        for(float& q:z.q)q/=std::sqrt(norm);
    }
    result[0]=state->root;
    // Scene-parent coordinates match the rendered skeleton. The previous
    // Havok-frame conversion introduced a second, different heading on turns.
    std::memcpy(reinterpret_cast<char*>(tracks)+h.dataOffset,result.data(),sizeof(result));
    h.onFraction=1.f; // Full replacement is valid even at the first crossfade frame.
    cycle::frameLock::ridePoseActive.store(true);
    synchronizedSamples.fetch_add(1);
    return true;
}
void graphGenerate(RE::hkbBehaviorGraph* self,const RE::hkbContext& context,
    const RE::hkbGeneratorOutput** children,RE::hkbGeneratorOutput& output,float offset) {
    cycle::pairMotion::EvaluationScope evaluation;
    cycle::forwardGenerate(originalGenerate,self,context,children,output,offset);
    graphCalls.fetch_add(1,std::memory_order_relaxed);
    applyRiderPose(context,output);
}
void transitionGenerate(RE::hkbBlendingTransitionEffect* self,const RE::hkbContext& context,
    const RE::hkbGeneratorOutput** children,RE::hkbGeneratorOutput& output,float offset) {
    cycle::pairMotion::EvaluationScope evaluation;
    cycle::forwardGenerate(originalTransition,self,context,children,output,offset);
    // Apply after transition mixing, not before it can blend in a cached horse pose.
    // The same snapshot gate disables ALL writes during mount/dismount.
    if(applyRiderPose(context,output))transitionSamples.fetch_add(1,std::memory_order_relaxed);
}
static_assert(std::is_same_v<decltype(&graphGenerate),GraphGenerate>);
static_assert(std::is_same_v<decltype(&transitionGenerate),TransitionGenerate>);
void inspectNearby(RE::PlayerCharacter* player,double now) {
    // Compatibility repair only after three seconds fully on foot. Never enable
    // AI during an interaction: the engine can disable it for paired actions.
    bool onFoot=player->AsActorState()->GetSitSleepState()==RE::SIT_SLEEP_STATE::kNormal &&
        !player->IsOnMount() && !player->GetActorRuntimeData().boolFlags.any(RE::Actor::BOOL_FLAGS::kGettingOnOffMount);
    if(!onFoot)onFootSince=-1;
    else if(onFootSince<0)onFootSince=now;
    if(now-lastBikeScan>1 && onFootSince>=0 && now-onFootSince>3) {
        lastBikeScan=now;
        if(auto* world=RE::TES::GetSingleton())world->ForEachReferenceInRange(player,4096,[&](RE::TESObjectREFR* ref) {
            auto* actor=ref->As<RE::Actor>();
            RE::NiPointer<RE::Actor> rider;
            if(actor && actor->GetActorBase()==bikeBase && !actor->IsAIEnabled() &&
                !actor->GetActorRuntimeData().boolFlags.any(RE::Actor::BOOL_FLAGS::kGettingOnOffMount) &&
                !actor->GetMountedBy(rider)) {
                actor->EnableAI(true);SKSE::log::info("Re-enabled saved bike AI {:08X}",actor->GetFormID());
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });
    }
    if(now-lastWatchLog>3) {
        lastWatchLog=now;
        if(auto bike=watchedBike.get()) {
            auto p=bike->GetPosition();auto* model=bike->Get3D();auto* pack=bike->GetCurrentPackage();
            SKSE::log::info("Bike reference {:08X}: position [{:.1f},{:.1f},{:.1f}], disabled={}, 3D={}, culled={}, package={:08X}",
                bike->GetFormID(),p.x,p.y,p.z,bike->IsDisabled(),bool(model),model?model->GetAppCulled():false,pack?pack->GetFormID():0);
        } else if(watchedBike)SKSE::log::warn("Watched bike handle no longer resolves");
    }
}
bool changingMount(RE::Actor* actor) {
    return actor->GetActorRuntimeData().boolFlags.any(RE::Actor::BOOL_FLAGS::kGettingOnOffMount);
}
bool rotate(RE::NiAVObject* root,const char* name,float angle) {
    auto* bone=root->GetObjectByName(RE::BSFixedString(name));
    if(!bone) return false;
    bone->local.rotate.MakeXRotation(angle);
    RE::NiUpdateData update{};
    bone->Update(update);
    return true;
}
void logFeet(RE::PlayerCharacter* player,RE::NiAVObject* mountRoot,float phase) {
    auto* body=player->Get3D(false);
    auto* base=mountRoot->GetObjectByName(RE::BSFixedString("NPC Root [Root]"));
    auto* crank=mountRoot->GetObjectByName(RE::BSFixedString("C3Crank"));
    if(!body||!base||!crank||base->world.scale<=0)return;
    for(auto side:{'L','R'}) {
        const char* name=side=='L'?"NPC L Foot [Lft ]":"NPC R Foot [Rft ]";
        auto* foot=body->GetObjectByName(RE::BSFixedString(name));
        if(!foot)continue;
        auto local=base->world.rotate.Transpose()*(foot->world.translate-crank->world.translate);
        local=local*(1.0f/base->world.scale);
        const double angle=side=='L'?std::atan2(local.y+4.9,-(local.z-9.87)):
            std::atan2(-(local.y+4.9),local.z-9.87);
        const auto error=std::remainder(angle-phase,2*cycle::pi);
        SKSE::log::info("Foot {} relative crank: [{:.2f},{:.2f},{:.2f}], approximate phase error {:.3f} rad",
            side,local.x,local.y,local.z,error);
    }
}
void restoreParkedBike(RE::PlayerCharacter* player,double now,bool paused) {
    auto bike=parkedBike.get();if(!bike || bike->GetActorBase()!=bikeBase || bike->IsDisabled() || !parkedCell)return;
    bool foot=player->AsActorState()->GetSitSleepState()==RE::SIT_SLEEP_STATE::kNormal && !player->IsOnMount() &&
        !changingMount(player) && !changingMount(bike.get());
    RE::NiPointer<RE::Actor> rider;
    if(bike->GetMountedBy(rider))foot=false;
    bool sameSpace=player->GetWorldspace()==parkedWorld && (parkedWorld || player->GetParentCell()==parkedCell);
    auto current=bike->GetPosition();auto pp=player->GetPosition();
    if(!parkingAnchor.shouldRestore({current.x,current.y,current.z},{pp.x,pp.y,pp.z},sameSpace,foot,paused,now)) {
        auto anchor=parkingAnchor.position;
        static double lastGuardLog{};
        double jump=std::hypot(std::hypot(current.x-anchor.x,current.y-anchor.y),current.z-anchor.z);
        if(parkingAnchor.active(now)&&jump>210&&now-lastGuardLog>1) {
            lastGuardLog=now;
            SKSE::log::warn("Parking correction deferred: foot={}, sameSpace={}, paused={}, player=[{:.1f},{:.1f},{:.1f}], anchor=[{:.1f},{:.1f},{:.1f}]",foot,sameSpace,paused,pp.x,pp.y,pp.z,anchor.x,anchor.y,anchor.z);
        }
        return;
    }
    auto p=parkingAnchor.position;RE::NiPoint3 target{float(p.x),float(p.y),float(p.z)};
    SKSE::log::warn("Restoring SAME parked bike {:08X}: [{:.1f},{:.1f},{:.1f}] -> [{:.1f},{:.1f},{:.1f}]",bike->GetFormID(),current.x,current.y,current.z,target.x,target.y,target.z);
    if(auto* start=bike->extraList.GetByType<RE::ExtraStartingPosition>())
        SKSE::log::info("Saved bike start: [{:.1f},{:.1f},{:.1f}]",start->startPosition.pos.x,start->startPosition.pos.y,start->startPosition.pos.z);
    if(auto* start=bike->extraList.GetByType<RE::ExtraPackageStartLocation>())
        SKSE::log::info("Saved bike package start: [{:.1f},{:.1f},{:.1f}]",start->worldLoc.locPt.x,start->worldLoc.locPt.y,start->worldLoc.locPt.z);
    // Exact binding used by CommonLib TESObjectREFR::MoveTo_Impl; one relocation
    // carries the reference, target handle, cached cell/world, position AND angle.
    // No DLL-wide movement hook, new bike, AI toggle or raw pose/data write.
    using Move=void(*)(RE::TESObjectREFR*,const RE::ObjectRefHandle&,RE::TESObjectCELL*,RE::TESWorldSpace*,const RE::NiPoint3&,const RE::NiPoint3&);
    static REL::Relocation<Move> move{RELOCATION_ID(56227,56626)};
    auto handle=static_cast<RE::TESObjectREFR*>(player)->GetHandle();
    parkingAnchor.attempted(now);
    cycle::forwardReferenceMove(move,static_cast<RE::TESObjectREFR*>(bike.get()),handle,parkedCell,parkedWorld,target,parkedAngle);
}
void tick() {
    struct Done { ~Done(){pending.store(false,std::memory_order_release);} } done;
    if(loading.load(std::memory_order_acquire) || !bikeBase) {cycle::gears::allowed.store(false);clearGraphs();return;}
    auto* player=RE::PlayerCharacter::GetSingleton();
    if(!player)return;
    cycle::lamp::cleanupLoaded(player);
    auto now=std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
    auto* ui=RE::UI::GetSingleton();
    restoreParkedBike(player,now,ui&&ui->GameIsPaused());
    inspectNearby(player,now);
    RE::NiPointer<RE::Actor> mount;
    if(!player || !player->GetMount(mount) || !mount || mount->GetActorBase()!=bikeBase) {
        parkingAnchor.dismount(now);
        // A relocation can occur just as GetMount becomes false. Try again after
        // marking release, before returning to the next main-thread tick.
        restoreParkedBike(player,now,ui&&ui->GameIsPaused());
        cycle::sound::primeLastBike(player);
        cycle::sound::currentBike.store(0);cycle::sound::silence();
        if(previousMount) SKSE::log::info("Bike rider released; parking preserves mounting AI");
        cycle::gears::restore();rideMeter.reset();
        cycle::lamp::release();
        cycle::hud::publish({},now);
        const bool exitTail=!mount&&changingMount(player)&&
            player->AsActorState()->GetSitSleepState()==RE::SIT_SLEEP_STATE::kWantToStand&&cycle::frameLock::hasExit();
        clearGraphs(exitTail);rideGate.reset();previousMount=0;motion.reset();return;
    }
    cycle::sound::currentBike.store(mount->GetFormID());cycle::sound::remember(mount->GetFormID());
    watchedBike=mount->GetHandle();
    cycle::pairMotion::publish(player,mount.get());
    auto sit=player->AsActorState()->GetSitSleepState();
    bool transition=changingMount(player)||changingMount(mount.get())||sit==RE::SIT_SLEEP_STATE::kWantToStand;
    if(transition || !player->IsOnMount() || sit!=RE::SIT_SLEEP_STATE::kRidingMount)parkingAnchor.dismount(now);
    if(!rideGate.sample(true,player->IsOnMount(),sit==RE::SIT_SLEEP_STATE::kRidingMount,transition,now)) {
        if(sit==RE::SIT_SLEEP_STATE::kWantToStand)
            cycle::frameLock::publish(player,mount.get(),cycle::frameLock::Mode::exit,0);
        else if(rideGate.poseReady())
            cycle::frameLock::publish(player,mount.get(),cycle::frameLock::Mode::ride,0);
        else cycle::frameLock::clear();
        cycle::gears::restore();rideMeter.reset();
        cycle::lamp::release();
        cycle::hud::publish({},now);
        const auto bridgeFraction=static_cast<float>(motion.fraction());
        cycle::sound::silence();snapshot.store(nullptr);motion.reset();
        if(rideGate.poseReady()||(sit==RE::SIT_SLEEP_STATE::kWantToStand&&!cycle::pairMotion::exitSeen.load()))
            publishRiderPose(player,mount->Get3D(),bridgeFraction);
        static double lastTransition{};
        if(now-lastTransition>.5){lastTransition=now;SKSE::log::info("Paired transition left to engine: sit={}, onMount={}, transition={}",static_cast<int>(sit),player->IsOnMount(),transition);}
        return;
    }
    auto* root=mount->Get3D();
    if(!root) {cycle::lamp::release();cycle::hud::publish({},now);cycle::gears::restore();rideMeter.reset();cycle::sound::silence();clearGraphs();motion.reset();return;}
    cycle::gears::apply(mount.get());
    if(previousMount!=mount->GetFormID()) {motion.reset();previousMount=mount->GetFormID();}
    const auto position=mount->GetPosition();
    // Only record actual, settled mounted positions. Never overwrite the parked
    // anchor with a stale GetMount relation during the exit animation.
    parkingAnchor.mounted({position.x,position.y,position.z});
    parkedBike=mount->GetHandle();parkedCell=mount->GetParentCell();parkedWorld=mount->GetWorldspace();parkedAngle=mount->GetAngle();
    const auto dt=now-motion.previousTime;
    const auto speed=motion.initialized && dt>0 && dt<.25 ?
        std::hypot(position.x-motion.previous.x,position.y-motion.previous.y)/dt/70*3.6:0.0;
    const int gear=cycle::gears::selected.load();
    const auto phase=static_cast<float>(motion.sample({position.x,position.y,position.z},mount->GetAngleZ(),now,ui&&ui->GameIsPaused(),cycle::gearRatio(gear)));
    const double crankDelta=motion.crankDelta;
    const double wheelDelta=motion.wheelDelta;
    rideMeter.sample(crankDelta,wheelDelta*cycle::radiusUnits/70,dt,now,ui&&ui->GameIsPaused());
    auto* bikeRoot=root->GetObjectByName(RE::BSFixedString("NPC Root [Root]"));
    const bool lampOn=cycle::lamp::update(mount.get(),bikeRoot?bikeRoot->AsNode():nullptr);
    cycle::hud::publish({true,gear,rideMeter.rpm,rideMeter.kmh,lampOn},now);
    publishRiderPose(player,root,static_cast<float>(motion.fraction()));
    cycle::frameLock::publish(player,mount.get(),cycle::frameLock::Mode::ride,static_cast<float>(motion.crankAngle));
    cycle::sound::moving(crankDelta,dt,!(ui&&ui->GameIsPaused()),now);
    // Custom nodes are absent from vanilla HKX tracks and aren't overwritten by horse gait clips.
    const bool front=rotate(root,"C3FrontWheel",phase);
    const bool rear=rotate(root,"C3RearWheel",phase);
    float crankPhase=static_cast<float>(motion.crankAngle);
    // Main-task fallback uses the authored phase; never feed last frame's feet
    // into this frame's crank. The final scene pass fits actual feet afterwards.
    // The exported NIF skin axes require the opposite sign from the ankle
    // phase convention. Reverse the crank and its pedal-level compensation.
    const float rigCrankPhase=-crankPhase;
    const bool crank=rotate(root,"C3Crank",rigCrankPhase);
    rotate(root,"C3RightPedal",-rigCrankPhase);rotate(root,"C3LeftPedal",-rigCrankPhase);
    if(!front||!rear||!crank) {
        static unsigned failures=0;
        if(++failures==1) SKSE::log::error("C3 animation nodes missing: front={}, rear={}, crank={}",front,rear,crank);
    }
    static double lastLog{};
    if(now-lastLog>5) {
        lastLog=now;
        SKSE::log::info("Bike motion: {:.2f} km/h, phase {:.3f}, nodes {}{}{}, rider clip samples {}",
            speed,phase,front,rear,crank,synchronizedSamples.load());
        SKSE::log::info("Animation diagnostics: graphs={}, matches={}, transition poses={}, distance phase={:.3f}, visible crank phase={:.3f}",graphCalls.load(),matchedGraphs.load(),transitionSamples.load(),phase,crankPhase);
        logFeet(player,root,crankPhase);
    }

}
void message(SKSE::MessagingInterface::Message* event) {
    switch(event->type) {
    case SKSE::MessagingInterface::kPreLoadGame:
        cycle::lamp::release();
        cycle::gears::restore();cycle::hud::publish({},0,true);rideMeter.reset();
        cycle::sound::currentBike.store(0);cycle::sound::silence();loading.store(true);clearGraphs();break;
    case SKSE::MessagingInterface::kNewGame:
    case SKSE::MessagingInterface::kPostLoadGame:
        cycle::lamp::release();cycle::lamp::cleanupNeeded=true;
        cycle::gears::reset(bikeBase);cycle::hud::publish({},0,true);rideMeter.reset();
        cycle::sound::currentBike.store(0);cycle::sound::silence();motion.reset();previousMount=0;rideGate.reset();parkingAnchor.reset();parkedBike={};parkedCell=nullptr;parkedWorld=nullptr;watchedBike={};lastBikeScan=0;onFootSince=-1;loading.store(false);clearGraphs();break;
    case SKSE::MessagingInterface::kDataLoaded:
        bikeBase=RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESNPC>(0x800,"C3SkyrimCycle.esp");
        if(!bikeBase) {SKSE::log::error("C3SkyrimCycle.esp bike actor missing; controller inactive");break;}
        cycle::sound::initialize(bikeBase);
        cycle::lamp::initialize();
        cycle::gears::install();
        if(!started.exchange(true)) std::thread([]{
            while(true) {
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
                if(!pending.exchange(true,std::memory_order_acq_rel)) SKSE::GetTaskInterface()->AddTask(tick);
            }
        }).detach();
        SKSE::log::info("Keyboard controller ready, actor {:08X}; motion follows world distance",bikeBase->GetFormID());
        break;
    }
}
}
using namespace REL::literals;
SKSEPluginInfo(.Version="0.1.24.0"_v,.Name="C3KeyboardMotion",.Author="Local Skyrim Cycle",.RuntimeCompatibility={"1.7.104.0"_v});
SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    if(skse->IsEditor() || skse->RuntimeVersion()!=REL::Version(1,7,104,0) || skse->SKSEVersion()!=REL::Version(2,3,1,0).pack())return false;
    SKSE::Init(skse,SKSE::InitInfo{.log=true,.logName="C3KeyboardMotion"});
    if(!SKSE::GetTaskInterface() || !SKSE::GetMessagingInterface())return false;
    if(!loadPoses()){SKSE::log::error("Invalid or missing C3CyclePose.bin");return false;}
    REL::Relocation<std::uintptr_t> table{RE::VTABLE_hkbBehaviorGraph[0]};
    originalGenerate=table.write_vfunc(0x17,graphGenerate);
    REL::Relocation<std::uintptr_t> transitionTable{RE::VTABLE_hkbBlendingTransitionEffect[0]};
    originalTransition=transitionTable.write_vfunc(0x17,transitionGenerate);
    cycle::sound::installEvents();
    cycle::pairMotion::install();
    SKSE::GetMessagingInterface()->RegisterListener(message);
    SKSE::log::info("C3 Keyboard Motion 0.1.24: scene-parent rider anchor, final actual-node frame lock and same-pass foot/crank fit, final exit visual cap; brightness only changed for light; scoped bicycle only; runtime 1.7.104, SKSE 2.3.1");
    return true;
}
