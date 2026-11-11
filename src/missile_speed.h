// Speed changes are independent of PN. Normal MSL uses a fresh native scalar
// every frame; SP restores its previous unscaled velocity before native physics.
static volatile LONG missileSpeedOn;
static std::atomic<float> missileSpeedGains[pnWeaponCount];
static unsigned speedInvalidConfig;
static std::atomic<unsigned long long> normalSpeedCalls{}, spSpeedCalls{};
static std::atomic<float> lastNativeMissileSpeed{}, lastModifiedMissileSpeed{};
static void missileSpeedConfig() {
    missileSpeedOn=GetPrivateProfileIntA("Features","MissileSpeed",0,ini)!=0;
    unsigned invalid=0;
    for(unsigned i=0;i<pnWeaponCount;i++) {
        char value[96]; float parsed;
        GetPrivateProfileStringA("MissileSpeed",pnWeaponKeys[i],"1",value,sizeof(value),ini);
        if(parsePnGain(value,parsed)&&parsed>=0.25f&&parsed<=3.f) missileSpeedGains[i]=parsed;
        else invalid|=1u<<i;
    }
    if(invalid!=speedInvalidConfig&&invalid) log("Invalid missile speed multiplier: retained last valid value.");
    speedInvalidConfig=invalid;
}
extern "C" {
void* normalSpeedTrampoline=nullptr;
void normalSpeedBridge();
float normalMissileSpeed(U8* projectile,float nativeSpeed) {
    if(!missileSpeedOn||!validPlayer(player)||!readable(projectile,0x50)||
       projectile[0x44]!=player[0x20a1]||projectile[0x45]!=player[0x20a2]||!std::isfinite(nativeSpeed)) return nativeSpeed;
    float gain=missileSpeedGains[static_cast<unsigned>(PnWeapon::MSL)].load();
    ++normalSpeedCalls;
    lastNativeMissileSpeed=fabsf(nativeSpeed); lastModifiedMissileSpeed=fabsf(nativeSpeed*gain);
    return nativeSpeed*gain;
}
}
struct SpSpeedState { U8* pointer; V output; float factor,age; };
static SpSpeedState spSpeedStates[pnWeaponCount][16];
using MissileVelocity=void(*)(void*,U8*,float,float,float);
static MissileVelocity originalMissileVelocity;
static SpSpeedState* ownedSpSpeed(U8* projectile,PnWeapon& kind) {
    if(!validPlayer(player)||!readable(projectile,0x60)||projectile[0x50]!=player[0x20a1]||projectile[0x51]!=player[0x20a2]) return nullptr;
    UP root=UP(field<U8*>(player,0x1fc8))+0x1b10;
    if(UP(projectile)<root) return nullptr;
    UP offset=UP(projectile)-root;
    kind=spPnWeapon(offset);
    UP begin=0, stride=0x68;
    switch(kind) {
    case PnWeapon::LASM:begin=0x10348;stride=0x78;break;
    case PnWeapon::LAGM:begin=0x10b48;break;
    case PnWeapon::XAGM:begin=0x11248;break;
    case PnWeapon::XAAM:begin=0x11948;break;
    case PnWeapon::XLAA:begin=0x12048;break;
    case PnWeapon::SAAM:begin=0x12748;break;
    case PnWeapon::QAAM:begin=0x12e48;break;
    default:return nullptr; // Exclude bombs, TLS, enemy weapons and unknown layouts.
    }
    UP relative=offset-begin-0x80;
    if(relative%stride||relative/stride>=16) return nullptr;
    return &spSpeedStates[static_cast<unsigned>(kind)][relative/stride];
}
static void missileVelocityHook(void* context,U8* projectile,float drag,float speed,float smoothing) {
    PerfScope timer(Perf::Velocity);
    MemoryReadScope memoryScope;
    PnWeapon kind=PnWeapon::Other;
    SpSpeedState* state=ownedSpSpeed(projectile,kind);
    if(!state) { perfInvoke(Perf::VelocityNative,originalMissileVelocity,context,projectile,drag,speed,smoothing); return; }
    float age=field<float>(projectile,0x54);
    V incoming=field<V>(projectile,0x20);
    // A reused pool slot or externally replaced velocity starts a fresh history.
    bool same=state->pointer==projectile&&age>state->age&&state->factor>=0.25f&&
        length(sub(incoming,state->output))<=0.0001f*std::max(1.f,length(incoming));
    if(same&&state->factor!=1.f) {
        V native=mul(incoming,1.f/state->factor); native.w=incoming.w;
        field<V>(projectile,0x20)=native;
    }
    perfInvoke(Perf::VelocityNative,originalMissileVelocity,context,projectile,drag,speed,smoothing);
    float factor=missileSpeedOn?missileSpeedGains[static_cast<unsigned>(kind)].load():1.f;
    V native=field<V>(projectile,0x20), output=native;
    if(finite(native)&&std::isfinite(age)) {
        output=mul(native,factor); output.w=native.w;
        field<V>(projectile,0x20)=output;
        *state={projectile,output,factor,age};
        if(missileSpeedOn) {
            ++spSpeedCalls; lastNativeMissileSpeed=length(native); lastModifiedMissileSpeed=length(output);
        }
    } else *state={};
}
