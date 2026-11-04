#pragma once
#include "Gearbox.h"
#include <atomic>
#include <memory>
#include <string>
#include <cstdio>

namespace cycle::hud {
inline std::atomic_bool queued{};
inline std::atomic<std::shared_ptr<const RideTelemetry>> latest;
inline bool menusBlocked() {
    auto* ui=RE::UI::GetSingleton();
    if(!ui || ui->GameIsPaused() || ui->IsItemMenuOpen() || ui->IsModalMenuOpen())return true;
    for(auto name:{"Console","Dialogue Menu","FavoritesMenu","MapMenu","TweenMenu","Journal Menu",
        "Loading Menu","MessageBoxMenu","Crafting Menu","Book Menu","Main Menu","RaceSex Menu"})
        if(ui->IsMenuOpen(name))return true;
    return false;
}
inline void rect(RE::GFxValue& clip,double x,double y,double w,double h,std::uint32_t color,double alpha) {
    RE::GFxValue fill[]{double(color),alpha};clip.Invoke("beginFill",nullptr,fill,2);
    RE::GFxValue p[]{x,y};clip.Invoke("moveTo",nullptr,p,2);
    for(auto xy:{std::pair{x+w,y},std::pair{x+w,y+h},std::pair{x,y+h},std::pair{x,y}}) {
        RE::GFxValue v[]{xy.first,xy.second};clip.Invoke("lineTo",nullptr,v,2);
    }
    clip.Invoke("endFill");
}
inline bool text(RE::GFxMovieView* movie,RE::GFxValue& clip,const char* name,int depth,
                 double x,double y,double w,double h,int size,std::uint32_t color,const char* value) {
    RE::GFxValue args[]{name,double(depth),x,y,w,h};
    if(!clip.Invoke("createTextField",nullptr,args,6))return false;
    RE::GFxValue field,format;
    if(!clip.GetMember(name,&field) || !field.IsObject())return false;
    movie->CreateObject(&format,"TextFormat");
    if(!format.IsObject())return false;
    format.SetMember("font",RE::GFxValue("$EverywhereFont"));
    format.SetMember("size",RE::GFxValue(double(size)));
    format.SetMember("color",RE::GFxValue(double(color)));
    format.SetMember("align",RE::GFxValue("center"));
    field.Invoke("setNewTextFormat",nullptr,&format,1);
    field.SetMember("selectable",RE::GFxValue(false));
    field.SetMember("embedFonts",RE::GFxValue(true));
    field.SetMember("text",RE::GFxValue(value));return true;
}
inline void field(RE::GFxValue& clip,const char* name,const std::string& value) {
    RE::GFxValue target;if(clip.GetMember(name,&target)&&target.IsObject())target.SetMember("text",RE::GFxValue(value.c_str()));
}
inline bool create(RE::GFxMovieView* movie,RE::GFxValue& root,RE::GFxValue& clip) {
    if(!root.CreateEmptyMovieClip(&clip,"C3CycleRidePanel",21300))return false;
    // Thin pale edging and warm charcoal, matching the restrained vanilla HUD.
    rect(clip,0,0,330,178,0xB5A78C,90);rect(clip,1,1,328,176,0x161719,83);
    rect(clip,14,28,302,1,0x8F8574,70);rect(clip,14,100,302,1,0x8F8574,70);
    bool ok=text(movie,clip,"title",1,14,3,302,24,15,0xD8CCB5,"ВЕЛОСИПЕД");
    ok=text(movie,clip,"rpm",2,12,32,96,37,28,0xECE6D9,"0")&&ok;
    ok=text(movie,clip,"speed",3,116,32,96,37,28,0xECE6D9,"0.0")&&ok;
    ok=text(movie,clip,"gear",4,220,32,96,37,28,0xDFC18C,"2 / 8")&&ok;
    ok=text(movie,clip,"rpmLabel",5,12,72,96,23,12,0xB8B1A5,"ОБ/МИН")&&ok;
    ok=text(movie,clip,"speedLabel",6,116,72,96,23,12,0xB8B1A5,"КМ/Ч")&&ok;
    ok=text(movie,clip,"gearLabel",7,220,72,96,23,12,0xB8B1A5,"ПЕРЕДАЧА")&&ok;
    ok=text(movie,clip,"hint",8,14,103,302,23,13,0xB8B1A5,"Обычная скорость: 18 км/ч")&&ok;
    ok=text(movie,clip,"lampHint",9,38,127,278,23,13,0xB8B1A5,"L — включить фонарь")&&ok;
    ok=text(movie,clip,"gearHint",10,14,151,302,23,13,0xB8B1A5,"1–8 — переключение передачи")&&ok;
    return ok;
}
inline void bulb(RE::GFxValue& clip,bool on) {
    RE::GFxValue icon;
    if(!clip.GetMember("lampIcon",&icon)||!icon.IsDisplayObject())
        if(!clip.CreateEmptyMovieClip(&icon,"lampIcon",11))return;
    icon.Invoke("clear");
    const double color=on?0xF2CD70:0x817D73;
    RE::GFxValue fill[]{color,100.0};icon.Invoke("beginFill",nullptr,fill,2);
    RE::GFxValue start[]{27.0,137.0};icon.Invoke("moveTo",nullptr,start,2);
    for(int i=1;i<=20;++i) {
        double a=i*6.283185307179586/20;
        RE::GFxValue point[]{21+6*std::cos(a),137+6*std::sin(a)};
        icon.Invoke("lineTo",nullptr,point,2);
    }
    icon.Invoke("endFill");
    rect(icon,18,143,6,3,std::uint32_t(color),100);
    rect(icon,19,147,4,1,std::uint32_t(color),100);
    if(on) {
        rect(icon,20,125,2,3,0xF2CD70,100);rect(icon,10,136,3,2,0xF2CD70,100);
        rect(icon,30,136,3,2,0xF2CD70,100);
    }
}
inline void draw() {
    struct Done {~Done(){queued.store(false);}}done;
    auto state=latest.load();auto* ui=RE::UI::GetSingleton();if(!state||!ui)return;
    auto menu=ui->GetMenu<RE::HUDMenu>();if(!menu||!menu->uiMovie)return;
    auto* movie=menu->uiMovie.get();RE::GFxValue root,clip;
    if(!movie->GetVariable(&root,"_root") || !root.IsDisplayObject())return;
    bool exists=root.GetMember("C3CycleRidePanel",&clip)&&clip.IsDisplayObject();
    bool visible=state->visible&&!menusBlocked();
    if(!exists&&!visible)return;
    if(!exists) {
        bool ok=create(movie,root,clip);
        SKSE::log::info("Cycle HUD created: {}",ok);
        if(!ok||!clip.IsDisplayObject())return;
    }
    auto bounds=movie->GetVisibleFrameRect();
    RE::GFxValue::DisplayInfo info;
    info.SetX(bounds.left+24);info.SetY(bounds.bottom-220);info.SetVisible(visible);clip.SetDisplayInfo(info);
    if(!visible)return;
    field(clip,"rpm",std::to_string(int(std::round(state->rpm))));
    char speed[32];std::snprintf(speed,sizeof(speed),"%.1f",state->kmh);field(clip,"speed",speed);
    field(clip,"gear",std::to_string(state->gear)+" / 8");
    field(clip,"hint","Обычная скорость: "+std::to_string(int(gearSpeed(state->gear)))+" км/ч");
    field(clip,"lampHint",state->lampOn?"L — выключить фонарь":"L — включить фонарь");
    bulb(clip,state->lampOn);
}
inline void publish(RideTelemetry value,double now,bool immediate=false) {
    static double last{};static bool wasVisible{};
    bool changed=value.visible!=wasVisible;wasVisible=value.visible;
    latest.store(std::make_shared<const RideTelemetry>(value));
    if(!immediate&&!changed&&now-last<.1)return;
    last=now;
    if(!queued.exchange(true))SKSE::GetTaskInterface()->AddUITask(draw);
}
}
