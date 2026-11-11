#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include "MinHook.h"
#include "pn_settings.h"
#include "gun_tracking.h"

// Standalone ACZero Steam 2.0.0.1. Symbolic legacy RVAs use game_build.h.
// Experimental: feature switches default to OFF. No game files or saves are edited.
using U8 = unsigned char;
using UP = uintptr_t;
#include "memory_access.h"
#include "performance.h"
static UP image;
#include "game_build.h"
static HMODULE self;
static char directory[MAX_PATH], ini[MAX_PATH], logPath[MAX_PATH], statusPath[MAX_PATH];
static volatile LONG dualOn, pnOn, gunOn, ammoOn, reloadOn, switchRequest, stopRequest;
static U8* player;
static unsigned long long frame, switches, guided, assisted, ticks;
static unsigned long long steerCalls, playerSteer, targetSteer, historySteer, gunCalls, playerGun;
static unsigned long long snapshotUs;
static unsigned long long normalSteerCalls, normalPlayerSteer, normalGuided;
static unsigned long long targetSamples, gunWithTargets, gunInRange, gunInCone, snapshotMaxUs;
static float gunNearest=1e30f, gunBestAngle=180.f;
static volatile LONG gunConeMilli=3000, gunMaxDistance=4000;
static std::atomic<float> gunTrackRate{15.f},gunReturnRate{30.f};
static guntrack::State gunTracking;
static U8* gunTrackingPlayer;
static int gunTrackingPlane=-1;
static unsigned long long gunTrackingFrame,gunTrackingUpdates,gunTrackingRejected;
static std::atomic<float> gunDeflectionDegrees{},gunRotationError{},gunLastBulletSpeed{};
static std::atomic<LONG> gunTrackingEligible{},gunHeldState{};
struct GunShotTrace {
    unsigned long long sequence,frame; UP target;
    float distance,rawAngle,leadAngle,leadTime,deflection,rotationError,speed;
    float origin[3],targetPosition[3],targetVelocity[3],bulletVelocity[3];
    int selectedGroup,selectedIndex,reason,applied;
};
static SRWLOCK gunTraceLock=SRWLOCK_INIT;
static GunShotTrace gunTraceRows[256];
static unsigned long long gunTraceSequence,gunTraceFlushed;
static volatile LONG gunTraceOn;
static void flushGunTrace();
static bool dualInitialized, ammoInitialized;
static U8* dualPlayer;
static U8* ammoPlayer;
static int dualPlane=-1, ammoPlane=-1;
static int slotA, slotB, ammoCache[3];
static uint16_t cooldownCache[3][4];
static uint16_t savedGun;
static U8 savedMsl, savedSp;
static unsigned long long weaponCycles;
static unsigned long long weaponBeeps;
static thread_local U8* inputPlayer;
static std::atomic<float> pnGains[pnWeaponCount];
static unsigned long long pnGuidedByWeapon[pnWeaponCount];
static unsigned pnInvalidConfig;
static PnWeapon lastPnWeapon=PnWeapon::Other;
static float lastPnGain;

template<class T> T& field(void* p, size_t offset) { return *reinterpret_cast<T*>(static_cast<U8*>(p) + offset); }
#ifdef AC0_CHECKPOINT_TEST
static void* checkpointTestProc(size_t rva);
template<class T> T proc(size_t rva) { return reinterpret_cast<T>(checkpointTestProc(rva)); }
#else
template<class T> T proc(size_t rva) { return reinterpret_cast<T>(gameAddress(rva)); }
#endif
static bool readable(const void* p, size_t n) {
    return memoryReadable(p,n);
}
static void log(const char* text) {
    FILE* f = fopen(logPath, "a");
    if (f) { fprintf(f, "%s\n", text); fclose(f); }
}
static bool validPlayer(U8* p) {
    if (!readable(p, 0x27a0)) return false;
    int index = p[0x279a];
    return index < 3 && field<UP>(p, 0xfd8) == UP(p) + 0xf90 + index * 24 &&
           p[0x20a1] == 0 && p[0x20a2] == 0 && readable(field<U8*>(p, 0xfe0), 16);
}

struct V { float x, y, z, w; };
static V add(V a,V b) { return {a.x+b.x,a.y+b.y,a.z+b.z,0}; }
static V sub(V a,V b) { return {a.x-b.x,a.y-b.y,a.z-b.z,0}; }
static V mul(V a,float k) { return {a.x*k,a.y*k,a.z*k,0}; }
static float dot(V a,V b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
static V cross(V a,V b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x,0}; }
static float length(V a) { return sqrtf(dot(a,a)); }
static V unit(V a) { float l=length(a); return l>1e-6f?mul(a,1.f/l):V{}; }
static bool finite(V a) { return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z); }

static U8* node(int group, int index) {
    if (!validPlayer(player) || group < 0 || group >= 255 || index < 0 || index >= 255) return nullptr;
    U8* groups = field<U8*>(player, 0x2040);
    if (!readable(groups, 0x38) || group >= groups[0x34]) return nullptr;
    U8** array = field<U8**>(groups, 0);
    if (!readable(array, (group+1)*8)) return nullptr;
    U8* objectGroup = array[group];
    if (!readable(objectGroup, 0x160) || index >= field<int>(objectGroup,0x118)) return nullptr;
    U8* n = field<U8*>(objectGroup,0x110) + size_t(index)*0x520;
    // Native missile collision code at 0x13dd2a tests bit 0. Some live ground
    // objects have 0x01/0x05, so bit 1 incorrectly excludes them.
    if (!readable(n,0x520) || !(n[0x1f0]&1)) return nullptr;
    return n;
}
static void initializeDual(U8* p) {
    dualPlayer=p; dualPlane=p[0x20a3]; slotA=p[0x279a];
    int configured=GetPrivateProfileIntA("DualSP","SecondarySlot",-1,ini);
    slotB=(configured>=0&&configured<3&&configured!=slotA)?configured:(slotA+1)%3;
    for (int i=0;i<3;i++) {
        ammoCache[i]=field<U8*>(p,0xfe0)[0xb+i];
        for(int j=0;j<4;j++) cooldownCache[i][j]=0;
    }
    ammoCache[slotA]=p[0x209a];
    dualInitialized=true;
    log("Dual SP initialized: existing selected slot plus configured secondary (or next) slot.");
}
static int storedSpAmmo(U8* p,int slot) {
    // Infinite ammunition uses this aircraft's native capacity for every slot,
    // including unselected HUD rows. Keep finite reserves separate so disabling
    // the feature can still restore the amounts saved before enabling it.
    return ammoOn?field<U8*>(p,0xfe0)[0xb+slot]:ammoCache[slot];
}
static void switchSp(U8* p, int target) {
    int old=p[0x279a];
    if (target<0 || target>2 || old==target) return;
    U8* selected=field<U8*>(p,0xfd8);
    U8* manager=field<U8*>(p,0x1fc8);
    U8* planeData=field<U8*>(p,0xff8);
    if(!readable(manager,0x20000)||!readable(planeData,8)||!readable(selected,24)) return;
    int type=planeData[4+target];
    if(type<1||type>31) return;
    U8* control=field<U8*>(manager,0x1b10+0x1da30+type*8);
    if(!readable(control,0x78)) return;
    if(!ammoOn) ammoCache[old]=p[0x209a];
    for(int i=0;i<4;i++) cooldownCache[old][i]=field<uint16_t>(p,0x1f40+i*24);
    proc<void(*)(U8*,U8)>(0xc51e0)(p,U8(target));
    selected=field<U8*>(p,0xfd8);
    field<UP>(control,0x68)=field<UP>(selected,0);
    field<int>(p,0xc00)=target;
    proc<void(*)(U8*)>(0x158f80)(p+0x110);
    proc<void(*)(U8*)>(0x10f560)(p);
    // The original sortie initializer counts hardpoints only once. Recount after changing SP.
    U8 mounts=0;
    while(mounts<4 && proc<int(*)(U8*,int)>(0x15e530)(p+0x120,target*4+0x43+mounts)) ++mounts;
    p[0x20b4]=mounts;
    p[0x209d]=0;
    p[0x209a]=U8(storedSpAmmo(p,target));
    for(int i=0;i<4;i++) field<uint16_t>(p,0x1f40+i*24)=cooldownCache[target][i];
    // Clear acquired locks when changing weapon, rather than reusing an incompatible lock.
    proc<void(*)(U8*)>(0xc1030)(p+0x1e38);
    ++switches;
}

static void missileLockApply(U8* p);
static void setWeaponMode(U8* p, bool special) {
    // Match the native weapon-toggle block at 0x104abe..0x104d24, including
    // lock count, range and lock behaviour. Empty slots remain selectable.
    p[0x2098]=special?1:0;
    p[0x1eee]&=~0x40;
    field<uint16_t>(p,0x1eec)=0xffff;
    proc<void(*)(U8*)>(0xc1030)(p+0x1e38);
    U8* spec=field<U8*>(p,0xfd8);
    int count=special?std::min<int>(p[0x209a],spec[0xf]):(p[0x2099]!=0);
    proc<void(*)(U8*,int)>(0xc04e0)(p+0x1e38,count);
    float range=field<float>(reinterpret_cast<void*>(image),gameRva(0x2d2478))*
                field<float>(field<U8*>(p,0x1fb8),0x10);
    if(special) range*=field<float>(spec,8);
    field<float>(p,0x1ec4)=range;
    for(int i=0;i<4;i++) field<float>(p,0x1e44+i*0x20)=range;
    if(special) {
        proc<void(*)(U8*,U8)>(0xc2630)(p+0x1e38,spec[0xc]&1);
        proc<void(*)(U8*,U8)>(0xc2680)(p+0x1e38,(spec[0xc]>>1)&1);
    } else {
        p[0x1ecd]&=~3; p[0x1eee]&=~0xc;
    }
    p[0x20bc]=special?2:1;
    missileLockApply(p);
}
static void cycleWeapon(U8* p) {
    if(!dualInitialized) return;
    if(!p[0x2098]) { switchSp(p,slotA); setWeaponMode(p,true); }
    else if(p[0x279a]==slotA) { switchSp(p,slotB); setWeaponMode(p,true); }
    else setWeaponMode(p,false);
    // Original weapon toggle queues bank 1 / event 6 (SP) or event 7 (MSL).
    // Our input interception skips that native block, so queue it once here.
    uint16_t bank=1, event=p[0x2098]?6:7;
    if(proc<U8(*)(void*,const uint16_t*,const uint16_t*)>(0x1c6150)(nullptr,&bank,&event)) ++weaponBeeps;
    ++weaponCycles;
}
using InputPressed=U8(*)(void*,U8,U8);
static InputPressed originalInput;
static U8 inputHook(void* context,U8 index,U8 action) {
    U8 result=originalInput(context,index,action);
    // Action 2 is the game's bound weapon-cycle action, already edge-triggered.
    // Scope this interception to the player's live update, leaving menus alone.
    if(result&&action==2&&inputPlayer&&dualOn) {
        cycleWeapon(inputPlayer); return 0;
    }
    return result;
}

#include "highg_hud.h"
#include "missile_speed.h"
#include "missile_range.h"
#include "next_target.h"
#include "target_indicator.h"
#include "radar_missiles.h"

using Tick=UP(*)(U8*,U8,void*);
static Tick originalTick;
struct TargetHistory { U8* ptr; V position,velocity; unsigned long long frame; };
static TargetHistory targets[1024];
struct AimTarget { U8* ptr; V position,velocity; };
static AimTarget aimTargets[2048];
static int aimTargetCount;
static void appendAimTarget(int group,int index) {
    U8* object=node(group,index);
    if(!object) return;
    for(int i=0;i<aimTargetCount;i++) if(aimTargets[i].ptr==object) return;
    V pos=field<V>(object,0x258);
    if(!finite(pos)) return;
    auto& h=targets[(UP(object)>>4)%1024];
    V velocity{};
    if(h.ptr==object && h.frame==frame) velocity=h.velocity;
    else if(h.ptr==object && h.frame+1==frame) velocity=sub(pos,h.position);
    h={object,pos,velocity,frame};
    if(aimTargetCount<5) { aimTargets[aimTargetCount++]={object,pos,velocity}; ++targetSamples; }
}
static void sampleTargets() {
    MemoryReadScope memoryScope;
    aimTargetCount=0;
    if(!gunOn) return;
    LARGE_INTEGER start,finish,freq; QueryPerformanceCounter(&start);
    // Only completed weapon locks are eligible, as requested by the user.
    // This avoids scanning the world and respects the player's target selection.
    int count=std::min<int>(player[0x1ec8],4);
    int selectedGroup=player[0x1eec],selectedIndex=player[0x1eed];
    for(int i=0;i<count;i++) {
            U8* lock=player+0x1e38+i*0x20;
            if(!(lock[0x1d]&2)) continue;
            if(lock[0x1b]!=selectedGroup||lock[0x1c]!=selectedIndex) continue;
            appendAimTarget(lock[0x1b],lock[0x1c]);
            break; // exactly the current designated, completed-lock target
    }
    QueryPerformanceCounter(&finish); QueryPerformanceFrequency(&freq);
    snapshotUs=static_cast<unsigned long long>((finish.QuadPart-start.QuadPart)*1000000/freq.QuadPart);
    snapshotMaxUs=std::max(snapshotMaxUs,snapshotUs);
}
static void gunFinishTick(U8* p,U8 input);
static void checkpointService(bool atMissionBoundary=false);
static UP tickHook(U8* p,U8 input,void* context) {
    PerfScope timer(Perf::Player);
    if(!validPlayer(p)) return perfInvoke(Perf::PlayerNative,originalTick,p,input,context);
    player=p; ++frame; ++ticks;
    checkpointService();
    missileLockApply(p);
    sampleTargets();
    if(ammoPlayer!=p||ammoPlane!=p[0x20a3]) {
        // Native player storage can be reused when changing aircraft. Never
        // restore another aircraft's saved ammunition into the new loadout.
        ammoInitialized=false; ammoPlayer=p; ammoPlane=p[0x20a3];
    }
    if(dualOn) {
        if(!dualInitialized||dualPlayer!=p||dualPlane!=p[0x20a3]) initializeDual(p);
        if(InterlockedExchange(&switchRequest,0)) cycleWeapon(p);
    } else { dualInitialized=false; InterlockedExchange(&switchRequest,0); }
    if(ammoOn&&!ammoInitialized) {
        savedGun=field<uint16_t>(p,0x208e); savedMsl=p[0x2099]; savedSp=p[0x209a]; ammoInitialized=true;
        if(dualInitialized) ammoCache[p[0x279a]]=savedSp;
    }
    if(!ammoOn&&ammoInitialized) {
        field<uint16_t>(p,0x208e)=savedGun; p[0x2099]=savedMsl;
        p[0x209a]=dualInitialized?U8(ammoCache[p[0x279a]]):savedSp;
        ammoInitialized=false;
    }
    inputPlayer=p; flightInputIndex=input;
    UP result=perfInvoke(Perf::PlayerNative,originalTick,p,input,context);
    missileLockApply(p);
    if(nextTargetOn)nextRefresh(p);else if(nextPlayer)nextReset();
    gunFinishTick(p,input);
    inputPlayer=nullptr;
    if(ammoOn) {
        auto* data=field<U8*>(p,0xfe0);
        field<uint16_t>(p,0x208e)=10000;
        p[0x2099]=data[0xa]; p[0x209a]=data[0xb+p[0x279a]];
    }
    if(reloadOn) {
        field<uint16_t>(p,0x1f10)=0; field<uint16_t>(p,0x1f28)=0;
        for(int i=0;i<4;i++) field<uint16_t>(p,0x1f40+i*24)=0;
    }
    if(dualOn&&dualInitialized&&!(p[0x2058]&2)) {
        // The engine updates only the currently selected SP. Advance the other SP
        // by the same one-frame countdown so switching cannot pause its reload.
        int active=p[0x279a];
        for(int slot=0;slot<3;slot++) if(slot!=active) {
            for(int mount=0;mount<4;mount++) {
                auto& remaining=cooldownCache[slot][mount];
                if(reloadOn) remaining=0; else if(remaining) --remaining;
            }
        }
    }
    return result;
}

using Steer=void(*)(U8*,const V*,float,float,float);
static Steer originalSteer;
struct MissileHistory { U8* ptr; V relative; U8 group,index; float age; unsigned long long frame; };
static MissileHistory missiles[512];
static bool pnAim(U8* projectile,U8* target,float fps,float age,V& adjusted,PnWeapon weapon,bool frameTime=false) {
    unsigned kind=static_cast<unsigned>(weapon);
    float gain=pnGains[kind].load(std::memory_order_relaxed);
    lastPnWeapon=weapon; lastPnGain=gain;
    V position=field<V>(projectile,0), velocity=field<V>(projectile,0x20);
    V relative=sub(field<V>(target,0x258),position);
    float distance=length(relative), speed=length(velocity);
    auto& h=missiles[(UP(projectile)>>3)%512];
    // Normal MSL +0x38 is accumulated travel, not age. Its update executes once
    // per simulation frame; use the player update serial for its time interval.
    float elapsed=frameTime?float(frame-h.frame):(age-h.age)*fps;
    bool same=h.ptr==projectile && h.group==target[0x234]&&h.index==target[0x235] &&
              elapsed>0.001f && elapsed<30.f && (!frameTime||age>=h.age);
    bool changed=false;
    if(gain>0.f && same && distance>1.f && speed>0.001f && finite(relative)&&finite(velocity)) {
        ++historySteer;
        V relativeVelocity=mul(sub(relative,h.relative),1.f/elapsed);
        float closing=-dot(relativeVelocity,unit(relative));
        if(closing>0.f) {
            V omega=mul(cross(relative,relativeVelocity),1.f/(distance*distance));
            V acceleration=mul(cross(omega,unit(velocity)),gain*closing);
            V direction=unit(add(velocity,acceleration));
            V aim=add(position,mul(direction,std::max(distance,1000.f))); aim.w=position.w;
            if(finite(aim)) { adjusted=aim; ++guided; ++pnGuidedByWeapon[kind]; changed=true; }
        }
    }
    h={projectile,relative,target[0x234],target[0x235],age,frame};
    return changed;
}
extern "C" {
void* normalAimTrampoline=nullptr;
void normalAimBridge();
void normalPnAim(U8* projectile,U8* manager,V* aim) {
    PerfScope timer(Perf::NormalPN);
    MemoryReadScope memoryScope;
    ++normalSteerCalls;
    if(!pnOn||!validPlayer(player)||!readable(projectile,0x58)||!readable(manager,0x6f0)||
       projectile[0x44]!=player[0x20a1]||projectile[0x45]!=player[0x20a2]) return;
    ++normalPlayerSteer;
    U8* target=node(projectile[0x42],projectile[0x43]);
    if(!target) return;
    if(pnAim(projectile,target,float(field<uint32_t>(manager,0x6ec)),field<float>(projectile,0x38),*aim,PnWeapon::MSL,true)) ++normalGuided;
}
}
static void steerHook(U8* projectile,const V* aim,float fps,float maxTurn,float fov) {
    PerfScope timer(Perf::Steer);
    MemoryReadScope memoryScope;
    ++steerCalls;
    if(!pnOn||!validPlayer(player)||!readable(projectile,0x60)||!readable(aim,16) ||
       projectile[0x50]!=player[0x20a1]||projectile[0x51]!=player[0x20a2]) {
        perfInvoke(Perf::SteerNative,originalSteer,projectile,aim,fps,maxTurn,fov); return;
    }
    ++playerSteer;
    U8* target=node(projectile[0x52],projectile[0x53]);
    if(!target) { perfInvoke(Perf::SteerNative,originalSteer,projectile,aim,fps,maxTurn,fov); return; }
    ++targetSteer;
    V adjusted=*aim;
    UP root=UP(field<U8*>(player,0x1fc8))+0x1b10;
    PnWeapon kind=UP(projectile)>=root?spPnWeapon(UP(projectile)-root):PnWeapon::Other;
    pnAim(projectile,target,fps,field<float>(projectile,0x54),adjusted,kind);
    perfInvoke(Perf::SteerNative,originalSteer,projectile,&adjusted,fps,maxTurn,fov);
}

using SpawnGun=UP(*)(U8*,const V*,const V*,float,U8,U8,void*,U8);
static SpawnGun originalGun;
static void gunResetIfNeeded(U8* p) {
    if(gunTrackingPlayer!=p||gunTrackingPlane!=p[0x20a3]) {
        gunTracking.reset();gunTrackingPlayer=p;gunTrackingPlane=p[0x20a3];gunTrackingFrame=frame;
    }
}
static guntrack::Vec asGun(V v){return {v.x,v.y,v.z};}
static void gunPublish() {
    gunDeflectionDegrees=guntrack::angle({0,0,-1},gunTracking.direction)*180.f/guntrack::pi;
}
static void gunFinishTick(U8* p,U8 input) {
    gunResetIfNeeded(p);
    U8* context=field<U8*>(reinterpret_cast<void*>(image),gameRva(0x32c8f0));
    bool held=readable(context,0x100)&&proc<U8(*)(void*,U8,U8)>(0x187a40)(context,input,0xd);
    gunHeldState=held;
    if(!gunOn) {gunTracking.reset();gunTrackingEligible=0;gunTrackingFrame=frame;}
    else if(!held) {
        float fps=float(p[0x20a0]);
        if(fps>=20.f&&fps<=240.f)gunTracking.step({0,0,-1},false,1.f/fps,float(gunConeMilli)*.001f,gunTrackRate.load(),gunReturnRate.load());
        gunTrackingFrame=frame;gunTrackingEligible=0;
    }
    gunPublish();
}
static UP gunHook(U8* control,const V* position,const V* rotation,float speed,U8 group,U8 index,void* data,U8 effect) {
    ++gunCalls;
    if(!gunOn||!validPlayer(player)||inputPlayer!=player||group!=player[0x20a1]||index!=player[0x20a2]||
       !readable(position,16)||!readable(rotation,16)||!finite(*position)||!finite(*rotation)||
       !readable(data,0x48)||!readable(control,0x73c))
        return originalGun(control,position,rotation,speed,group,index,data,effect);
    ++playerGun;
    gunResetIfNeeded(player);
    sampleTargets();
    if(aimTargetCount) ++gunWithTargets;
    float fps=float(control[0x28]);
    if(fps<20.f||fps>240.f)return originalGun(control,position,rotation,speed,group,index,data,effect);
    // Exact native 0x13af14..0x13af3f speed formula, not a changed weapon speed.
    float bulletSpeed=speed+float(field<int32_t>(data,0x44))*.2777778208255768f/fps*5.119999885559082f;
    gunLastBulletSpeed=bulletSpeed;
    GunShotTrace trace{};
    trace.frame=frame;trace.speed=bulletSpeed;trace.selectedGroup=player[0x1eec];trace.selectedIndex=player[0x1eed];
    trace.origin[0]=position->x;trace.origin[1]=position->y;trace.origin[2]=position->z;
    trace.reason=1;
    using Matrix=void(*)(V*,const V*);
    alignas(16) V basis[4]{};
    proc<Matrix>(0x202920)(basis,rotation);
    guntrack::Vec desired{0,0,-1};bool eligible=false;
    if(aimTargetCount&&std::isfinite(bulletSpeed)&&bulletSpeed>0.f) {
        V relative=sub(aimTargets[0].position,*position);
        float distance=length(relative),lead=0.f;
        trace.target=UP(aimTargets[0].ptr);trace.distance=distance;
        memcpy(trace.targetPosition,&aimTargets[0].position,12);memcpy(trace.targetVelocity,&aimTargets[0].velocity,12);
        trace.rawAngle=acosf(std::clamp(dot(unit(relative),mul(basis[2],-1.f)),-1.f,1.f))*180.f/guntrack::pi;
        trace.reason=2;
        gunNearest=std::min(gunNearest,distance);
        if(distance>=1.f&&distance<=float(gunMaxDistance)) {
            ++gunInRange;
            trace.reason=3;
            if(guntrack::intercept(asGun(relative),asGun(aimTargets[0].velocity),bulletSpeed,lead)) {
                V direction=unit(add(relative,mul(aimTargets[0].velocity,lead)));
                desired=guntrack::unit({dot(direction,basis[0]),dot(direction,basis[1]),dot(direction,basis[2])});
                float degrees=guntrack::angle({0,0,-1},desired)*180.f/guntrack::pi;
                gunBestAngle=std::min(gunBestAngle,degrees);
                eligible=degrees<=float(gunConeMilli)*.001f;
                trace.leadAngle=degrees;trace.leadTime=lead;trace.reason=eligible?0:4;
                if(eligible)++gunInCone;
            }
        }
    }
    if(gunTrackingFrame!=frame) {
        float dt=float(std::min<unsigned long long>(frame-gunTrackingFrame,30))/fps;
        gunTrackingEligible=gunTracking.step(desired,eligible,dt,float(gunConeMilli)*.001f,gunTrackRate.load(),gunReturnRate.load());
        gunTrackingFrame=frame;++gunTrackingUpdates;
    }
    gunPublish();
    V adjusted=*rotation;
    auto local=gunTracking.direction;
    V offset{asinf(std::clamp(local.y,-1.f,1.f)),atan2f(-local.x,-local.z),0,0};
    using Compose=void(*)(V*,const V*,const V*);
    proc<Compose>(0x203050)(&adjusted,rotation,&offset);
    adjusted.w=rotation->w;
    alignas(16) V checkBasis[4]{};
    proc<Matrix>(0x202920)(checkBasis,&adjusted);
    V expected=unit(add(add(mul(basis[0],local.x),mul(basis[1],local.y)),mul(basis[2],local.z)));
    float error=acosf(std::clamp(dot(expected,mul(checkBasis[2],-1.f)),-1.f,1.f))*180.f/guntrack::pi;
    gunRotationError=error;
    // Verify conventions in the native transform before allowing the shot correction.
    bool accepted=finite(adjusted)&&std::isfinite(error)&&error<=.15f;
    if(!accepted) {++gunTrackingRejected;adjusted=*rotation;}
    uint32_t before=field<uint32_t>(control,0x738);
    UP result=originalGun(control,position,&adjusted,speed,group,index,data,effect);
    uint32_t added=field<uint32_t>(control,0x738)&~before;
    if(accepted&&gunDeflectionDegrees.load()>.001f&&
       (field<uint32_t>(control,0x738)&~before))++assisted;
    if(gunTraceOn&&added&&TryAcquireSRWLockExclusive(&gunTraceLock)) {
        trace.sequence=++gunTraceSequence;trace.deflection=gunDeflectionDegrees.load();trace.rotationError=error;
        trace.applied=accepted&&trace.deflection>.001f;
        int slot=0;while(slot<32&&!(added&(1u<<slot)))++slot;
        memcpy(trace.bulletVelocity,control+(slot+1)*0x38+0x10,12);
        gunTraceRows[(trace.sequence-1)%256]=trace;
        ReleaseSRWLockExclusive(&gunTraceLock);
    }
    return result;
}

#include "checkpoint_native.h"
#include "checkpoint_menu.h"

static void flushGunTrace() {
    if(!gunTraceOn)return;
    GunShotTrace rows[256];unsigned count=0;
    AcquireSRWLockShared(&gunTraceLock);
    unsigned long long end=gunTraceSequence,start=std::max(gunTraceFlushed+1,end>255?end-255:1);
    for(auto seq=start;seq<=end&&count<256;seq++)rows[count++]=gunTraceRows[(seq-1)%256];
    ReleaseSRWLockShared(&gunTraceLock);
    if(!count)return;
    char path[MAX_PATH];snprintf(path,MAX_PATH,"%s\\ACZeroMods-gun-v26.csv",directory);
    FILE* f=fopen(path,gunTraceFlushed?"a":"w");if(!f)return;
    if(!gunTraceFlushed)fputs("seq,frame,target,group,index,reason,applied,distance,raw_deg,lead_deg,lead_frames,deflection_deg,rotation_error_deg,bullet_speed,origin_x,origin_y,origin_z,target_x,target_y,target_z,target_vx,target_vy,target_vz,bullet_vx,bullet_vy,bullet_vz\n",f);
    for(unsigned i=0;i<count;i++) {
        const auto& r=rows[i];if(r.sequence>3000)continue;
        fprintf(f,"%llu,%llu,%llx,%d,%d,%d,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f",r.sequence,r.frame,static_cast<unsigned long long>(r.target),r.selectedGroup,r.selectedIndex,r.reason,r.applied,r.distance,r.rawAngle,r.leadAngle,r.leadTime,r.deflection,r.rotationError,r.speed);
        for(float v:r.origin)fprintf(f,",%.5f",v);
        for(float v:r.targetPosition)fprintf(f,",%.5f",v);
        for(float v:r.targetVelocity)fprintf(f,",%.5f",v);
        for(float v:r.bulletVelocity)fprintf(f,",%.5f",v);
        fputc('\n',f);
    }
    fclose(f);gunTraceFlushed=end;
}

static void status(const char* state) {
    FILE* f=fopen(statusPath,"w"); if(!f) return;
    fprintf(f,"state=%s\npid=%lu\nplayer=%p\nticks=%llu\nswitches=%llu\nguided=%llu\nassisted=%llu\n",state,GetCurrentProcessId(),player,ticks,switches,guided,assisted);
    fprintf(f,"dual=%ld\npn=%ld\ngun=%ld\nammo=%ld\nreload=%ld\n",dualOn,pnOn,gunOn,ammoOn,reloadOn);
    fprintf(f,"weapon_cycles=%llu\nweapon_beeps=%llu\n",weaponCycles,weaponBeeps);
    fprintf(f,"version=59\ngame_build=2.0.0.1\npn_invalid_config_mask=%u\npn_last_weapon=%s\npn_last_gain=%.4f\n",pnInvalidConfig,pnWeaponKeys[static_cast<unsigned>(lastPnWeapon)],lastPnGain);
    fprintf(f,"extended_clip_calls=%llu\n",extendedClipCalls.load());
    fprintf(f,"radar_sp_added=%llu\nradar_sp_types=%u\ntarget_indicator_onscreen_hidden=%llu\n",radarSpAdded.load(),radarSpTypes.load(),targetIndicatorOnscreenHidden.load());
    fprintf(f,"checkpoint_unsupported_weapon_type=%u\ncheckpoint_unsupported_weapon_mask=%u\n",checkpointUnsupportedWeaponType.load(),checkpointUnsupportedWeaponMask.load());
    fprintf(f,"target_indicator_designation_draws=%llu\n",targetIndicatorDesignationDraws.load());
    fprintf(f,"target_direction_range_checks=%llu\ntarget_direction_real_feet=%.1f\n",directionRangeChecks.load(),directionRealFeet.load());
    fprintf(f,"target_indicator_name=%ld\ntarget_indicator_draws=%llu\ntarget_indicator_missing=%llu\ntarget_indicator_id=%u\ntarget_indicator_position=%.1f,%.1f\n",targetIndicatorNameOn,targetIndicatorDraws.load(),targetIndicatorMissing.load(),targetIndicatorId.load(),targetIndicatorX.load(),targetIndicatorY.load());
    fprintf(f,"next_target=%ld\nnext_preview_id=%u\nnext_candidates=%u\nnext_switches=%llu\nnext_draws=%llu\nextended_target_boxes=%ld\ntarget_box_distance_feet=%.1f\nextended_box_draws=%llu\n",nextTargetOn,nextPreviewId.load(),nextCandidateCount.load(),nextSwitches.load(),nextDraws.load(),extendedTargetBoxesOn,targetBoxFeet.load(),extendedBoxDraws.load());
    fprintf(f,"next_marker_matches=%llu\nnext_label_clipped=%llu\nextended_box_packets=%llu\nextended_box_max_feet=%.1f\n",nextMarkerMatches.load(),nextLabelClipped.load(),extendedBoxPackets.load(),extendedBoxMaxFeet.load());
    fprintf(f,"next_scan_us=%llu\nnext_scan_max_us=%llu\nmarker_us=%llu\nmarker_max_us=%llu\n",nextScanUs.load(),nextScanMaxUs.load(),markerUs.load(),markerMaxUs.load());
    fprintf(f,"next_selection_mode=%u\nnext_nose_degrees=%.1f\nnext_angular_band_degrees=%.1f\nnext_fallback_degrees=%.1f\nnext_label_offset=%.1f,%.1f\n",nextSelectionMode.load(),nextConeDegrees.load(),nextAngularBand.load(),nextFallbackDegrees.load(),nextLabelX.load(),nextLabelY.load());
    fprintf(f,"extended_target_selection=%ld\nnext_native_select_feet=%.1f\nnext_effective_select_feet=%.1f\nnext_native_lock_feet=%.1f\n",extendedTargetSelectionOn,nextNativeSelectFeet.load(),nextEffectiveSelectFeet.load(),nextNativeLockFeet.load());
    fprintf(f,"far_distance_draws=%llu\nfar_distance_feet=%.1f\n",farDistanceDraws.load(),farDistanceFeet.load());
    fprintf(f,"checkpoint_ready=%d\ncheckpoint_result=%d\ncheckpoint_message=%s\ncheckpoint_saves=%llu\ncheckpoint_loads=%llu\ncheckpoint_epoch=%llu\ncheckpoint_cleared_projectiles=%u\n",int(checkpointReady.load()),checkpointResult.load(),checkpointMessage(checkpointResult.load()),checkpointSaves.load(),checkpointLoads.load(),checkpointEpoch.load(),checkpointCleared.load());
    fprintf(f,"checkpoint_menu_inputs=%llu\ncheckpoint_menu_draws=%llu\n",checkpointMenuInputs.load(),checkpointMenuDraws.load());
    fprintf(f,"checkpoint_death_loads=%llu\ncheckpoint_mission_frames=%llu\ncheckpoint_failure_inputs=%llu\ncheckpoint_failure_draws=%llu\n",checkpointDeathLoads.load(),checkpointMissionFrames.load(),checkpointFailureInputs.load(),checkpointFailureDraws.load());
    fprintf(f,"checkpoint_invalidations=%llu\ncheckpoint_clear_reason=%s\ncheckpoint_snapshot_spans=%u\n",checkpointInvalidations.load(),checkpointClearReason.load(),checkpointSnapshotSpans.load());
    fprintf(f,"checkpoint_auto=%ld\ncheckpoint_auto_saves=%llu\ncheckpoint_auto_attempts=%llu\ncheckpoint_auto_result=%d\n",checkpointAutoOn,checkpointAutoSaves.load(),checkpointAutoAttempts.load(),checkpointAutoResult.load());
    fprintf(f,"checkpoint_radio_restores=%llu\n",checkpointRadioRestores.load());
    fprintf(f,"checkpoint_auto_profile=%u\ncheckpoint_auto_milestone=%u\n",checkpointAutoProfile.load(),checkpointAutoMilestone.load());
    fprintf(f,"gun_tracking_updates=%llu\ngun_tracking_rejected=%llu\ngun_tracking_eligible=%ld\ngun_held=%ld\ngun_deflection_deg=%.4f\ngun_rotation_error_deg=%.4f\ngun_bullet_speed=%.4f\ngun_track_deg_per_sec=%.3f\ngun_return_deg_per_sec=%.3f\n",gunTrackingUpdates,gunTrackingRejected,gunTrackingEligible.load(),gunHeldState.load(),gunDeflectionDegrees.load(),gunRotationError.load(),gunLastBulletSpeed.load(),gunTrackRate.load(),gunReturnRate.load());
    fprintf(f,"hud_text_calls=%llu\n",hudTextCalls.load());
    fprintf(f,"highg_native_stall_mode=%ld\nhighg_native_stall_valid=%ld\nhighg_plane_id=%d\nhighg_native_stall_kmh=%.3f\nhighg_native_buffet_kmh=%.3f\nhighg_exit_speed_kmh=%.3f\nhighg_entry_speed_kmh=%.3f\nhighg_speed_cutoffs=%llu\nhighg_engagements=%llu\n",highGNativeStallOn,highGNativeStallValid.load(),highGPlaneId,highGNativeStall.load(),highGNativeBuffet.load(),highGExitSpeed.load(),highGEntrySpeed.load(),highGSpeedCutoffs.load(),highGEngagements.load());
    fprintf(f,"highg_native_onset_kmh=%.3f\nhighg_native_severity=%.4f\n",highGNativeOnset.load(),highGNativeSeverity.load());
    fprintf(f,"missile_speed=%ld\nspeed_invalid_config_mask=%u\nnormal_speed_calls=%llu\nsp_speed_calls=%llu\nlast_native_missile_speed=%.4f\nlast_modified_missile_speed=%.4f\n",missileSpeedOn,speedInvalidConfig,normalSpeedCalls.load(),spSpeedCalls.load(),lastNativeMissileSpeed.load(),lastModifiedMissileSpeed.load());
    missileRangeStatus(f);
    performanceStatus(f);
    for(unsigned i=0;i<pnWeaponCount;i++) fprintf(f,"speed_%s=%.4f\n",pnWeaponKeys[i],missileSpeedGains[i].load());
    fprintf(f,"highg=%ld\nhud_rows=%ld\nhighg_active=%ld\nhighg_blend=%.4f\nhighg_multiplier=%.4f\nhighg_pitch=%.4f\nhighg_accel=%.4f\nhighg_brake=%.4f\nhighg_speed=%.2f\nhighg_calls=%llu\nhighg_positive_frames=%llu\nhighg_negative_frames=%llu\nhud_draws=%llu\nhighg_hint_draws=%llu\n",highGOn,weaponHudOn,highGActive.load(),highGBlend.load(),highGLastMultiplier.load(),highGLastPitch.load(),highGAccel.load(),highGBrake.load(),highGLastSpeed.load(),highGCalls.load(),highGPositiveFrames.load(),highGNegativeFrames.load(),hudDraws.load(),highGHintDraws.load());
    for(unsigned i=0;i<pnWeaponCount;i++) fprintf(f,"pn_%s=%.4f\npn_guided_%s=%llu\n",pnWeaponKeys[i],pnGains[i].load(std::memory_order_relaxed),pnWeaponKeys[i],pnGuidedByWeapon[i]);
    fprintf(f,"normal_steer_calls=%llu\nnormal_player_steer=%llu\nnormal_guided=%llu\ntarget_samples=%llu\ngun_with_targets=%llu\ngun_in_range=%llu\ngun_in_cone=%llu\ngun_nearest=%.1f\ngun_best_angle=%.3f\nsnapshot_max_us=%llu\n",normalSteerCalls,normalPlayerSteer,normalGuided,targetSamples,gunWithTargets,gunInRange,gunInCone,gunNearest,gunBestAngle,snapshotMaxUs);
    fprintf(f,"steer_calls=%llu\nplayer_steer=%llu\ntarget_steer=%llu\nhistory_steer=%llu\ngun_calls=%llu\nplayer_gun=%llu\nsnapshot_us=%llu\naim_targets=%d\n",steerCalls,playerSteer,targetSteer,historySteer,gunCalls,playerGun,snapshotUs,aimTargetCount);
    if(validPlayer(player)) fprintf(f,"weapon_mode=%s\nslot_a=%d\nslot_b=%d\nslot=%u\nsp_id=%u\nsp_ammo=%u\nmsl_ammo=%u\nmounts=%u\n",player[0x2098]?"SP":"MSL",slotA,slotB,player[0x279a],field<U8*>(player,0xfd8)[0xd],player[0x209a],player[0x2099],player[0x20b4]);
    if(dualInitialized) {
        if(validPlayer(player)) fprintf(f,"sp_ammo_display=%d,%d,%d\nsp_ammo_finite_cache=%d,%d,%d\n",storedSpAmmo(player,0),storedSpAmmo(player,1),storedSpAmmo(player,2),ammoCache[0],ammoCache[1],ammoCache[2]);
        for(int s=0;s<3;s++) fprintf(f,"slot_%d_cooldown=%u,%u,%u,%u\n",s,cooldownCache[s][0],cooldownCache[s][1],cooldownCache[s][2],cooldownCache[s][3]);
    }
    fclose(f);
}
static void config() {
    highGConfig();
    performanceOn=GetPrivateProfileIntA("Diagnostics","PerformanceProfile",0,ini)!=0;
    nextTargetOn=GetPrivateProfileIntA("Features","NextTarget",0,ini)!=0;
    targetIndicatorNameOn=GetPrivateProfileIntA("Features","TargetIndicatorName",0,ini)!=0;
    extendedTargetBoxesOn=GetPrivateProfileIntA("Features","ExtendedTargetBoxes",0,ini)!=0;
    extendedTargetSelectionOn=GetPrivateProfileIntA("Features","ExtendedTargetSelection",0,ini)!=0;
    nextSelectFeet=configFloat("NextTarget","SelectDistanceFeet",30000.f,21000.f,60000.f);
    nextConeDegrees=configFloat("NextTarget","ConeDegrees",15.f,1.f,60.f);
    nextAngularBand=configFloat("NextTarget","AngularBandDegrees",3.f,0.f,10.f);
    nextFallbackDegrees=configFloat("NextTarget","FallbackConeDegrees",120.f,60.f,180.f);
    nextFallbackMultiplier=configFloat("NextTarget","FallbackDistanceMultiplier",2.f,1.f,3.f);
    nextFallbackPaddingFeet=configFloat("NextTarget","FallbackPaddingFeet",4000.f,0.f,8000.f);
    nextLabelX=configFloat("NextTarget","LabelOffsetX",-24.f,-64.f,64.f);
    nextLabelY=configFloat("NextTarget","LabelOffsetY",18.f,-32.f,48.f);
    targetBoxFeet=configFloat("NextTarget","TargetBoxDistanceFeet",30000.f,21000.f,60000.f);
    checkpointMenuOn=GetPrivateProfileIntA("HUD","CheckpointMenu",1,ini)!=0;
    checkpointAutoOn=GetPrivateProfileIntA("Checkpoint","AutoSave",0,ini)!=0;
    missileSpeedConfig();
    missileRangeConfig();
    unsigned invalid=0;
    for(unsigned i=0;i<pnWeaponCount;i++) {
        char value[96]; float parsed;
        GetPrivateProfileStringA("ProportionalNavigation",pnWeaponKeys[i],"0",value,sizeof(value),ini);
        if(parsePnGain(value,parsed)) pnGains[i].store(parsed,std::memory_order_relaxed);
        else invalid|=1u<<i; // Retain the last valid value, including on partial editor saves.
    }
    if(invalid!=pnInvalidConfig && invalid) log("Invalid PN coefficient(s): retained last valid values. See pn_invalid_config_mask.");
    pnInvalidConfig=invalid;
    InterlockedExchange(&dualOn,GetPrivateProfileIntA("Features","DualSP",0,ini)!=0);
    InterlockedExchange(&pnOn,GetPrivateProfileIntA("Features","ProportionalNavigation",0,ini)!=0);
    InterlockedExchange(&gunOn,GetPrivateProfileIntA("Features","GunAssist",0,ini)!=0);
    InterlockedExchange(&ammoOn,GetPrivateProfileIntA("Features","InfiniteAmmo",0,ini)!=0);
    InterlockedExchange(&reloadOn,GetPrivateProfileIntA("Features","FastReload",0,ini)!=0);
    InterlockedExchange(&gunConeMilli,std::clamp<LONG>(GetPrivateProfileIntA("GunAssist","ConeMilliDegrees",3000,ini),500,10000));
    InterlockedExchange(&gunMaxDistance,std::clamp<LONG>(GetPrivateProfileIntA("GunAssist","MaxDistance",4000,ini),100,20000));
    gunTrackRate=configFloat("GunAssist","TrackingDegreesPerSecond",15.f,1.f,180.f);
    gunReturnRate=configFloat("GunAssist","ReturnDegreesPerSecond",30.f,1.f,360.f);
    gunTraceOn=GetPrivateProfileIntA("GunAssist","TraceShots",0,ini)!=0;
    if(GetPrivateProfileIntA("Commands","SwitchSP",0,ini)) {
        InterlockedExchange(&switchRequest,1); WritePrivateProfileStringA("Commands","SwitchSP","0",ini);
    }
    if(GetPrivateProfileIntA("Commands","Stop",0,ini)) InterlockedExchange(&stopRequest,1);
    if(GetPrivateProfileIntA("Commands","CheckpointSave",0,ini)) {
        InterlockedExchange(&checkpointRequest,1);WritePrivateProfileStringA("Commands","CheckpointSave","0",ini);
    }
    if(GetPrivateProfileIntA("Commands","CheckpointLoad",0,ini)) {
        InterlockedExchange(&checkpointRequest,2);WritePrivateProfileStringA("Commands","CheckpointLoad","0",ini);
    }
}
static bool check(size_t rva,const U8* data,size_t n) { return memcmp(reinterpret_cast<void*>(gameAddress(rva)),data,n)==0; }
static DWORD WINAPI worker(void*) {
    for(auto& value:pnGains) value.store(pnDefaultGain,std::memory_order_relaxed);
    for(auto& value:missileSpeedGains) value.store(1.f,std::memory_order_relaxed);
    for(auto& value:missileLockGains)value.store(1.f);
    for(auto& value:missileFlightGains)value.store(1.f);
    LARGE_INTEGER perfRate;QueryPerformanceFrequency(&perfRate);perfFrequency=perfRate.QuadPart;
    image=UP(GetModuleHandleA(nullptr));
    GetModuleFileNameA(self,directory,MAX_PATH); char* slash=strrchr(directory,'\\'); if(slash) *slash=0;
    snprintf(ini,MAX_PATH,"%s\\ACZeroMods.ini",directory);
    snprintf(logPath,MAX_PATH,"%s\\ACZeroMods.log",directory);
    snprintf(statusPath,MAX_PATH,"%s\\ACZeroMods.status",directory);
    const U8 tickSig[]={0x48,0x8b,0xc4,0x4c,0x89,0x40,0x18};
    const U8 nextNearestSig[]={0x48,0x8b,0xc4,0x53,0x41,0x54,0x41,0x55};
    const U8 nextCycleSig[]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x20};
    const U8 markerSig[]={0x4c,0x8b,0xdc,0x4d,0x89,0x4b,0x20};
    const U8 rangeSig[]={0xc5,0x7a,0x10,0x05,0x4f,0xb2,0x21,0x00};
    const U8 distanceSig[]={0xc5,0xfa,0x10,0xb4,0x83,0x0c,0x02,0x00,0x00};
    const U8 singleMarkerSig[]={0x48,0x8b,0xc4,0x55,0x56,0x57,0x41,0x54};
    const U8 markerNameSig[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c};
    const U8 markerDistanceSig[]={0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x48,0x81,0xec};
    const U8 radarMissilesSig[]={0x40,0x53,0x48,0x83,0xec,0x20,0x80,0x79,0x08,0x00};
    if(!check(0xb6dc0,markerDistanceSig,sizeof(markerDistanceSig))||!check(0xe9340,radarMissilesSig,sizeof(radarMissilesSig))){status("v42_hud_signature_mismatch");return 1;}
    const U8 directionSig[]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x18};
    const U8 directionPacketSig[]={0xe8,0x4e,0xe1,0x14,0x00};
    const U8 directionMarkersSig[]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x10,0x4c,0x89,0x48,0x20};
    if(!check(0xafd00,directionMarkersSig,sizeof(directionMarkersSig))){status("target_direction_range_signature_mismatch");return 1;}
    if(!check(0xb00c0,directionSig,sizeof(directionSig))||!check(0xb056d,directionPacketSig,sizeof(directionPacketSig))){status("target_indicator_signature_mismatch");return 1;}
    targetDirectionReturn=reinterpret_cast<void*>(gameAddress(0xb0572));
    const U8 visibleSig[]={0x48,0x83,0xec,0x38,0x44,0x8b,0x91,0xf0,0x01,0x00,0x00};
    if(!check(0xb9b01,rangeSig,sizeof(rangeSig))||!check(0xb9bf9,distanceSig,sizeof(distanceSig))||!check(0xb9d80,singleMarkerSig,sizeof(singleMarkerSig))||!check(0xbf770,visibleSig,sizeof(visibleSig))){status("target_range_signature_mismatch");return 1;}
    targetRangeReturn=reinterpret_cast<void*>(gameAddress(0xb9b09));
    targetDistanceReturn=reinterpret_cast<void*>(gameAddress(0xb9c02));
    if(!check(0xb6c10,markerNameSig,sizeof(markerNameSig))){status("target_distance_text_signature_mismatch");return 1;}
    if(!check(0xc15d0,nextNearestSig,sizeof(nextNearestSig))||!check(0xc1c20,nextCycleSig,sizeof(nextCycleSig))||!check(0xb9a60,markerSig,sizeof(markerSig))){status("next_target_signature_mismatch");return 1;}
    const U8 steerSig[]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x08};
    const U8 gunSig[]={0x4c,0x89,0x44,0x24,0x18};
    const U8 inputSig[]={0x48,0x89,0x5c,0x24,0x08};
    const U8 normalSig[]={0xe8,0xd8,0x3f,0x0c,0x00};
    const U8 physicsSig[]={0x48,0x8b,0xc4,0x55,0x56,0x41,0x57};
    const U8 hudSig[]={0x40,0x55,0x41,0x56,0x48,0x8d,0x6c,0x24,0xc8};
    const U8 speedSig[]={0x48,0x8b,0xc4,0xc5,0xfa,0x11,0x58,0x20};
    const U8 xagmUpdateSig[]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x08};
    const U8 airUpdateSig[]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x10};
    if(!check(0x9d9d0,xagmUpdateSig,sizeof(xagmUpdateSig))||!check(0x9dff0,airUpdateSig,sizeof(airUpdateSig))||!check(0x9f1f0,airUpdateSig,sizeof(airUpdateSig))){status("performance_signature_mismatch");return 1;}
    const U8 flightExtra9af70Sig[]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x08};
    if(!check(0x9af70,flightExtra9af70Sig,sizeof(flightExtra9af70Sig))){status("all_flight_signature_mismatch");return 1;}
    const U8 flightExtra9c450Sig[]={0x48,0x8b,0xc4,0x48,0x89,0x70,0x10};
    if(!check(0x9c450,flightExtra9c450Sig,sizeof(flightExtra9c450Sig))){status("all_flight_signature_mismatch");return 1;}
    const U8 flightExtra9d220Sig[]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x10};
    if(!check(0x9d220,flightExtra9d220Sig,sizeof(flightExtra9d220Sig))){status("all_flight_signature_mismatch");return 1;}
    const U8 flightExtra9e950Sig[]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x10};
    if(!check(0x9e950,flightExtra9e950Sig,sizeof(flightExtra9e950Sig))){status("all_flight_signature_mismatch");return 1;}
    const U8 flightExtra9b6f0Sig[]={0xc5,0xfa,0x59,0x0d,0xc8,0x90,0x23,0x00};
    if(!check(0x9b6f0,flightExtra9b6f0Sig,sizeof(flightExtra9b6f0Sig))){status("all_flight_signature_mismatch");return 1;}
    const U8 flightExtra9c9f3Sig[]={0xc5,0xfa,0x59,0x0d,0x8d,0x7d,0x23,0x00};
    if(!check(0x9c9f3,flightExtra9c9f3Sig,sizeof(flightExtra9c9f3Sig))){status("all_flight_signature_mismatch");return 1;}
    const U8 flightExtra9d46aSig[]={0xc5,0xc2,0x59,0x48,0x14};
    if(!check(0x9d46a,flightExtra9d46aSig,sizeof(flightExtra9d46aSig))){status("all_flight_signature_mismatch");return 1;}
    const U8 flightExtra9edc9Sig[]={0xc5,0xba,0x59,0x48,0x14};
    if(!check(0x9edc9,flightExtra9edc9Sig,sizeof(flightExtra9edc9Sig))){status("all_flight_signature_mismatch");return 1;}
    lagmFlightNativeScale=field<float>(reinterpret_cast<void*>(image),gameRva(0x2d2590));
    sodFlightNativeScale=field<float>(reinterpret_cast<void*>(image),gameRva(0x2d25c8));
    lasmFlightRangeReturn=reinterpret_cast<void*>(gameAddress(0x9d46f));
    lagmFlightRangeReturn=reinterpret_cast<void*>(gameAddress(0x9c9fb));
    saamFlightRangeReturn=reinterpret_cast<void*>(gameAddress(0x9edce));
    sodFlightRangeReturn=reinterpret_cast<void*>(gameAddress(0x9b6f8));
    const U8 lockUpdateSig[]={0x40,0x55,0x41,0x56,0x48,0x8d,0x6c,0x24,0x98};
    const U8 normalFlightSig[]={0xc5,0xfa,0x59,0x0d,0x88,0x53,0x19,0x00};
    const U8 xagmFlightSig[]={0xc5,0xca,0x59,0x48,0x14};
    const U8 qaamFlightSig[]={0xc5,0xba,0x59,0x48,0x14};
    const U8 xmaaFlightSig[]={0xc4,0xc1,0x7a,0x59,0xc9};
    if(!check(0xc0740,lockUpdateSig,sizeof(lockUpdateSig))||!check(0x13f2e0,normalFlightSig,sizeof(normalFlightSig))||
       !check(0x9dae2,xagmFlightSig,sizeof(xagmFlightSig))||!check(0x9e46e,xmaaFlightSig,sizeof(xmaaFlightSig))||!check(0x9f55a,qaamFlightSig,sizeof(qaamFlightSig))){status("missile_range_signature_mismatch");return 1;}
    normalFlightRangeReturn=reinterpret_cast<void*>(gameAddress(0x13f2e8));
    xagmFlightRangeReturn=reinterpret_cast<void*>(gameAddress(0x9dae7));
    xmaaFlightRangeReturn=reinterpret_cast<void*>(gameAddress(0x9e473));
    qaamFlightRangeReturn=reinterpret_cast<void*>(gameAddress(0x9f55f));
    missileRangeNativeScale=field<float>(reinterpret_cast<void*>(image),gameRva(0x2d2478));
    const U8 normalSpeedSig[]={0xe8,0x9f,0x3d,0x0c,0x00};
    const U8 groupResetSig[]={0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xec,0x20};
    if(!check(0x12c740,groupResetSig,sizeof(groupResetSig))){status("checkpoint_signature_mismatch");return 1;}
    const U8 menuInputSig[]={0x48,0x89,0x5c,0x24,0x08};
    const U8 menuInitSig[]={0x40,0x53,0x48,0x83,0xec,0x20};
    const U8 missionUpdateSig[]={0x48,0x8b,0xc4,0x55,0x56,0x48,0x8d,0x6c,0x24,0x88};
    if(!check(0xce880,missionUpdateSig,sizeof(missionUpdateSig))){status("checkpoint_death_signature_mismatch");return 1;}
    if(!check(0xeae00,menuInputSig,sizeof(menuInputSig))||!check(0xeba10,menuInputSig,sizeof(menuInputSig))||!check(0xec6c0,menuInitSig,sizeof(menuInitSig))){status("checkpoint_menu_signature_mismatch");return 1;}
    if(!check(0x904d0,speedSig,sizeof(speedSig))||!check(0x13f1bc,normalSpeedSig,sizeof(normalSpeedSig))) {
        log("Refused: missile speed signatures do not match tested build."); status("signature_mismatch"); return 1;
    }
    if(!check(0x1040c0,tickSig,sizeof(tickSig))||!check(0x90a20,steerSig,sizeof(steerSig))||!check(0x13ace0,gunSig,sizeof(gunSig))||!check(0x187c80,inputSig,sizeof(inputSig))||!check(0x13ef83,normalSig,sizeof(normalSig))||!check(0x12aa30,physicsSig,sizeof(physicsSig))||!check(0xdd440,hudSig,sizeof(hudSig))) {
        log("Refused: hook signatures do not match tested build."); status("signature_mismatch"); return 1;
    }
    if(MH_Initialize()!=MH_OK) {status("hook_init_failed");return 1;}
    if(MH_CreateHook(reinterpret_cast<void*>(gameAddress(0x1040c0)),reinterpret_cast<void*>(tickHook),reinterpret_cast<void**>(&originalTick))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0xc15d0)),reinterpret_cast<void*>(nextNearestHook),reinterpret_cast<void**>(&originalSelectNearest))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0xc1c20)),reinterpret_cast<void*>(nextCycleHook),reinterpret_cast<void**>(&originalSelectCycle))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0xb9a60)),reinterpret_cast<void*>(nextMarkersHook),reinterpret_cast<void**>(&originalMarkers))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0xb9b01)),reinterpret_cast<void*>(targetRangeBridge),nullptr)!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0xb9bf9)),reinterpret_cast<void*>(targetDistanceBridge),nullptr)!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0xb9d80)),reinterpret_cast<void*>(nextMarkerHook),reinterpret_cast<void**>(&originalMarker))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0xb6c10)),reinterpret_cast<void*>(nextMarkerNameHook),reinterpret_cast<void**>(&originalMarkerName))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0xb6dc0)),reinterpret_cast<void*>(nextMarkerDistanceHook),reinterpret_cast<void**>(&originalMarkerDistance))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0xe9340)),reinterpret_cast<void*>(radarMissilesHook),reinterpret_cast<void**>(&originalRadarMissiles))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0xb00c0)),reinterpret_cast<void*>(targetDirectionHook),reinterpret_cast<void**>(&originalTargetDirection))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0xafd00)),reinterpret_cast<void*>(targetDirectionMarkersHook),reinterpret_cast<void**>(&originalDirectionMarkers))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0xb056d)),reinterpret_cast<void*>(targetDirectionBridge),nullptr)!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0xbf770)),reinterpret_cast<void*>(nextVisibleHook),reinterpret_cast<void**>(&originalMarkerVisible))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0x90a20)),reinterpret_cast<void*>(steerHook),reinterpret_cast<void**>(&originalSteer))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0x13ace0)),reinterpret_cast<void*>(gunHook),reinterpret_cast<void**>(&originalGun))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0x187c80)),reinterpret_cast<void*>(inputHook),reinterpret_cast<void**>(&originalInput))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0x13ef83)),reinterpret_cast<void*>(normalAimBridge),&normalAimTrampoline)!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0x12aa30)),reinterpret_cast<void*>(physicsHook),reinterpret_cast<void**>(&originalPhysics))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0x904d0)),reinterpret_cast<void*>(missileVelocityHook),reinterpret_cast<void**>(&originalMissileVelocity))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0x9d9d0)),reinterpret_cast<void*>(xagmPoolUpdateHook),reinterpret_cast<void**>(&originalXagmPoolUpdate))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0x9dff0)),reinterpret_cast<void*>(airPoolUpdateHook),reinterpret_cast<void**>(&originalAirPoolUpdate))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0x9f1f0)),reinterpret_cast<void*>(qaamPoolUpdateHook),reinterpret_cast<void**>(&originalQaamPoolUpdate))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0x9d220)),reinterpret_cast<void*>(lasmPoolUpdateHook),reinterpret_cast<void**>(&originalLasmPoolUpdate))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0x9d46a)),reinterpret_cast<void*>(lasmFlightRangeBridge),nullptr)!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0x9c450)),reinterpret_cast<void*>(lagmPoolUpdateHook),reinterpret_cast<void**>(&originalLagmPoolUpdate))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0x9c9f3)),reinterpret_cast<void*>(lagmFlightRangeBridge),nullptr)!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0x9e950)),reinterpret_cast<void*>(saamPoolUpdateHook),reinterpret_cast<void**>(&originalSaamPoolUpdate))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0x9edc9)),reinterpret_cast<void*>(saamFlightRangeBridge),nullptr)!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0x9af70)),reinterpret_cast<void*>(sodPoolUpdateHook),reinterpret_cast<void**>(&originalSodPoolUpdate))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0x9b6f0)),reinterpret_cast<void*>(sodFlightRangeBridge),nullptr)!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0xc0740)),reinterpret_cast<void*>(missileLockUpdateHook),reinterpret_cast<void**>(&originalLockUpdate))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0x13f2e0)),reinterpret_cast<void*>(normalFlightRangeBridge),nullptr)!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0x9dae2)),reinterpret_cast<void*>(xagmFlightRangeBridge),nullptr)!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0x9e46e)),reinterpret_cast<void*>(xmaaFlightRangeBridge),nullptr)!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0x9f55a)),reinterpret_cast<void*>(qaamFlightRangeBridge),nullptr)!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0x13f1bc)),reinterpret_cast<void*>(normalSpeedBridge),&normalSpeedTrampoline)!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0xdd440)),reinterpret_cast<void*>(planeInfoDrawHook),reinterpret_cast<void**>(&originalPlaneInfoDraw))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0x12c740)),reinterpret_cast<void*>(checkpointResetHook),reinterpret_cast<void**>(&originalGroupReset))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0xeae00)),reinterpret_cast<void*>(checkpointPauseInputHook),reinterpret_cast<void**>(&originalPauseMenuInput))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0xeba10)),reinterpret_cast<void*>(checkpointPauseListHook),reinterpret_cast<void**>(&originalPauseMenuList))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0xec6c0)),reinterpret_cast<void*>(checkpointPauseInitHook),reinterpret_cast<void**>(&originalPauseMenuInit))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(gameAddress(0xce880)),reinterpret_cast<void*>(checkpointMissionUpdateHook),reinterpret_cast<void**>(&originalCheckpointMissionUpdate))!=MH_OK) {
        MH_Uninitialize(); status("hook_create_failed");return 1;
    }
    config();
    if(MH_EnableHook(MH_ALL_HOOKS)!=MH_OK) {MH_Uninitialize();status("hook_enable_failed");return 1;}
    log("Hooks installed. All features follow ACZeroMods.ini; defaults OFF."); status("active");
    bool wasDown=false;
    for(unsigned iteration=0;!stopRequest;iteration++) {
        if(iteration%10==0) { config(); status("active");flushGunTrace(); }
        HWND foreground=GetForegroundWindow(); DWORD pid=0; GetWindowThreadProcessId(foreground,&pid);
        bool down=(GetAsyncKeyState(VK_F2)&0x8000)!=0;
        if(pid==GetCurrentProcessId()&&down&&!wasDown&&dualOn) InterlockedExchange(&switchRequest,1);
        wasDown=down; Sleep(20);
    }
    dualOn=pnOn=gunOn=ammoOn=reloadOn=highGOn=weaponHudOn=missileSpeedOn=nextTargetOn=extendedTargetBoxesOn=targetIndicatorNameOn=0;
    // Restore hooks but keep the DLL resident: no thread can return into unmapped code.
    missileLockRangeOn=missileFlightRangeOn=0;
    MH_DisableHook(MH_ALL_HOOKS); missileLockApply(player); status("stopped"); log("Hooks disabled. DLL remains inert until game exits.");
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) { self=instance; DisableThreadLibraryCalls(instance); HANDLE t=CreateThread(nullptr,0,worker,nullptr,0,nullptr); if(t) CloseHandle(t); }
    return TRUE;
}
