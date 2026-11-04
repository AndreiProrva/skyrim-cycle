#pragma once
#include "Gearbox.h"
#include "RideHUD.h"
#include "MovementRefresh.h"
#include "BicycleLamp.h"

namespace cycle::gears {
inline std::atomic<int> selected{defaultGear};
inline std::atomic_bool allowed{};
inline RE::ActorHandle controlled;
inline int applied{};
inline void setSpeed(RE::Actor* bike,float value) {
    auto* owner=bike->AsActorValueOwner();
    cycle::setMovementSpeed(value,
        [&](float speed){owner->SetBaseActorValue(RE::ActorValue::kSpeedMult,speed);},
        [&](float delta){owner->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary,RE::ActorValue::kCarryWeight,delta);});
}
inline void restore() {
    allowed.store(false);
    if(auto bike=controlled.get())setSpeed(bike.get(),100.f);
    controlled={};applied=0;
}
inline void reset(RE::TESNPC* base) {
    restore();selected.store(defaultGear);
    // SpeedMult is stored in saves. Always restore our known baseline rather
    // than multiplying a value that an earlier save already scaled.
    auto* player=RE::PlayerCharacter::GetSingleton();
    if(player)if(auto bike=player->QLastRiddenMount().get();bike&&bike->GetActorBase()==base)
        setSpeed(bike.get(),100.f);
}
inline void apply(RE::Actor* bike) {
    int gear=selected.load();
    if(auto old=controlled.get();old&&old.get()!=bike)restore();
    if(!controlled || applied!=gear) {
        controlled=bike->GetHandle();applied=gear;
        const float weight=bike->AsActorValueOwner()->GetActorValue(RE::ActorValue::kCarryWeight);
        setSpeed(bike,float(100*gearRatio(gear)));
        SKSE::log::info("Cycle gear {}: target {} km/h, refreshed SpeedMult base/effective {}/{}, CarryWeight before/after {}/{}, engine run/jog/walk speeds {}/{}/{}",
            gear,gearSpeed(gear),bike->AsActorValueOwner()->GetBaseActorValue(RE::ActorValue::kSpeedMult),
            bike->AsActorValueOwner()->GetActorValue(RE::ActorValue::kSpeedMult),weight,
            bike->AsActorValueOwner()->GetActorValue(RE::ActorValue::kCarryWeight),bike->GetRunSpeed(),bike->GetJogSpeed(),bike->GetWalkSpeed());
    }
    allowed.store(true);
}
class Input final:public RE::BSTEventSink<RE::InputEvent*> {
public:
    RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* events,RE::BSTEventSource<RE::InputEvent*>*) override {
        if(!events || !allowed.load() || hud::menusBlocked())return RE::BSEventNotifyControl::kContinue;
        for(auto* event=*events;event;event=event->next) {
            if(event->GetDevice()!=RE::INPUT_DEVICE::kKeyboard)continue;
            auto* button=event->AsButtonEvent();if(!button)continue;
            const auto code=button->GetIDCode();
            int gear=gearKey(code);if(!gear&&code!=0x26)continue; // physical L / Russian Д
            if(button->IsDown()) {
                if(gear)selected.store(gear);
                else lamp::requested.store(!lamp::requested.load());
            }
            // Consume only this key, never the whole linked input batch. Other
            // movement/mouse/menu events still reach the game's own handlers.
            button->SetUserEvent(RE::BSFixedString{});button->SetIDCode(0xFFFFFFFF);
            button->GetRuntimeData().value=0;button->GetRuntimeData().heldDownSecs=0;
        }
        return RE::BSEventNotifyControl::kContinue;
    }
};
inline Input input;
inline void install() {if(auto* manager=RE::BSInputDeviceManager::GetSingleton())manager->PrependEventSink(&input);}
}
