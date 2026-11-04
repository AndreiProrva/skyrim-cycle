#include "Motion.h"
#include "RideState.h"
#include "ParkAnchor.h"
#include "AudioPolicy.h"
#include "RiderClip.h"
#include "GenerateForward.h"
#include "Gearbox.h"
#include "MovementRefresh.h"
#include "BicycleVisuals.h"
#include <cassert>
#include <iostream>
int main() {
    assert(cycle::headlampFovRadians>0&&cycle::headlampFovRadians<cycle::pi);
    assert(std::abs(cycle::headlampFovRadians*180/cycle::pi-52)<1e-4);
    cycle::ExitSeatAnchor exitSeat;
    exitSeat.seated(123,-7000);
    assert(exitSeat.exitHeight(123,-6850)==-7000); // paired bike motion cannot raise the target
    assert(exitSeat.exitHeight(124,-6850)==-6850); // another mount must not reuse the target
    exitSeat.seated(123,NAN);assert(exitSeat.exitHeight(123,-6850)==-7000);
    exitSeat.reset();assert(exitSeat.exitHeight(123,-6850)==-6850);
    const std::array<float,4> persistentWorld{100,200,300,1};
    for(int frame=0;frame<600;++frame) {
        float poseRoot[4]{2,3,4,9}; // freshly generated pose each frame
        assert(cycle::offsetVisualRoot(poseRoot,{0,0,-70},2));
        assert(poseRoot[0]==2&&poseRoot[1]==3&&poseRoot[2]==-31&&poseRoot[3]==9);
        assert(persistentWorld[2]==300); // root-motion state cannot be modified by this API
    }
    float unmodified[4]{1,2,3,4};
    assert(!cycle::offsetVisualRoot(unmodified,{0,0,-1},0)&&unmodified[2]==3);
    assert(!cycle::offsetVisualRoot(unmodified,{0,0,NAN},1)&&unmodified[2]==3);
    const auto lightBasis=cycle::headlampRotation();
    assert(lightBasis[1][0]>.95f&&lightBasis[2][0]<0); // beam forward and down
    for(int a=0;a<3;++a)for(int b=0;b<3;++b) {
        float dot=0;for(int row=0;row<3;++row)dot+=lightBasis[row][a]*lightBasis[row][b];
        assert(std::abs(dot-(a==b?1.f:0.f))<1e-5); // orthonormal camera basis
    }
    for(float origin:{-9000.f,0.f,20000.f}) {
        float worldPelvis=origin+145.f,target=origin+80.15f;
        worldPelvis-=cycle::mountLowering(worldPelvis,target);
        assert(std::abs(worldPelvis-target)<.002);
        assert(cycle::mountLowering(worldPelvis,target)==0);
    }
    assert(cycle::mountLowering(80,80)==0);
    assert(cycle::mountLowering(65,80)==0); // grounded rider must not be lowered
    float entryHeight=135;
    entryHeight-=cycle::mountLowering(entryHeight,80);
    assert(entryHeight==80);
    entryHeight-=cycle::mountLowering(entryHeight,80);
    assert(entryHeight==80); // nested Generate calls cannot accumulate offsets
    assert(cycle::mountLowering(NAN,80)==0);
    float pairedHeight=190;
    pairedHeight-=cycle::mountLowering(pairedHeight,80);
    assert(pairedHeight==80); // parent's additional rise exceeded the old 84-unit guard
    pairedHeight-=cycle::mountLowering(pairedHeight,80);
    assert(pairedHeight==80); // same correction used on entry AND exit
    assert(cycle::mountLowering(500,80)==0); // malformed/unrelated pose rejected
    float baseSpeed=100,weight=300,cachedSpeed=100;
    int writes=0,refreshes=0;
    auto write=[&](float speed){baseSpeed=speed;++writes;};
    auto refresh=[&](float delta){
        assert(writes>0); // invalidation must follow the SpeedMult update
        weight+=delta;cachedSpeed=baseSpeed;++refreshes;
    };
    for(int gear:{1,8,2,5,1,2}) {
        float requested=float(100*cycle::gearRatio(gear));
        cycle::setMovementSpeed(requested,write,refresh);
        assert(cachedSpeed==requested&&weight==300);
    }
    cycle::setMovementSpeed(100,write,refresh); // release/load restores derived speed too
    assert(cachedSpeed==100&&weight==300&&writes==7&&refreshes==14);
    for(int gear=1;gear<=8;++gear) {
        assert(cycle::gearSpeed(gear)==14+4*(gear-1));
        assert(cycle::gearKey(gear+1)==gear);
        cycle::Motion geared;
        geared.sample({0,0,0},0,0,false,cycle::gearRatio(gear));
        const double d=18/3.6*70*.1*cycle::gearRatio(gear);
        geared.sample({0,d,0},0,.1,false,cycle::gearRatio(gear));
        assert(std::abs(geared.crankDelta+35/cycle::radiusUnits)<1e-8);
        assert(std::abs(geared.wheelDelta+ d/cycle::radiusUnits)<1e-8);
        double crank=geared.crankAngle,wheel=geared.angle;
        geared.sample({0,d,0},0,.2,false,cycle::gearRatio(9-gear));
        assert(geared.crankAngle==crank&&geared.angle==wheel); // shift cannot rephase a stationary rider
        geared.sample({0,10000,0},0,.3,false,cycle::gearRatio(gear));
        assert(geared.crankDelta==0&&geared.wheelDelta==0); // no teleport-derived HUD values
    }
    assert(cycle::gearSpeed(-1)==14&&cycle::gearSpeed(99)==42);
    assert(cycle::gearRatio(cycle::defaultGear)==1);
    assert(!cycle::gearKey(0x01)&&!cycle::gearKey(0x0A));
    cycle::RideMeter meter;
    for(int i=1;i<=20;++i)meter.sample(-2*cycle::pi*.01,.05,.01,i*.01,false);
    assert(std::abs(meter.rpm-60)<1e-7&&std::abs(meter.kmh-18)<1e-7);
    for(int i=21;i<=50;++i)meter.sample(0,0,.01,i*.01,false);
    assert(meter.rpm==0&&meter.kmh==0);
    meter.sample(-1,1,.016,.6,true);assert(meter.rpm==0&&meter.kmh==0);
    cycle::Motion fast;
    fast.sample({0,0,0},0,0,false,cycle::gearRatio(8));
    // A delayed frame at 42 km/h traverses more than half a wheel revolution.
    // Raw deltas retain distance; deriving it from wrapped phases loses turns.
    const double fastDistance=42/3.6*70*.2;
    fast.sample({0,fastDistance,0},0,.2,false,cycle::gearRatio(8));
    assert(std::abs(fast.wheelDelta+fastDistance/cycle::radiusUnits)<1e-8);
    cycle::RideMeter fastMeter;
    fastMeter.sample(fast.crankDelta,fast.wheelDelta*cycle::radiusUnits/70,.2,.2,false);
    assert(std::abs(fastMeter.kmh-42)<1e-7);
    cycle::ParkAnchor anchor;
    anchor.mounted({34307,19582,-6921});anchor.dismount(10);
    cycle::Point home{37567,3256,-4642},nearby{34330,19590,-6921};
    assert(anchor.shouldRestore(home,nearby,true,true,false,10.1));
    assert(!anchor.shouldRestore(home,nearby,true,false,false,10.1)); // paired animation
    assert(!anchor.shouldRestore(home,nearby,true,true,true,10.1)); // pause
    assert(!anchor.shouldRestore(home,nearby,false,true,false,10.1)); // door/world change
    assert(!anchor.shouldRestore(home,home,true,true,false,10.1)); // player travelled too
    assert(!anchor.shouldRestore(nearby,nearby,true,true,false,10.1)); // normal settling
    assert(!anchor.shouldRestore(home,nearby,true,true,false,26)); // expired window
    anchor.attempted(10.1);
    assert(!anchor.shouldRestore(home,nearby,true,true,false,10.2));
    assert(anchor.shouldRestore(home,nearby,true,true,false,10.7));
    anchor.attempted(10.7);anchor.attempted(11.3);
    assert(!anchor.shouldRestore(home,nearby,true,true,false,12)); // bounded retries
    anchor.mounted({0,0,0});anchor.dismount(20);
    assert(anchor.shouldRestore({500,0,0},{0,0,0},true,true,false,21));
    anchor.reset();assert(!anchor.shouldRestore(home,nearby,true,true,false,21));
    struct Ref{};struct Handle{};struct Cell{};struct World{};
    Ref ref;Handle handle;Cell cell;World world;cycle::Point pos{1,2,3},rot{4,5,6};bool moved=false;
    auto originalMove=[&](Ref* r,const Handle& h,Cell* c,World* w,const cycle::Point& p,const cycle::Point& angle){
        assert(r==&ref && &h==&handle && c==&cell && w==&world && &p==&pos && &angle==&rot);moved=true;
    };
    cycle::forwardReferenceMove(originalMove,&ref,handle,&cell,&world,pos,rot);assert(moved);

    using Scope=cycle::ActorSoundScope;
    assert(cycle::suppressSelectedBikeAudio(Scope{100,100,false,true},"FootFront",""));
    assert(!cycle::suppressSelectedBikeAudio(Scope{101,100,false,true},"FootFront","")); // another bike/horse
    assert(!cycle::suppressSelectedBikeAudio(Scope{100,100,false,false},"SoundPlay","NPCHorseWhinny")); // normal player horse
    assert(!cycle::suppressSelectedBikeAudio(Scope{100,0,false,false},"SoundPlay","NPCHorseWhinny")); // on foot
    assert(cycle::suppressSelectedBikeAudio(Scope{0x14,100,true,true},"NPCSoundPlay","NPCHorseMount"));
    assert(!cycle::suppressSelectedBikeAudio(Scope{0x14,100,true,true},"SoundPlay","WPNBowZoomIn"));
    assert(!cycle::suppressSelectedBikeAudio(Scope{100,100,false,true},"HorseExit",""));
    assert(cycle::suppressSelectedBikeAudio(Scope{100,0,false,false,100},"SoundPlay","NPCHorseIdleHead03"));
    assert(!cycle::suppressSelectedBikeAudio(Scope{101,0,false,false,100},"SoundPlay","NPCHorseIdleHead03"));
    assert(!cycle::suppressSelectedBikeAudio(Scope{0x14,0,true,false,100},"SoundPlay","NPCHorseIdleHead03"));
    cycle::WheelMotionGate movingSound;
    assert(!movingSound.sample(2,true,0));
    assert(!movingSound.sample(0,true,.2)); // one physics spike is not riding
    movingSound.reset();
    for(int i=0;i<30;++i) {
        bool request=movingSound.sample(i%2?0:13,true,i*.016);
        if(i>=10)assert(request); // alternating stationary samples do not restart output
    }
    assert(!movingSound.sample(0,true,.8));
    assert(!movingSound.sample(13,false,.9)); // pause/paired/on-foot always stop
    assert(!movingSound.sample(13,true,1)); // fresh debounce
    cycle::WheelLoopGate audio;
    assert(audio.sample(true,0)==cycle::WheelLoopGate::start);
    for(int i=1;i<10000;++i)assert(audio.sample(true,i*.008)==cycle::WheelLoopGate::none);
    assert(audio.sample(false,80)==cycle::WheelLoopGate::stop);
    assert(audio.sample(false,81)==cycle::WheelLoopGate::none);
    assert(audio.sample(true,82)==cycle::WheelLoopGate::start);
    audio.failed(82);
    assert(audio.sample(true,82.5)==cycle::WheelLoopGate::none);
    assert(audio.sample(true,83.1)==cycle::WheelLoopGate::start);
    assert(audio.sample(false,84)==cycle::WheelLoopGate::stop);
    assert(cycle::audioEventKind("FootFront")==cycle::AudioEventKind::foot);
    assert(cycle::audioEventKind("NPCSoundPlay.NPCHorseMount")==cycle::AudioEventKind::play);
    assert(!cycle::audioEventKind("FootIKOn"));
    assert(!cycle::audioEventKind("SoundStop"));
    assert(!cycle::audioEventKind("HorseExit"));
    using K=cycle::AudioEventKind;
    assert(cycle::muteBikeAudio(true,false,false,K::play,"NPCHorseWhinny"));
    assert(!cycle::muteBikeAudio(false,false,false,K::play,"NPCHorseWhinny"));
    assert(!cycle::muteBikeAudio(false,true,false,K::play,"NPCHorseMount"));
    assert(!cycle::muteBikeAudio(false,true,true,K::play,"WPNBowZoomIn"));
    assert(!cycle::muteBikeAudio(false,true,true,K::foot,"FootLeft"));
    assert(cycle::muteBikeAudio(false,true,true,K::play,"SoundPlay.NPCHorseDismount"));
    cycle::RideGate gate;
    assert(!gate.sample(true,false,false,true,0)); // paired mount starts
    assert(!gate.poseReady());
    assert(!gate.sample(true,true,true,false,1));
    assert(gate.poseReady()); // render bicycle idle immediately, not vanilla horse idle for 150ms
    assert(gate.sample(true,true,true,false,1.2)); // settled mounted pose
    assert(gate.sample(true,true,true,false,2)); // Shift does not reset seat
    assert(!gate.sample(true,true,true,true,3)); // dismount flag before relation clears
    assert(!gate.poseReady()); // never bridge the exit with a seated bicycle pose
    assert(!gate.sample(true,false,false,false,3.2)); // stale GetMount relation
    assert(!gate.sample(false,false,false,false,4));
    assert(!gate.sample(true,true,true,false,5)); // fresh mount, not automatic control
    assert(gate.sample(true,true,true,false,5.2));
    for(int i=-720;i<=720;++i) {
        float phase=i*static_cast<float>(cycle::pi)/180.f;
        // Arbitrary common heel/saddle offsets cancel, even after a delayed frame.
        float y=80.f+11.9f*std::sin(phase),z=-37.f-11.9f*std::cos(phase);
        auto observed=cycle::crankFromAnkles(y,z,160.f-y,-74.f-z);
        assert(observed && std::abs(std::remainder(*observed-phase,2*cycle::pi))<1e-5);
    }
    assert(!cycle::crankFromAnkles(0,0,0,0));
    assert(!cycle::crankFromAnkles(0,100,0,0));
    assert(!cycle::crankFromAnkles(NAN,0,0,0));
    assert(cycle::isCyclingRiderClip("Animations/Horse_Rider/idle.hkx"));
    assert(cycle::isCyclingRiderClip(R"(horse_rider\walkforward.hkx)"));
    assert(!cycle::isCyclingRiderClip("horse_rider/saddle_offset.hkx"));
    assert(!cycle::isCyclingRiderClip("sharedkillmoves/human&horse/paired_mount.hkx"));
    assert(!cycle::isCyclingRiderClip("horse_rider/mount.hkx"));
    assert(!cycle::isCyclingRiderClip("animations/idle.hkx"));
    // Regression: the original hook dropped output pointers and the fifth ABI argument.
    struct Clip {int value;};struct Context {int value;};struct Output {int value;};
    Clip clip{3};Context context{4};Output child{5},output{0};const Output* children[]{&child};
    auto engine=[&](Clip* actualClip,const Context& actualContext,const Output** actualChildren,
                   Output& actualOutput,float timeOffset) {
        assert(actualClip==&clip && &actualContext==&context);
        assert(actualChildren==children && &actualOutput==&output);
        assert(timeOffset==.375f);
        actualOutput.value=actualClip->value+actualContext.value+actualChildren[0]->value;
    };
    cycle::forwardGenerate(engine,&clip,context,children,output,.375f);
    assert(output.value==12);
    cycle::Motion m;
    m.sample({0,0,0},0,0,false);
    m.sample({0,cycle::radiusUnits*cycle::pi/2,0},0,.2,false);
    assert(std::abs(m.angle+cycle::pi/2)<1e-8);
    assert(std::abs(m.fraction()-.25)<1e-8);
    auto phase=m.angle;
    m.sample({0,cycle::radiusUnits*cycle::pi/2,0},0,.216,false);
    assert(m.angle==phase); // stationary means frozen, regardless of elapsed time
    m.sample({0,1000,0},0,.232,false);
    assert(m.angle==phase); // teleport does not spin wheels
    m.sample({0,1020,0},0,.248,true);
    assert(m.angle==phase); // menu/pause suppresses movement
    m.sample({0,1010,0},0,.264,false);
    assert(m.angle>phase); // reverse travel reverses the crank
    phase=m.angle;
    m.sample({0,1040,0},0,1.5,false);
    assert(m.angle==phase); // long loading/frame gap rebaselines position
    m.reset();m.sample({0,0,0},cycle::pi/2,0,false);
    m.sample({10,0,0},cycle::pi/2,.1,false);
    assert(m.angle<0); // heading rotates the forward axis
    cycle::Motion longRun;
    longRun.sample({0,0,0},0,0,false);
    for(int i=1;i<=12000;++i) {
        const double distance=i*2.8;
        longRun.sample({0,distance,0},0,i/60.0,false);
        const auto expected=std::remainder(-distance/cycle::radiusUnits,2*cycle::pi);
        assert(std::abs(std::remainder(longRun.angle-expected,2*cycle::pi))<1e-8);
    }
    auto stopped=longRun.angle;
    for(int i=12001;i<=12600;++i)longRun.sample({0,12000*2.8,0},0,i/60.0,false);
    assert(longRun.angle==stopped);
    std::cout<<"PASS: parking-anchor guards and six-argument relocation; single wheel instance/retry and actor-scoped audio; full Generate argument forwarding; mount/dismount gate and observed crank phase, rider clip exclusions, distance/radius, phase, stop, reverse, heading, pause and teleport guards\n";
}
