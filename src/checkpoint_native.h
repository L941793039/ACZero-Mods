// Experimental same-phase manual checkpoint. Included after all feature state.
// No disk game saves, cross-process snapshots or arbitrary heap restoration.
#include "checkpoint_journal.h"
#include "checkpoint_autosave.h"
#include "checkpoint_milestone.h"
static checkpoint::MilestoneProbe checkpointMilestone;
static std::atomic<unsigned> checkpointAutoProfile{},checkpointAutoMilestone{};
static volatile LONG checkpointAutoOn;
static checkpoint::AutoSave checkpointAuto;
static unsigned long long checkpointWorldSession{};
static std::atomic<unsigned long long> checkpointAutoSaves{},checkpointAutoAttempts{};
static std::atomic<int> checkpointAutoResult{};
static volatile LONG checkpointRequest;
static std::atomic<int> checkpointResult{}; // 1 saved, 2 loaded, negative = refused
static std::atomic<bool> checkpointReady{};
static std::atomic<unsigned long long> checkpointEpoch{1},checkpointSaves{},checkpointLoads{};
static std::atomic<unsigned> checkpointCleared{};
static std::atomic<unsigned> checkpointUnsupportedWeaponType{},checkpointUnsupportedWeaponMask{};
static std::atomic<unsigned long long> checkpointDeathLoads{},checkpointMissionFrames{};
static std::atomic<unsigned long long> checkpointInvalidations{};
static std::atomic<const char*> checkpointClearReason{"none"};
static std::atomic<unsigned> checkpointSnapshotSpans{};
static std::atomic<unsigned long long> checkpointRadioRestores{};
static const char* checkpointMessage(int code) {
    switch(code){case 1:return "CHECKPOINT SAVED";case 2:return "CHECKPOINT LOADED";
    case -1:return "PAUSE TO USE CHECKPOINT";case -2:return "NO CHECKPOINT";
    case -3:return "CHECKPOINT EXPIRED";case -4:return "MISSION STATE UNSUPPORTED";
    case -5:return "WAIT FOR WEAPONS TO CLEAR";case -6:return "CHECKPOINT LOAD FAILED";
    case -7:return "RESTORE FEATURE SWITCHES";
    case -8:return "WAIT FOR SPECIAL WEAPON TO CLEAR";default:return "CHECKPOINT READY";}
}
static void cpRequire(bool good,const char* message){if(!good)throw std::runtime_error(message);}
static void cpRead(UP at,void* data,size_t n){cpRequire(readable(reinterpret_cast<void*>(at),n),"Unreadable checkpoint range");memcpy(data,reinterpret_cast<void*>(at),n);}
static bool cpWritable(UP at,size_t n){MEMORY_BASIC_INFORMATION m{};return readable(reinterpret_cast<void*>(at),n)&&VirtualQuery(reinterpret_cast<void*>(at),&m,sizeof(m))&&(m.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));}
static void cpWrite(UP at,const void* data,size_t n){cpRequire(cpWritable(at,n),"Unwritable checkpoint range");memcpy(reinterpret_cast<void*>(at),data,n);}
static checkpoint::Bytes cpBytes(UP at,size_t n){checkpoint::Bytes b(n);if(n)cpRead(at,b.data(),n);return b;}
static void cpMatch(const checkpoint::Span& s){cpRequire(cpBytes(s.address,s.bytes.size())==s.bytes,"Checkpoint object identity changed");}
struct CheckpointNative {
    U8* player{};UP root{},mission{},weapon{},manager{},owner{};unsigned long long epoch{};
    U8 plane{},slot{},mode{},missionMode{};LONG dual{},ammo{},reload{};
    std::vector<checkpoint::Span> guards,restore;
    checkpoint::Bytes missionBytes;
    UP failureMenu{};std::vector<checkpoint::Span> deathRestore;
};
static CheckpointNative savedCheckpoint;
static void cpInvalidate(const char* reason){
    bool hadPoint=checkpointReady.exchange(false)||!savedCheckpoint.restore.empty();
    InterlockedExchange(&checkpointRequest,0);
    savedCheckpoint=CheckpointNative{}; // Release buffers and all saved addresses.
    checkpointSnapshotSpans=0;
    checkpointResult=-2;
    if(hadPoint){++checkpointEpoch;++checkpointInvalidations;checkpointClearReason=reason;log(reason);}
}
using GroupReset=void(*)(U8*);
static GroupReset originalGroupReset;
static void checkpointResetHook(U8* group){
    nextReset();
    ++checkpointWorldSession;checkpointAuto.reset();
    // Native group reset is used by mission construction/retry, including when
    // allocation addresses are reused. Be conservative on other reset events.
    ++checkpointEpoch;cpInvalidate("native mission/group reset");
    originalGroupReset(group);
}
static UP cpMission(U8* p,UP& manager){
    UP weapon=field<UP>(p,0x1fc8);cpRequire(weapon>0x6a130,"Invalid weapon owner");manager=weapon-0x6a130;
    cpRequire(readable(reinterpret_cast<void*>(manager),0xa8320)&&field<UP>(reinterpret_cast<void*>(manager),0xa8248)==UP(p),"Wrong mission manager");
    U8* owner=field<U8*>(reinterpret_cast<void*>(manager),0xa8318);
    cpRequire(readable(owner,0x18),"Missing mission owner");UP mission=field<UP>(owner,0x10);
    cpRequire(readable(reinterpret_cast<void*>(mission),0x410)&&field<UP>(reinterpret_cast<void*>(mission),0x2f8)==field<UP>(p,0x2040),"Wrong native mission");
    return mission;
}
static void cpGuard(CheckpointNative& s,UP at,size_t n){s.guards.push_back({at,cpBytes(at,n)});}
static void cpSaveSpan(CheckpointNative& s,UP at,size_t n){if(n){cpRequire(cpWritable(at,n),"Unwritable save range");s.restore.push_back({at,cpBytes(at,n)});}}
#include "checkpoint_model_state.h"
#include "checkpoint_script_state.h"
// Global normal/gun pools and seven SP missile pools. Unsupported active SP
// layouts refuse the operation. Save currently requires no in-flight weapons;
// Load removes later projectiles and their native trail/tracer effect slots.
static unsigned cpWeapons(UP weapon,UP root,UP manager,bool clear,checkpoint::Journal& journal){
    cpRequire(readable(reinterpret_cast<void*>(weapon),0x1b10+0x1db38),"Weapon manager unavailable");
    // 0xcbe0d..0xcbe25 installs the graphics/weapon owner subobject at
    // mission manager +0x240, not the outer mission-manager address.
    UP owner=manager+0x240;
    cpRequire(field<UP>(reinterpret_cast<void*>(weapon),0)==owner&&field<UP>(reinterpret_cast<void*>(weapon),8)==root,"Normal weapon owner mismatch");
    UP gun=weapon+0xca0;
    cpRequire(field<UP>(reinterpret_cast<void*>(gun),0)==owner&&field<UP>(reinterpret_cast<void*>(gun),8)==root,"Gun owner mismatch");
    std::vector<uint16_t> effects;unsigned active=0;
    auto pool=[&](UP maskAddress,UP begin,size_t stride,unsigned count,size_t handleOffset){
        uint32_t mask{};cpRead(maskAddress,&mask,4);
        cpRequire(count==32||!(mask>>count),"Unknown projectile mask bits");
        for(unsigned i=0;i<count;i++)if(mask&(1u<<i)){
            ++active;uint16_t handle{};cpRead(begin+i*stride+handleOffset,&handle,2);
            if(handle!=0xffff)effects.push_back(handle&0x3ff);
        }
        if(clear&&mask){uint32_t zero=0;journal.add(maskAddress,&zero,4,cpRead);}
    };
    pool(weapon+0x6e8,weapon+0x158,0x58,16,0x40);
    pool(gun+0x738,gun+0x38,0x38,32,0x2a);
    UP sp=weapon+0x1b10;
    const size_t offsets[17]={0,0,0,0,0,0,0,0,0,0,0x10348,0x10b48,0x11248,0x12048,0x11948,0x12748,0x12e48};
    for(unsigned type=1;type<=32;type++){
        UP ctrl=field<UP>(reinterpret_cast<void*>(sp),0x1da30+type*8);
        if(!ctrl)continue;
        cpRequire(readable(reinterpret_cast<void*>(ctrl),0x80),"SP controller unavailable");
        if(type>=10&&type<=16){
            cpRequire(ctrl==sp+offsets[type],"SP layout changed");
            pool(ctrl+0x5c,ctrl+0x80,type==10?0x78:0x68,16,0x5c);
        }else if(uint32_t mask=field<uint32_t>(reinterpret_cast<void*>(ctrl),0x5c)){
            // A shared enemy/script weapon can be active here too. Do not
            // guess its layout or silently drop it during checkpoint restore.
            checkpointUnsupportedWeaponType=type;checkpointUnsupportedWeaponMask=mask;checkpointResult=-8;
            char detail[128];snprintf(detail,sizeof(detail),"Unsupported active SP weapon: type=%u mask=0x%08x operation=%s",type,mask,clear?"load":"save");log(detail);
            throw std::runtime_error("Unsupported active SP weapon");
        }
    }
    if(!clear)return active;
    if(active){
        // Same native release algorithm as 0x90300, 0x13f3aa and 0x13c7a3:
        // clear occupied slot bits once and decrement the effect count once.
        UP fx=field<UP>(reinterpret_cast<void*>(owner),0x66ee0);
        auto bytes=cpBytes(fx+0x1bc48,0x44);unsigned removed=0;
        for(unsigned handle:effects){cpRequire(handle<512,"Unknown effect bitmap extent");uint32_t bits;memcpy(&bits,bytes.data()+(handle/32)*4,4);uint32_t bit=1u<<(handle%32);
            if(bits&bit){bits&=~bit;memcpy(bytes.data()+(handle/32)*4,&bits,4);++removed;}}
        int count;memcpy(&count,bytes.data()+0x40,4);cpRequire(count>=0&&count<=1024&&removed<=unsigned(count),"Invalid effect allocation count");
        count-=int(removed);memcpy(bytes.data()+0x40,&count,4);
        journal.add(fx+0x1bc48,bytes.data(),bytes.size(),cpRead);
        int none=-1;journal.add(weapon+0x6f0,&none,4,cpRead);
    }
    return active;
}
static void cpCapture(){
    MemoryReadScope reads;
    CheckpointNative s;s.player=player;s.epoch=checkpointEpoch.load();s.root=field<UP>(player,0x2040);
    s.weapon=field<UP>(player,0x1fc8);s.mission=cpMission(player,s.manager);
    s.plane=player[0x20a3];s.slot=player[0x279a];s.mode=player[0x2098];s.dual=dualOn;s.ammo=ammoOn;s.reload=reloadOn;
    checkpoint::Journal unused;
    if(cpWeapons(s.weapon,s.root,s.manager,false,unused)){checkpointResult=-5;throw std::runtime_error("Save with active weapons unsupported");}
    for(size_t off:{0xfe0,0xff8,0x1fb8,0x1fc0,0x1fc8,0x1880,0x1888})cpGuard(s,UP(player)+off,8);
    cpGuard(s,UP(player)+0xf90,72);cpGuard(s,UP(player)+0x20a1,3);
    cpGuard(s,s.manager+0xa8318,8);UP owner=field<UP>(reinterpret_cast<void*>(s.manager),0xa8318);s.owner=owner;cpGuard(s,owner+0x10,8);
    s.missionMode=field<U8>(reinterpret_cast<void*>(s.manager),0xa8822);
    auto root=cpBytes(s.root,0x38);UP array=field<UP>(reinterpret_cast<void*>(s.root),0);unsigned groups=root[0x34];
    cpRequire(groups>0,"No unit groups");cpGuard(s,s.root,0x28);cpGuard(s,s.root+0x34,1);cpGuard(s,array,groups*8);
    UP shared=field<UP>(reinterpret_cast<void*>(s.root),8);cpGuard(s,shared,0x70);
    cpCaptureScriptState(s,shared);
    s.missionBytes=cpBytes(s.mission,0x410);
    // Only capture ordinary flight, never the replay/failure transition itself.
    cpRequire(s.missionBytes[0x3cc]==3,"Save requires normal mission flight");
    // RadioGameFont owns the mission radio scheduler at mission+308 and
    // manager+a82a8. Failure paths f8204/f8304 set 90c bit 2; enqueue
    // 1a6853 refuses ordinary requests while it remains set. Restore this
    // saved gate only, not playback flags or audio-engine resource handles.
    UP radio=field<UP>(reinterpret_cast<void*>(s.mission),0x308);
    cpGuard(s,s.manager+0xa82a8,8);cpGuard(s,s.mission+0x308,8);
    cpRequire(field<UP>(reinterpret_cast<void*>(s.manager),0xa82a8)==radio,"Radio owner mismatch");
    if(radio){
        cpRequire(readable(reinterpret_cast<void*>(radio),0x90e),"Radio controller unavailable");
        cpGuard(s,radio,8);cpSaveSpan(s,radio+0x90c,2);
    }
    cpGuard(s,owner,8);cpGuard(s,owner+0x1c,8);
    s.failureMenu=field<UP>(reinterpret_cast<void*>(s.manager),0x67150);
    cpRequire(readable(reinterpret_cast<void*>(s.failureMenu),0x264)&&field<UP>(reinterpret_cast<void*>(s.failureMenu),0)==gameAddress(0x2ac4a8),"Failure menu identity unavailable");
    cpGuard(s,s.manager+0x67150,8);cpGuard(s,s.failureMenu,8);
    auto deathSpan=[&](UP at,size_t n){s.deathRestore.push_back({at,cpBytes(at,n)});};
    // Native failure branch in 0xce880, and the observed death contrast. These
    // small fields are separate from ordinary Load, not a whole-player copy.
    deathSpan(owner+0x18,4);deathSpan(s.manager+0xa8790,4);deathSpan(s.manager+0xa8820,1);
    deathSpan(UP(player)+0x2058,4);deathSpan(UP(player)+0x1c20,2);deathSpan(UP(player)+0x208c,2);
    CpModelCapture modelState{s};
    for(unsigned g=0;g<groups;g++){
        UP address=field<UP>(reinterpret_cast<void*>(array),g*8);auto head=cpBytes(address,0x180);
        for(size_t off:{0,0xa8,0xb0,0xb8,0xc0,0x110,0x130,0x140,0x148,0x150})cpGuard(s,address+off,8);
        cpGuard(s,address+0x118,4);unsigned count=field<unsigned>(reinterpret_cast<void*>(address),0x118);
        cpRequire(count<=4096,"Too many unit nodes");UP pool=field<UP>(reinterpret_cast<void*>(address),0x110);
        auto nodes=cpBytes(pool,size_t(count)*0x520);
        for(unsigned n=0;n<count;n++){
            size_t b=n*0x520;for(size_t off:{0,8,0x18,0x180,0x1a0,0x1a8,0x1b8,0x388})cpGuard(s,pool+b+off,8);
            memset(nodes.data()+b+0x3d0,0,16);
            modelState.unit(pool+b);
        }
        if(count)s.restore.push_back({pool,std::move(nodes)});
        // Group+48 is the native movement controller (132954/13299a).
        // Its +94 scalar and +98 speed are group+dc/e0: 1279d4 writes
        // the former; 126ec2 initializes speed and 12832d..12838c uses it
        // to advance translation. Saving position without these leaves a
        // restored, still-rising V2 CTRL frozen at its saved partial height.
        // Pointers at a8..d8 remain guards; never widen across them.
        for(auto span:{std::pair<size_t,size_t>{8,0xa8},{0xdc,0xe4},{0xec,0x108},{0x120,0x130},{0x154,0x160}})cpSaveSpan(s,address+span.first,span.second-span.first);
        UP def=field<UP>(reinterpret_cast<void*>(address),0xa8),flags=field<UP>(reinterpret_cast<void*>(address),0xb0);
        if(def&&flags){cpRequire(readable(reinterpret_cast<void*>(def),8),"Invalid group script definition");UP p=field<UP>(reinterpret_cast<void*>(def),0);
            cpRequire(readable(reinterpret_cast<void*>(p),1),"Invalid group flag count");cpSaveSpan(s,flags,*reinterpret_cast<U8*>(p));}
    }
    cpCaptureHudTimer(s,array,groups);
    cpSaveSpan(s,UP(player)+0x1430,0x438);cpSaveSpan(s,UP(player)+0x2728,0x6a);cpSaveSpan(s,s.root+0x28,0xc);
    // Native standalone f6279..f635f advances +398 from a SEQ condition,
    // using frame rate and script duration. d229b consumes it as a float.
    // It is mutable script interpolation, not controller identity or a pointer.
    for(size_t off:{0x378,0x384,0x38c,0x394,0x398})cpSaveSpan(s,s.mission+off,4);
    // Inline camera/projection numbers change during ordinary flight too.
    // Script/resource ownership starts later, at 2d8, and stays exact below.
    cpSaveSpan(s,s.mission+0xe8,0x188);
    cpSaveSpan(s,UP(player)+0x208e,2);cpSaveSpan(s,UP(player)+0x2099,2);
    for(size_t off:{0x1f10,0x1f28,0x1f40,0x1f58,0x1f70,0x1f88})cpSaveSpan(s,UP(player)+off,2);
    // Save finite reserves and dormant cooldowns, rather than reinitializing and
    // refilling both SP slots. Current feature switches must match on Load.
    cpSaveSpan(s,UP(ammoCache),sizeof(ammoCache));cpSaveSpan(s,UP(cooldownCache),sizeof(cooldownCache));
    cpSaveSpan(s,UP(&slotA),sizeof(slotA));cpSaveSpan(s,UP(&slotB),sizeof(slotB));
    cpSaveSpan(s,UP(&savedGun),sizeof(savedGun));cpSaveSpan(s,UP(&savedMsl),sizeof(savedMsl));cpSaveSpan(s,UP(&savedSp),sizeof(savedSp));
    cpSaveSpan(s,UP(&ammoInitialized),sizeof(ammoInitialized));
    char detail[256];snprintf(detail,sizeof(detail),"Checkpoint model capture: models=%zu submodels=%zu poses=%zu motions=%zu HUD_frames=%d timer_unit=%u/%u spans=%zu",
        modelState.models.size(),modelState.submodels.size(),modelState.poses.size(),modelState.motions.size(),field<int>(player,0x1bd4),player[0x1c2f],player[0x1c30],s.restore.size());log(detail);
    savedCheckpoint=std::move(s);checkpointSnapshotSpans=unsigned(savedCheckpoint.restore.size());checkpointReady=true;++checkpointSaves;checkpointResult=1;
}
static void cpValidateMission(const CheckpointNative& s,bool fromDeath){
    UP manager{};cpRequire(cpMission(s.player,manager)==s.mission&&manager==s.manager,"Mission changed");
    for(auto& guard:s.guards)cpMatch(guard);
    auto mission=cpBytes(s.mission,0x410);
    for(size_t off:{0x378,0x384,0x38c,0x394,0x398})memcpy(mission.data()+off,s.missionBytes.data()+off,4);
    memcpy(mission.data()+0xe8,s.missionBytes.data()+0xe8,0x188);
    if(!fromDeath&&(s.player[0x2058]&2)&&mission[0x3cc]==3&&mission[0x3cd]==3){
        // Native d1910/d191e toggles exactly bit 2 for pause. Automatic
        // captures run unpaused; ordinary Load runs from the pause menu.
        // Normalize only the comparison copy, leaving live pause untouched.
        mission[0x3a8]=U8((mission[0x3a8]&~4u)|(s.missionBytes[0x3a8]&4u));
    }
    if(fromDeath){
        // Exactly the same script definition/phase must still be present. Only
        // the known normal-flight -> failure state (3 -> 6) is accepted.
        UP owner=field<UP>(reinterpret_cast<void*>(manager),0xa8318);
        unsigned savedActions{};memcpy(&savedActions,s.deathRestore.front().bytes.data(),4);
        // 0x12bfda increments owner+18 after an action completes. It is an
        // event counter retained across Retry, not a boolean death flag.
        cpRequire(s.missionBytes[0x3cc]==3&&mission[0x3cc]==6&&mission[0x3cd]==6&&
            field<unsigned>(reinterpret_cast<void*>(owner),0x18)==savedActions+1&&*reinterpret_cast<U8*>(s.failureMenu+8),"Not a supported death menu state");
        unsigned beforeFlags{},afterFlags{};memcpy(&beforeFlags,s.missionBytes.data()+0x3a8,4);memcpy(&afterFlags,mission.data()+0x3a8,4);
        cpRequire((afterFlags&0x20)&&!((afterFlags^beforeFlags)&~0x2cu),"Unexpected mission failure flags");
        memcpy(mission.data()+0x3a8,s.missionBytes.data()+0x3a8,4);
        memcpy(mission.data()+0x3b8,s.missionBytes.data()+0x3b8,8);
        memcpy(mission.data()+0x3cc,s.missionBytes.data()+0x3cc,2);
    }
    cpRequire(mission==s.missionBytes,"Mission phase or script controller changed");
}
    // Service at the outer mission-update boundary, after its cached state and
    // menu decisions are finished. Dead players need not receive player ticks.
    bool restoring=checkpointRequest==3;
    if(restoring)checkpointService(true);
    else if(!checkpointRequest)checkpointAutoService(manager);
    return result;
}
