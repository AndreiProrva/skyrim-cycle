#pragma once
#include "Motion.h"
namespace cycle {
class ParkAnchor {
    bool valid{},watching{};
    double began{},retryAt{};
    unsigned repairs{};
    static double distance(Point a,Point b){return std::hypot(std::hypot(a.x-b.x,a.y-b.y),a.z-b.z);}
public:
    Point position{};
    void reset(){*this={};}
    void mounted(Point p){if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return;position=p;valid=true;watching=false;repairs=0;retryAt=0;}
    void dismount(double now){if(valid&&!watching){watching=true;began=now;}}
    bool shouldRestore(Point current,Point player,bool sameSpace,bool fullyOnFoot,bool paused,double now)const {
        return valid&&watching&&now>=began&&now-began<=15&&now>=retryAt&&repairs<3&&sameSpace&&fullyOnFoot&&!paused&&
            distance(player,position)<700&&std::isfinite(distance(current,position))&&distance(current,position)>210;
    }
    bool active(double now)const{return valid&&watching&&now>=began&&now-began<=15;}
    void attempted(double now){++repairs;retryAt=now+.5;}
};
// All six arguments of the engine's TESObjectREFR::MoveTo_Impl are required.
template<class Function,class Ref,class Handle,class Cell,class World,class Vector>
void forwardReferenceMove(Function&& original,Ref* ref,const Handle& target,Cell* cell,World* world,const Vector& position,const Vector& rotation) {
    original(ref,target,cell,world,position,rotation);
}
}
