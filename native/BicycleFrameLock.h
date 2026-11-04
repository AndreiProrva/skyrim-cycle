#pragma once
#include "BicycleVisuals.h"
#include "RideState.h"
#include <RE/B/BSFlattenedBoneTree.h>
namespace cycle::frameLock {
enum class Mode {ride,exit};
struct Target {
    Mode mode;RE::FormID bike{};float crank{};float exitZ{};
    RE::NiPointer<RE::NiNode> rider;
    RE::NiPointer<RE::NiAVObject> bicycle,pelvis,leftFoot,rightFoot;
};
inline std::atomic<std::shared_ptr<const Target>> target;
inline std::atomic<RE::NiNode*> riderAddress{};
inline std::atomic_bool ridePoseActive{};
inline std::atomic<float> lastSeatedZ{NAN};
inline RE::FormID lastBike{};
using Update=void(*)(RE::NiNode*,RE::NiUpdateData&,std::uint32_t);
inline std::array<Update,3> original{};
inline std::uintptr_t* hookedTable{};
inline bool tableReady{};
inline thread_local bool updating{};
struct SceneDelta {
    RE::NiTransform delta;
    std::array<RE::NiAVObject*,1024> visited{};std::size_t count{};
    void apply(RE::NiAVObject* node) {
        if(!node||std::find(visited.begin(),visited.begin()+count,node)!=visited.begin()+count||count==visited.size())return;
        visited[count++]=node;
        node->world=delta*node->world;
        node->worldBound.center=delta*node->worldBound.center;
        node->worldBound.radius*=std::abs(delta.scale);
        // Skyrim can flatten the skeleton: skinning reads these world matrices,
        // not just the visible NiNode children. Move both representations once.
        if(auto* tree=netimmerse_cast<RE::BSFlattenedBoneTree*>(node)) {
            auto& bones=tree->GetRuntimeData();
            if(bones.boneEntries&&bones.numBones<=1024)for(std::uint32_t i=0;i<bones.numBones;++i) {
                auto& entry=bones.boneEntries[i];entry.world=delta*entry.world;
                apply(entry.node);
            }
        }
        if(auto* parent=node->AsNode())for(auto& child:parent->GetChildren())apply(child.get());
    }
};
inline void clear(){riderAddress.store(nullptr);target.store(nullptr);ridePoseActive.store(false);lastBike=0;lastSeatedZ.store(NAN);}
inline bool hasExit(){auto value=target.load();return value&&value->mode==Mode::exit;}
inline void rotate(RE::NiAVObject* root,const char* name,float angle,RE::NiUpdateData& update) {
    if(auto* node=root->GetObjectByName(RE::BSFixedString(name))) {
        node->local.rotate.MakeXRotation(angle);node->Update(update);
    }
}
template<int Pass>
void update(RE::NiNode* self,RE::NiUpdateData& data,std::uint32_t flags) {
    original[Pass](self,data,flags);
    if(updating||self!=riderAddress.load(std::memory_order_relaxed))return;
    auto value=target.load();if(!value||value->rider.get()!=self||!self->parent)return;
    if(value->mode==Mode::ride&&!ridePoseActive.load())return; // never anchor the still-active paired entry as a riding pose
    const auto oldWorld=self->world;
    if(!std::isfinite(oldWorld.scale)||oldWorld.scale<=0||self->parent->world.scale<=0)return;
    if(value->mode==Mode::ride) {
        RE::NiTransform saddle;saddle.translate={1.6737349f,-.9770063f,134.9184418f};
        self->local=self->parent->world.Invert()*(value->bicycle->world*saddle);
    } else {
        if(!value->pelvis)return;
        const float down=cycle::mountLowering(value->pelvis->world.translate.z,value->exitZ);
        if(down<=.01f||self->parent->world.scale<=0)return;
        auto delta=self->parent->world.rotate.Transpose()*RE::NiPoint3{0,0,-down};
        self->local.translate=self->local.translate+delta*(1.f/self->parent->world.scale);
    }
    updating=true;
    // Do not run animation/controllers a second time. Move final node worlds,
    // flattened skinning matrices and bounds by one common rigid delta.
    SceneDelta corrected;corrected.delta=(self->parent->world*self->local)*oldWorld.Invert();
    corrected.apply(self);
    if(value->mode==Mode::ride) {
        if(value->pelvis)lastSeatedZ.store(value->pelvis->world.translate.z);
        float crank=value->crank;
        if(value->leftFoot&&value->rightFoot&&value->bicycle->world.scale>0) {
            auto delta=value->bicycle->world.rotate.Transpose()*(value->leftFoot->world.translate-value->rightFoot->world.translate);
            delta=delta*(1.f/value->bicycle->world.scale);
            if(auto fitted=cycle::crankFromAnkles(delta.y,delta.z,0,0))crank=*fitted;
        }
        rotate(value->bicycle.get(),"C3Crank",-crank,data);
        rotate(value->bicycle.get(),"C3RightPedal",crank,data);
        rotate(value->bicycle.get(),"C3LeftPedal",crank,data);
    }
    updating=false;
    static std::atomic_uint samples{};
    if(samples.fetch_add(1)%600==0)SKSE::log::info("Bicycle final frame lock: mode={}, bike={:08X}",value->mode==Mode::ride?"ride":"exit",value->bike);
}
static_assert(std::is_same_v<decltype(&update<0>),Update>);
inline bool installFor(RE::NiNode* node) {
    auto* table=*reinterpret_cast<std::uintptr_t**>(node);
    if(hookedTable)return tableReady&&table==hookedTable;
    for(int i=0;i<3;++i)original[i]=reinterpret_cast<Update>(table[0x2C+i]);
    hookedTable=table;
    const std::array<Update,3> hooks{update<0>,update<1>,update<2>};
    for(int i=0;i<3;++i) {
        const auto expected=reinterpret_cast<std::uintptr_t>(original[i]);
        const auto replacement=reinterpret_cast<std::uintptr_t>(hooks[i]);
        if(!REL::safe_write(reinterpret_cast<std::uintptr_t>(table+0x2C+i),&replacement,sizeof(replacement),&expected,sizeof(expected))) {
            SKSE::log::error("Cannot bind actual rider node update table");return false;
        }
    }
    SKSE::log::info("Frame lock bound to actual rider class: {}",node->GetRTTI()->GetName());
    tableReady=true;
    return true;
}
inline void publish(RE::PlayerCharacter* player,RE::Actor* bike,Mode mode,float crank) {
    auto* body=player->Get3D(false);auto* model=bike->Get3D();
    auto* root=body?body->GetObjectByName(RE::BSFixedString("NPC Root [Root]")):nullptr;
    auto* bicycle=model?model->GetObjectByName(RE::BSFixedString("NPC Root [Root]")):nullptr;
    if(!root||!root->AsNode()||!bicycle||!installFor(root->AsNode())){riderAddress.store(nullptr);target.store(nullptr);return;}
    if(lastBike!=bike->GetFormID()){lastBike=bike->GetFormID();lastSeatedZ.store(NAN);}
    Target value{};value.mode=mode;value.bike=lastBike;value.crank=crank;
    value.rider.reset(root->AsNode());value.bicycle.reset(bicycle);
    value.pelvis.reset(body->GetObjectByName(RE::BSFixedString("NPC Pelvis [Pelv]")));
    value.leftFoot.reset(body->GetObjectByName(RE::BSFixedString("NPC L Foot [Lft ]")));
    value.rightFoot.reset(body->GetObjectByName(RE::BSFixedString("NPC R Foot [Rft ]")));
    value.exitZ=lastSeatedZ.load();
    if(!std::isfinite(value.exitZ))value.exitZ=(bicycle->world*RE::NiPoint3{0,0,80.15f}).z;
    target.store(std::make_shared<const Target>(value));riderAddress.store(value.rider.get());
}
}
