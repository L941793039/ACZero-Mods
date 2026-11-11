// Lock acquisition and flight path limits are independent, player-only controls.
// Recompute acquisition limits from immutable native sources, never from the
// previously multiplied controller value. Keep native cones/timing/target flags.
static volatile LONG missileLockRangeOn, missileFlightRangeOn;
static std::atomic<float> missileLockGains[pnWeaponCount], missileFlightGains[pnWeaponCount];
static unsigned lockRangeInvalidConfig, flightRangeInvalidConfig;
static U8* lockRangeTouchedPlayer;
static std::atomic<float> lastNativeLockRange{}, lastModifiedLockRange{};
static std::atomic<unsigned> lastLockRangeKind{};
static std::atomic<unsigned long long> flightRangeCalls[pnWeaponCount];
static std::atomic<float> lastNativeFlightRange[pnWeaponCount], lastModifiedFlightRange[pnWeaponCount];
static PnWeapon lockRangeWeapon(U8* p) {
    if(!p[0x2098]) return PnWeapon::MSL;
    U8* data=field<U8*>(p,0xff8);
    if(!readable(data,7)) return PnWeapon::Other;
    switch(data[4+p[0x279a]]) {
    case 10:return PnWeapon::LASM;case 11:return PnWeapon::LAGM;
    case 12:return PnWeapon::XAGM;case 13:return PnWeapon::XLAA;
    case 14:return PnWeapon::XAAM;case 15:return PnWeapon::SAAM;
    case 16:return PnWeapon::QAAM;default:return PnWeapon::Other;
    }
}
static void missileRangeConfig() {
    missileLockRangeOn=GetPrivateProfileIntA("Features","MissileLockRange",0,ini)!=0;
    missileFlightRangeOn=GetPrivateProfileIntA("Features","MissileFlightRange",0,ini)!=0;
    unsigned invalid[2]{};
    for(unsigned section=0;section<2;section++) for(unsigned i=0;i<pnWeaponCount;i++) {
        char value[96];float parsed;
        GetPrivateProfileStringA(section?"MissileFlightRange":"MissileLockRange",pnWeaponKeys[i],"1",value,sizeof(value),ini);
        if(parsePnGain(value,parsed)&&parsed>=0.25f&&parsed<=4.f)
            (section?missileFlightGains[i]:missileLockGains[i]).store(parsed);
        else invalid[section]|=1u<<i;
    }
    if((invalid[0]&&invalid[0]!=lockRangeInvalidConfig)||(invalid[1]&&invalid[1]!=flightRangeInvalidConfig))
        log("Invalid missile range multiplier: retained last valid value (allowed 0.25..4).");
    lockRangeInvalidConfig=invalid[0];flightRangeInvalidConfig=invalid[1];
}
static void missileLockApply(U8* p) {
    MemoryReadScope memoryScope;
    if(!validPlayer(p)||(!missileLockRangeOn&&p!=lockRangeTouchedPlayer)) return;
    U8* spec=field<U8*>(p,0x1fb8);U8* sp=field<U8*>(p,0xfd8);
    if(!readable(reinterpret_cast<void*>(gameAddress(0x2d2478)),4)||!readable(spec,0x14)||!readable(sp,24)) return;
    float native=field<float>(reinterpret_cast<void*>(image),gameRva(0x2d2478))*field<float>(spec,0x10);
    if(p[0x2098]) native*=field<float>(sp,8);
    if(!std::isfinite(native)||native<=0.f) return;
    PnWeapon kind=lockRangeWeapon(p);unsigned k=static_cast<unsigned>(kind);
    // Unguided bombs, SOD, TLS and unrecognized layouts retain native settings.
    float gain=missileLockRangeOn&&kind!=PnWeapon::Other?missileLockGains[k].load():1.f;
    if(!std::isfinite(gain)||gain<0.25f||gain>4.f)gain=1.f;
    float modified=native*gain;
    field<float>(p,0x1ec4)=modified;
    for(unsigned i=0;i<4;i++)field<float>(p,0x1e44+i*0x20)=modified;
    lastNativeLockRange=native;lastModifiedLockRange=modified;lastLockRangeKind=k;
    lockRangeTouchedPlayer=missileLockRangeOn?p:nullptr;
}
using NativeLockUpdate=void(*)(U8*);
static NativeLockUpdate originalLockUpdate;
static void missileLockUpdateHook(U8* control) {
    PerfScope timer(Perf::Lock);
    MemoryReadScope memoryScope;
    if(validPlayer(player)&&control==player+0x1e38)missileLockApply(player);
    perfInvoke(Perf::LockNative,originalLockUpdate,control);
}
extern "C" {
void* normalFlightRangeReturn=nullptr;
void* xagmFlightRangeReturn=nullptr;
void* xmaaFlightRangeReturn=nullptr;
void* qaamFlightRangeReturn=nullptr;
void* lasmFlightRangeReturn=nullptr;void* lagmFlightRangeReturn=nullptr;void* saamFlightRangeReturn=nullptr;void* sodFlightRangeReturn=nullptr;
float lagmFlightNativeScale=5.12f,sodFlightNativeScale=5.12f;
float missileRangeNativeScale=5.12f;
void normalFlightRangeBridge();void xagmFlightRangeBridge();void xmaaFlightRangeBridge();void qaamFlightRangeBridge();void lasmFlightRangeBridge();void lagmFlightRangeBridge();void saamFlightRangeBridge();void sodFlightRangeBridge();
float missileFlightLimit(U8* projectile,float native,unsigned weapon) {
    PerfScope timer(Perf::FlightLimit);
    MemoryReadScope memoryScope;
    if(!missileFlightRangeOn||!std::isfinite(native)||native<=0.f||weapon>pnWeaponCount)return native;
    // Shared native XMAA/XLAA updater: resolve the actual in-flight pool.
    if(weapon==pnWeaponCount){
        if(missileFlightGains[unsigned(PnWeapon::XAAM)]==1.f&&missileFlightGains[unsigned(PnWeapon::XLAA)]==1.f)return native;
        PnWeapon actual=PnWeapon::Other;
        if(!ownedSpSpeed(projectile,actual)||(actual!=PnWeapon::XAAM&&actual!=PnWeapon::XLAA))return native;
        weapon=unsigned(actual);
    }
    float gain=missileFlightGains[weapon].load();
    // Identity is exact native output and needs no projectile/owner probes.
    if(gain==1.f)return native;
    if(!validPlayer(player))return native;
    auto kind=static_cast<PnWeapon>(weapon);
    if(kind==PnWeapon::MSL) {
        if(!readable(projectile,0x50)||projectile[0x44]!=player[0x20a1]||projectile[0x45]!=player[0x20a2])return native;
        UP manager=UP(field<U8*>(player,0x1fc8)),begin=manager+0x158,at=UP(projectile);
        if(!manager||at<begin||(at-begin)%0x58||(at-begin)/0x58>=16)return native;
    } else if(kind==PnWeapon::SOD) {
        if(!readable(projectile,0x440)||projectile[0x50]!=player[0x20a1]||projectile[0x51]!=player[0x20a2])return native;
        UP manager=UP(field<U8*>(player,0x1fc8)),begin=manager+0x1b10+0xbec8+0x80,at=UP(projectile);
        if(!manager||at<begin||(at-begin)%0x440||(at-begin)/0x440>=16)return native;
    } else if(kind==PnWeapon::XAAM||kind==PnWeapon::XLAA||kind==PnWeapon::XAGM||kind==PnWeapon::QAAM||kind==PnWeapon::SAAM||kind==PnWeapon::LASM||kind==PnWeapon::LAGM) {
        PnWeapon actual=PnWeapon::Other;
        if(!ownedSpSpeed(projectile,actual)||actual!=kind)return native;
    } else return native; // Other flight layouts need their own inspected hooks.
    if(!std::isfinite(gain)||gain<0.25f||gain>4.f)return native;
    ++flightRangeCalls[weapon];lastNativeFlightRange[weapon]=native;lastModifiedFlightRange[weapon]=native*gain;
    return native*gain;
}
}
using NativeSpPoolUpdate=void(*)(U8*,float);
static NativeSpPoolUpdate originalXagmPoolUpdate,originalAirPoolUpdate;
static void xagmPoolUpdateHook(U8* pool,float scale){PerfScope timer(Perf::XagmPool);MemoryReadScope memoryScope;originalXagmPoolUpdate(pool,scale);}
static void airPoolUpdateHook(U8* pool,float scale){PerfScope timer(Perf::AirPool);MemoryReadScope memoryScope;originalAirPoolUpdate(pool,scale);}
using NativeQaamPoolUpdate=void(*)(U8*);
static NativeQaamPoolUpdate originalQaamPoolUpdate;
static void qaamPoolUpdateHook(U8* pool){PerfScope timer(Perf::QaamPool);MemoryReadScope memoryScope;originalQaamPoolUpdate(pool);}
static NativeQaamPoolUpdate originalLasmPoolUpdate,originalLagmPoolUpdate,originalSaamPoolUpdate,originalSodPoolUpdate;
static void lasmPoolUpdateHook(U8* p){PerfScope timer(Perf::LasmPool);MemoryReadScope scope;originalLasmPoolUpdate(p);}
static void lagmPoolUpdateHook(U8* p){PerfScope timer(Perf::LagmPool);MemoryReadScope scope;originalLagmPoolUpdate(p);}
static void saamPoolUpdateHook(U8* p){PerfScope timer(Perf::SaamPool);MemoryReadScope scope;originalSaamPoolUpdate(p);}
static void sodPoolUpdateHook(U8* p){PerfScope timer(Perf::SodPool);MemoryReadScope scope;originalSodPoolUpdate(p);}
static void missileRangeStatus(FILE* f) {
    fprintf(f,"missile_lock_range=%ld\nmissile_flight_range=%ld\nlock_range_invalid_mask=%u\nflight_range_invalid_mask=%u\nlock_range_weapon=%s\nlock_native_feet=%.1f\nlock_modified_feet=%.1f\n",
        missileLockRangeOn,missileFlightRangeOn,lockRangeInvalidConfig,flightRangeInvalidConfig,pnWeaponKeys[lastLockRangeKind.load()],
        lastNativeLockRange.load()*.1953125f*3.2808f,lastModifiedLockRange.load()*.1953125f*3.2808f);
    for(unsigned i=0;i<pnWeaponCount;i++)fprintf(f,"lock_multiplier_%s=%.4f\nflight_multiplier_%s=%.4f\nflight_calls_%s=%llu\nflight_native_feet_%s=%.1f\nflight_modified_feet_%s=%.1f\n",
        pnWeaponKeys[i],missileLockGains[i].load(),pnWeaponKeys[i],missileFlightGains[i].load(),pnWeaponKeys[i],flightRangeCalls[i].load(),
        pnWeaponKeys[i],lastNativeFlightRange[i].load()*.1953125f*3.2808f,pnWeaponKeys[i],lastModifiedFlightRange[i].load()*.1953125f*3.2808f);
}
