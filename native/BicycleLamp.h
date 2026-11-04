#pragma once
#include "BicycleVisuals.h"
#include <RE/N/NiPointLight.h>
#include <RE/S/ShadowSceneNode.h>
#include <RE/B/BSShaderManager.h>
#include <RE/B/BSShadowLight.h>
#include <filesystem>
namespace cycle::lamp {
inline std::atomic_bool requested{};
inline RE::NiPointer<RE::NiPointLight> light;
inline RE::NiPointer<RE::ShadowSceneNode> scene;
inline RE::NiPointer<RE::NiNode> owner;
inline bool active{};
inline RE::NiPointer<RE::NiPointLight> beam;
inline RE::NiPointer<RE::BSLight> beamRender;
inline bool coneEnabled{true},beamAttempted{};
inline void releaseBeam() {
    if(beam) {
        beam->GetLightRuntimeData().fade=0;beam->GetLightRuntimeData().diffuse={0,0,0};
        if(scene&&beamRender)scene->RemoveLight(beamRender);
        if(owner)owner->DetachChild(beam.get());
    }
    beamRender.reset();beam.reset();beamAttempted=false;
}
inline void release(bool reset=true) {
    releaseBeam();
    if(light) {
        light->GetLightRuntimeData().fade=0;
        light->GetLightRuntimeData().diffuse={0,0,0};
        if(scene&&active)scene->RemoveLight(light.get());
        if(owner)owner->DetachChild(light.get());
    }
    light.reset();owner.reset();scene.reset();active=false;
    if(reset)requested.store(false);
}
inline RE::TESObjectLIGH* obsoleteBase{};
inline bool cleanupNeeded{true};
inline void initialize() {
    const auto config=std::filesystem::absolute("Data/SKSE/Plugins/C3CycleLight.ini").wstring();
    coneEnabled=GetPrivateProfileIntW(L"Light",L"ConeEnabled",1,config.c_str())!=0;
    obsoleteBase=RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectLIGH>(0x809,"C3SkyrimCycle.esp");
    cleanupNeeded=true;
}
inline void cleanupLoaded(RE::PlayerCharacter* player) {
    if(!cleanupNeeded||!obsoleteBase||!player)return;
    cleanupNeeded=false;
    std::vector<RE::NiPointer<RE::TESObjectREFR>> obsolete;
    if(auto* world=RE::TES::GetSingleton())world->ForEachReferenceInRange(player,4096,[&](RE::TESObjectREFR* ref) {
        if(ref->GetBaseObject()==obsoleteBase)obsolete.emplace_back(ref);
        return RE::BSContainer::ForEachResult::kContinue;
    });
    for(auto& ref:obsolete){ref->Disable();ref->SetDelete(true);}
}
inline bool update(RE::Actor*,RE::NiNode* root) {
    if(!requested.load()||!root){release(false);return false;}
    auto* current=RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
    if(owner.get()!=root||scene.get()!=current)release(false);
    if(!current)return false;
    if(!light) {
        light.reset(RE::NiPointLight::Create());if(!light)return false;
        owner.reset(root);scene.reset(current);
        light->name=RE::BSFixedString("C3BicycleLanternLight");
        // Forward is local +Y; the front tyre ends around Y=76.3. Put the
        // illumination centre half a metre ahead of it, closer to the road.
        light->local.translate={0,112.f,50.f};
        auto& data=light->GetLightRuntimeData();
        data.ambient={0,0,0};data.diffuse={1.f,.28f,.025f};
        data.radius={420,420,420};data.fade=1.65f;
        light->SetLightAttenuation(420.f);
        owner->AttachChild(light.get());
        RE::NiUpdateData update{};light->Update(update);
        RE::ShadowSceneNode::LIGHT_CREATE_PARAMS params{};
        params.dynamic=true;params.affectLand=true;params.affectWater=true;
        params.neverFades=true;params.fov=90;params.falloff=1;params.nearDistance=1;
        if(!scene->AddLight(light.get(),params)) {release(false);return false;}
        active=true;SKSE::log::info("Bicycle lantern light attached");
    }
    RE::NiUpdateData update{};light->Update(update);
    if(coneEnabled&&!beamAttempted) {
        beamAttempted=true;
        beam.reset(RE::NiPointLight::Create());
        if(beam) {
            beam->name=RE::BSFixedString("C3BicycleHeadlampCone");
            beam->local.translate={0,82,65};
            const auto basis=cycle::headlampRotation();
            for(int r=0;r<3;++r)for(int c=0;c<3;++c)beam->local.rotate.entry[r][c]=basis[r][c];
            auto& data=beam->GetLightRuntimeData();data.ambient={0,0,0};data.diffuse={1.f,.28f,.025f};
            data.radius={840,840,840};data.fade=2.4f;beam->SetLightAttenuation(840);
            owner->AttachChild(beam.get());beam->Update(update);
            RE::ShadowSceneNode::LIGHT_CREATE_PARAMS params{};
            params.dynamic=true;params.shadowLight=true;params.affectLand=true;params.affectWater=true;params.neverFades=true;
            params.fov=cycle::headlampFovRadians; // factory compares this with PI and 2*PI
            params.falloff=1;params.nearDistance=2;params.depthBias=1;
            beamRender.reset(scene->AddLight(beam.get(),params));
            const bool cone=beamRender&&beamRender->IsShadowLight()&&static_cast<RE::BSShadowLight*>(beamRender.get())->GetIsFrustumLight();
            SKSE::log::info("Headlamp candidate: radians {:.4f}, verified frustum={}; warm fill retained",params.fov,cone);
            if(!cone) {releaseBeam();beamAttempted=true;}
        }
    }
    if(beam) {
        beam->Update(update);
        if(beamRender)beamRender->worldTranslate=beam->world.translate;
    }
    return active;
}
}
