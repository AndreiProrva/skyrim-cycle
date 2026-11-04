#pragma once
#include "BicycleVisuals.h"
#include <RE/B/BSSynchronizedClipGenerator.h>
namespace cycle::pairMotion {
struct Scope {std::array<RE::hkbCharacter*,8> playerGraphs{};float targetWorldZ{};RE::FormID bicycle{};};
inline std::atomic<std::shared_ptr<const Scope>> state;
using Generate=void(*)(RE::BSSynchronizedClipGenerator*,const RE::hkbContext&,
    const RE::hkbGeneratorOutput**,RE::hkbGeneratorOutput&,float);
inline REL::Relocation<Generate> original;
inline thread_local unsigned evaluationDepth{};
inline thread_local bool pairedEvaluation{};
inline thread_local bool exitEvaluation{};
inline std::atomic_bool exitSeen{};
inline bool wasExit{};
inline cycle::ExitSeatAnchor seatAnchor;
struct EvaluationScope {
    EvaluationScope(){if(evaluationDepth++==0){pairedEvaluation=false;exitEvaluation=false;}}
    ~EvaluationScope(){if(--evaluationDepth==0){pairedEvaluation=false;exitEvaluation=false;}}
};
inline void clear(){state.store(nullptr);exitSeen.store(false);wasExit=false;seatAnchor.reset();}
inline void publish(RE::PlayerCharacter* player,RE::Actor* bike) {
    auto* root=bike->Get3D();
    auto* bone=root?root->GetObjectByName(RE::BSFixedString("NPC Root [Root]")):nullptr;
    if(!bone){clear();return;}
    Scope scope;scope.bicycle=bike->GetFormID();
    scope.targetWorldZ=(bone->world*RE::NiPoint3{0,0,80.15f}).z;
    const auto sit=player->AsActorState()->GetSitSleepState();
    const bool exiting=sit==RE::SIT_SLEEP_STATE::kWantToStand;
    if(exiting&&!wasExit)exitSeen.store(false);
    wasExit=exiting;
    if(!exiting&&sit==RE::SIT_SLEEP_STATE::kRidingMount&&
       !player->GetActorRuntimeData().boolFlags.any(RE::Actor::BOOL_FLAGS::kGettingOnOffMount)&&
       !bike->GetActorRuntimeData().boolFlags.any(RE::Actor::BOOL_FLAGS::kGettingOnOffMount))
        seatAnchor.seated(scope.bicycle,scope.targetWorldZ);
    if(exiting)scope.targetWorldZ=seatAnchor.exitHeight(scope.bicycle,scope.targetWorldZ);
    RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
    if(player->GetAnimationGraphManager(manager)&&manager) {
        RE::BSSpinLockGuard lock(manager->GetRuntimeData().updateLock);std::size_t i=0;
        for(auto& graph:manager->graphs)if(graph&&i<8)scope.playerGraphs[i++]=&graph->characterInstance;
    }
    state.store(std::make_shared<const Scope>(scope));
}
struct Pose {float t[4],q[4],s[4];};
static_assert(sizeof(Pose)==48);
inline RE::NiTransform transform(const Pose& p) {
    RE::NiTransform value;value.translate={p.t[0],p.t[1],p.t[2]};value.scale=p.s[0];
    value.rotate=RE::NiQuaternion(p.q[3],p.q[0],p.q[1],p.q[2]).ToRotation();return value;
}
inline bool correctOutput(const RE::hkbContext& context,RE::hkbGeneratorOutput& output) {
    auto scope=state.load();if(!scope||!context.character||!context.character->worldFromModel)return false;
    bool ours=false;for(auto* graph:scope->playerGraphs)if(graph==context.character)ours=true;
    if(!ours)return false;
    auto* tracks=output.tracks;if(!tracks)return false;
    auto& master=tracks->masterHeader;
    if(master.numTracks<3||master.numTracks>64||master.numBytes>1048576)return false;
    auto& header=tracks->trackHeaders[2];
    if(header.numData!=99||header.capacity<99||header.elementSizeBytes!=48||static_cast<int>(header.flags)!=0||
       header.type!=RE::hkbGeneratorOutput::TrackType::kQsTransform||header.dataOffset<16+16*master.numTracks||
       header.dataOffset+99*48>master.numBytes)return false;
    auto* pose=reinterpret_cast<Pose*>(reinterpret_cast<char*>(tracks)+header.dataOffset);
    // Standard 99-bone skeleton: COM=4, Pelvis=5. Slots 1..3 are helper
    // Look/Translate/Rotate nodes, not the pelvis chain. Preserve the verified
    // entry branch; use the correct pelvis chain for the exit.
    const auto local=exitEvaluation?
        transform(pose[0])*transform(pose[4])*transform(pose[5]):
        transform(pose[0])*transform(pose[1])*transform(pose[2]);
    Pose world;std::memcpy(&world,context.character->worldFromModel,sizeof(world));
    if(exitEvaluation) {
        // Exit publishes its new model frame in track 0 before the persistent
        // character frame catches up. Use the OUTPUT frame, read-only.
        const auto& frame=tracks->trackHeaders[0];
        if(frame.numData==1&&frame.capacity>=1&&frame.elementSizeBytes==48&&static_cast<int>(frame.flags)==0&&
           frame.type==RE::hkbGeneratorOutput::TrackType::kQsTransform&&frame.onFraction>0&&
           frame.dataOffset>=16+16*master.numTracks&&frame.dataOffset+48<=master.numBytes)
            std::memcpy(&world,reinterpret_cast<const char*>(tracks)+frame.dataOffset,sizeof(world));
    }
    const float before=(transform(world)*local.translate).z;
    const float down=cycle::mountLowering(before,scope->targetWorldZ);
    if(exitEvaluation)return false; // actual rendered pelvis is capped by the final scene pass, not this earlier frame
    if(down>0) {
        // Never mutate the persistent world/root-motion state. The previous
        // version accumulated an offset there and sent the exit below ground.
        const auto worldFrame=transform(world);
        if(worldFrame.scale>0) {
            const auto delta=worldFrame.rotate.Transpose()*RE::NiPoint3{0,0,-down};
            cycle::offsetVisualRoot(pose[0].t,{delta.x,delta.y,delta.z},worldFrame.scale);
        }
        static std::atomic_uint corrections{};
        if(corrections.fetch_add(1)%60==0)SKSE::log::info("Bicycle visual height capped after evaluation: before {:.2f}, target {:.2f}, down {:.2f}",before,scope->targetWorldZ,down);
    }
    return down>0;
}
inline void generate(RE::BSSynchronizedClipGenerator* self,const RE::hkbContext& context,
    const RE::hkbGeneratorOutput** children,RE::hkbGeneratorOutput& output,float offset) {
    original(self,context,children,output,offset);
    auto scope=state.load();if(!scope||!context.character||!context.character->worldFromModel)return;
    bool ours=false;for(auto* graph:scope->playerGraphs)if(graph==context.character)ours=true;
    if(!ours||!self->clipGenerator)return;
    auto* raw=self->clipGenerator->animationName.c_str();if(!raw)return;
    std::string name(raw);for(char& c:name)if(c>='A'&&c<='Z')c+=32;
    if(name.find("paired_mount.hkx")==std::string::npos&&name.find("paired_dismount.hkx")==std::string::npos)return;
    if(evaluationDepth)pairedEvaluation=true;
    if(name.find("paired_dismount.hkx")!=std::string::npos) {
        exitSeen.store(true);if(evaluationDepth)exitEvaluation=true;
    }
    correctOutput(context,output);
}
static_assert(std::is_same_v<decltype(&generate),Generate>);
inline void install() {
    REL::Relocation<std::uintptr_t> table{RE::VTABLE_BSSynchronizedClipGenerator[0]};
    original=table.write_vfunc(0x17,generate);
}
}
