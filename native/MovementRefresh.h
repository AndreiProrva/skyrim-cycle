#pragma once
namespace cycle {
// The engine caches derived movement speeds. Updating SpeedMult alone does
// not invalidate that cache. CarryWeight's change callback does; a balanced
// temporary modifier pair triggers it without changing the final actor value.
// Inject operations so the order and inverse pair can be regression tested.
template<class SetSpeed,class ChangeWeight>
void setMovementSpeed(float speed,SetSpeed&& setSpeed,ChangeWeight&& changeWeight) {
    setSpeed(speed);
    changeWeight(1.f);
    changeWeight(-1.f);
}
}
