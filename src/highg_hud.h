// Native AC0 flight-curve and HUD helpers, verified against the local 1.0.2.1 build.
#include "highg_speed.h"
static volatile LONG highGOn, weaponHudOn, highGHintOn;
static volatile LONG nativeHudStyle;
static std::atomic<int> highGHintX{256}, highGHintY{336}, highGHintFont{4};
static std::atomic<int> highGHintWidth{64}, highGHintHeight{20};
static std::atomic<float> positiveGMultiplier{2.20f}, negativeGMultiplier{1.20f};
static std::atomic<float> highGMinSpeed{550.f}, highGPitchThreshold{0.18f};
static volatile LONG highGNativeStallOn;
static std::atomic<float> highGStallExitRatio{1.f},highGStallReentryRatio{1.1f};
static std::atomic<float> highGNativeStall{},highGExitSpeed{},highGEntrySpeed{},highGNativeBuffet{};
static std::atomic<float> highGNativeOnset{},highGNativeSeverity{};
static std::atomic<LONG> highGNativeStallValid{};
static std::atomic<unsigned long long> highGSpeedCutoffs{},highGEngagements{};
static HighGSpeedGate highGSpeedGate;
static int highGPlaneId=-1;
static std::atomic<float> positiveGDrag{70.f}, negativeGDrag{35.f}, highGRampSeconds{0.18f};
static std::atomic<float> highGBlend{}, highGLastMultiplier{1.f}, highGLastPitch{}, highGLastSpeed{};
static std::atomic<float> highGAccel{}, highGBrake{};
static std::atomic<unsigned long long> highGCalls{}, highGPositiveFrames{}, highGNegativeFrames{}, highGLastUpdate{}, hudDraws{}, highGHintDraws{}, hudTextCalls{};
static std::atomic<LONG> highGActive{};
static thread_local U8 flightInputIndex;
static U8* highGPlayer;

using FlightPhysics=UP(*)(U8*);
static FlightPhysics originalPhysics;
static UP physicsHook(U8* control) {
    if(!player||control!=player+0x1430||!validPlayer(player)) return originalPhysics(control);
    ++highGCalls;
    if(highGPlayer!=player||highGPlaneId!=player[0x20a3]) { highGPlayer=player;highGPlaneId=player[0x20a3];highGBlend=0.f;highGSpeedGate.reset(); }
    float fps=float(control[0x244]);
    if(fps<20.f||fps>240.f) return originalPhysics(control);
    // Native input assignment at 0x10fba0: +0x220 is pitch (actions
    // 0x14 - 0x13), +0x224 is roll (0x15 - 0x16).
    float dt=1.f/fps, speed=field<float>(player,0x1848), pitch=field<float>(control,0x220);
    using Analog=float(*)(void*,U8,U8);
    auto analog=proc<Analog>(0x187ec0);
    float accel=analog(nullptr,flightInputIndex,0x11), brake=analog(nullptr,flightInputIndex,0x12);
    highGAccel=accel; highGBrake=brake; highGLastPitch=pitch; highGLastSpeed=speed;
    float threshold=highGPitchThreshold.load(), minimum=highGMinSpeed.load(), enterSpeed=minimum;
    float stall=0.f,buffet=0.f;
    bool nativeValid=false;
    if(highGNativeStallOn) {
        float altitude=field<float>(control,0x184),reference=field<float>(control,0x218);
        if(std::isfinite(altitude)&&std::isfinite(reference)&&reference>0.f) {
            float density=proc<float(*)(float)>(0x128d90)(altitude);
            nativeValid=ac0StallSpeed(float(field<uint16_t>(control,0x1a)),density,reference,stall);
            nativeValid=ac0StallSpeed(float(field<uint16_t>(control,0x1c)),density,reference,buffet)&&nativeValid&&buffet>=stall;
        }
        if(nativeValid) {
            float onset=ac0StallOnsetSpeed(stall,buffet);
            minimum=onset*highGStallExitRatio.load();
            enterSpeed=onset*std::max(highGStallExitRatio.load(),highGStallReentryRatio.load());
        }
    }
    highGNativeStallValid=nativeValid;highGNativeStall=stall;highGNativeBuffet=buffet;
    highGNativeOnset=nativeValid?ac0StallOnsetSpeed(stall,buffet):0.f;
    float severity=field<float>(control,0x230);
    bool nativeStalling=!std::isfinite(severity)||severity>0.5f;
    highGNativeSeverity=severity;
    highGExitSpeed=minimum;highGEntrySpeed=enterSpeed;
    bool held=inputPlayer==player&&std::isfinite(pitch)&&accel>=0.7f&&brake>=0.7f&&fabsf(pitch)>=threshold&&player[0x1c2b];
    bool wasEngaged=highGSpeedGate.engaged;
    bool requested=highGSpeedGate.update(highGOn,held&&!nativeStalling,speed,minimum,enterSpeed);
    if(wasEngaged&&held&&(speed<=minimum||nativeStalling))++highGSpeedCutoffs;
    if(!wasEngaged&&requested)++highGEngagements;
    float blend=highGBlend.load();
    // Losing the speed gate or disabling the feature immediately restores native curves.
    if(!highGOn||nativeStalling||!std::isfinite(speed)||speed<=minimum) blend=0.f;
    else blend=std::clamp(blend+(requested?1.f:-1.f)*dt/std::max(0.05f,highGRampSeconds.load()),0.f,1.f);
    highGBlend=blend;
    bool positive=pitch>=0.f;
    float strength=positive?positiveGMultiplier.load():negativeGMultiplier.load();
    float multiplier=1.f+(strength-1.f)*blend;
    float curves[8];
    bool usable=true;
    for(int i=0;i<8;i++) { curves[i]=field<float>(control,0x100+i*4); usable&=std::isfinite(curves[i])&&fabsf(curves[i])<100000.f; }
    if(!usable) { multiplier=1.f; blend=0.f; highGBlend=0.f; }
    // Scale this player's native pitch-rate curve only during this physics call.
    // Every call starts from the saved native values; there is no cumulative scaling.
    if(blend>0.f) for(int i=0;i<8;i++) field<float>(control,0x100+i*4)=curves[i]*multiplier;
    UP result=originalPhysics(control);
    if(blend>0.f) {
        for(int i=0;i<8;i++) field<float>(control,0x100+i*4)=curves[i];
        float drag=(positive?positiveGDrag.load():negativeGDrag.load())*blend;
        // Native speed is units/frame; HUD km/h = speed * fps * 0.703125.
        float factor=fps*0.703125f;
        float& velocity=field<float>(control,0x210);
        // No artificial minimum-speed floor: after crossing the threshold the
        // next call stops the boost and the native stall/recovery physics applies.
        if(std::isfinite(velocity)&&velocity>0.f) velocity=std::max(0.f,velocity-drag*dt/factor);
    }
    highGLastMultiplier=multiplier;
    // Original physics may enter stall during this very call. Hide the hint
    // immediately; the next call also bypasses curve scaling and extra drag.
    bool active=requested&&field<float>(control,0x230)<=0.5f&&
        field<float>(control,0x210)*fps*0.703125f>minimum&&blend>0.01f&&multiplier>1.001f;
    highGActive=active;
    highGLastUpdate=GetTickCount64();
    if(active) { if(positive) ++highGPositiveFrames; else ++highGNegativeFrames; }
    return result;
}

using PlaneInfoDraw=UP(*)(U8*,UP);
static PlaneInfoDraw originalPlaneInfoDraw;
static UP hudText(U8* context,UP commands,int x,int y,const char* text,uint32_t color) {
    U8* indicator=field<U8*>(reinterpret_cast<void*>(image),gameRva(0x328418));
    if(!readable(indicator,0x3a0)) return commands;
    // This is a packed native texture word, not a memory pointer. The native
    // text path forwards it into the graphics packet without dereferencing it.
    UP fontTexture=field<UP>(indicator,0x398);
    using Text=UP(*)(U8*,UP,U8,U8,int,int,int,UP,const uint32_t*,const char*);
    ++hudTextCalls;
    return proc<Text>(0xd5ab0)(context,commands,6,4,x,y,2,fontTexture,&color,text);
}
static UP hudCenteredHint(U8* context,UP commands,float x,float y,int font,const char* text,uint32_t color) {
    U8* indicator=field<U8*>(reinterpret_cast<void*>(image),gameRva(0x328418));
    if(!readable(indicator,0x3a0)) return commands;
    using CenteredText=UP(*)(U8*,UP,U8,U8,float,float,int,UP,const uint32_t*,const char*);
    ++hudTextCalls;
    // 0xd5d80 -> 0xbb840 measures the string and native font height and
    // centers both axes. Mode 0 keeps spaces blank instead of drawing dashes.
    return proc<CenteredText>(0xd5d80)(context,commands,U8(font),0,x,y,2,field<UP>(indicator,0x398),&color,text);
}
static const char* weaponLabel(int type) {
    switch(type) {
    case 2:return "UGBL";
    case 9:return "SOD"; case 10:return "LASM"; case 11:return "LAGM"; case 12:return "XAGM";
    case 13:return "XLAA"; case 14:return "XMAA"; case 15:return "SAAM"; case 16:return "QAAM";
    case 20:return "TLS"; default:return nullptr;
    }
}
static UP planeInfoDrawHook(U8* context,UP commands) {
    if(!readable(context,0x150)||!context[8]) return originalPlaneInfoDraw(context,commands);
    bool custom=weaponHudOn&&dualOn&&dualInitialized&&validPlayer(player)&&!context[0x1c];
    if(custom) {
        // Keep the original aircraft/hardpoint drawing and its native position.
        commands=proc<PlaneInfoDraw>(0xddce0)(context,commands);
        int x=field<int>(context,0x10)+field<int>(context,0xf8);
        int y=field<int>(context,0x14)+field<int>(context,0xfc);
        uint32_t color=field<uint32_t>(context,0x18);
        int slots[2]={slotA,slotB};
        int quantities[2]={storedSpAmmo(player,slotA),storedSpAmmo(player,slotB)};
        for(int i=0;i<2;i++) if(!ammoOn&&slots[i]==player[0x279a]) quantities[i]=player[0x209a];
        char fallback[2][12];
        const char* spNames[2];
        for(int i=0;i<2;i++) {
            int type=player[0xf90+slots[i]*24+0xd];
            spNames[i]=weaponLabel(type);
            if(!spNames[i]) { snprintf(fallback[i],sizeof(fallback[i]),"SP%d",type); spNames[i]=fallback[i]; }
        }
        const char* labels[5]={"GUN","MSL",spNames[0],spNames[1],"DMG"};
        int values[5]={field<uint16_t>(player,0x208e),player[0x2099],quantities[0],quantities[1],int(100.f-field<float>(context,0x128))};
        int selected=!player[0x2098]?1:(player[0x279a]==slotA?2:3);
        for(int row=0;row<5;row++) {
            // Native rows use 21-pixel spacing. Five rows use 18 pixels and
            // retain the original DMG baseline, leaving the aircraft untouched.
            int baseline=y+0x3d+row*18;
            if(row==0||row==selected||row==4) {
                using Rect=UP(*)(U8*,UP,int,int,int,int,const uint32_t*);
                // Exact native box width and top/bottom offsets from 0xdd440.
                commands=proc<Rect>(0xd93b0)(context,commands,
                    (x+0xb9+1792)*16,(baseline-3+1824)*16,
                    (x+0xe6+1792)*16,(baseline+13+1824)*16,&color);
            }
            char value[24];
            // Native HUD font renders padding spaces as dashes. Reserve three
            // positions for damage plus the percent sign, matching other rows.
            snprintf(value,sizeof(value),row==4?"%3d%%":"%d",values[row]);
            if(nativeHudStyle) {
                // Original HUD labels are atlas sprites; ordinary text uses a
                // different glyph set. Reuse the native labels and number helper.
                using Sprite=UP(*)(U8*,UP,int,int,const uint32_t*,float,float,int);
                int category=(row==2||row==3)?0x18:0x17;
                int glyph=row==0?0x1f:(row==1?0x20:(row==4?0x21:player[0xf90+slots[row-2]*24+0xd]-1));
                int originalRow=row==3?2:(row==4?3:row);
                float shiftY=float(-9+row*18-originalRow*21);
                commands=proc<Sprite>(0xd5f10)(context,commands,category,glyph,&color,0.f,shiftY,-1);
                if(row<4) {
                    using Number=UP(*)(U8*,UP,int,int,int,const uint32_t*,const U8*);
                    U8 fill=row==0?'0':' ';
                    commands=proc<Number>(0xddb40)(context,commands,x+0xbf,baseline,values[row],&color,&fill);
                } else commands=hudText(context,commands,x+0xbf,baseline,value,color);
            } else {
                commands=hudText(context,commands,x+0x72,baseline,labels[row],color);
                commands=hudText(context,commands,x+0xbf,baseline,value,color);
            }
        }
        ++hudDraws;
    } else commands=originalPlaneInfoDraw(context,commands);
    if(highGHintOn&&highGActive&&GetTickCount64()-highGLastUpdate.load()<100&&!context[0x1c]) {
        uint32_t color=field<uint32_t>(context,0x18);
        using Rect=UP(*)(U8*,UP,int,int,int,int,const uint32_t*);
        // AC0 uses a 512x448 HUD with PS2-style 12.4 fixed screen coordinates.
        int cx=highGHintX.load(), cy=highGHintY.load();
        int halfWidth=highGHintWidth.load()/2, halfHeight=highGHintHeight.load()/2;
        commands=proc<Rect>(0xd93b0)(context,commands,(cx-halfWidth+1792)*16,(cy-halfHeight+1824)*16,(cx+halfWidth+1792)*16,(cy+halfHeight+1824)*16,&color);
        commands=hudCenteredHint(context,commands,float(cx),float(cy),highGHintFont.load(),"HIGH G",color);
        ++highGHintDraws;
    }
    return commands;
}

static float configFloat(const char* section,const char* name,float fallback,float low,float high) {
    char value[64]; char initial[32]; snprintf(initial,sizeof(initial),"%.4f",fallback);
    GetPrivateProfileStringA(section,name,initial,value,sizeof(value),ini);
    char* end; float parsed=strtof(value,&end);
    while(*end==' '||*end=='\t'||*end=='\r'||*end=='\n') ++end;
    return end!=value&&!*end&&std::isfinite(parsed)?std::clamp(parsed,low,high):fallback;
}
static void highGConfig() {
    highGOn=GetPrivateProfileIntA("Features","HighG",0,ini)!=0;
    weaponHudOn=GetPrivateProfileIntA("HUD","DualSPRows",0,ini)!=0;
    nativeHudStyle=GetPrivateProfileIntA("HUD","NativeWeaponStyle",1,ini)!=0;
    highGHintOn=GetPrivateProfileIntA("HUD","HighGIndicator",1,ini)!=0;
    highGHintX=std::clamp(GetPrivateProfileIntA("HUD","HighGCenterX",256,ini),80u,432u);
    highGHintY=std::clamp(GetPrivateProfileIntA("HUD","HighGCenterY",336,ini),40u,408u);
    highGHintWidth=std::clamp(GetPrivateProfileIntA("HUD","HighGBoxWidth",64,ini),40u,160u);
    highGHintHeight=std::clamp(GetPrivateProfileIntA("HUD","HighGBoxHeight",20,ini),16u,64u);
    highGHintFont=GetPrivateProfileIntA("HUD","HighGFont",4,ini)==6?6:4;
    positiveGMultiplier=configFloat("HighG","PositiveMultiplier",2.20f,1.f,3.f);
    negativeGMultiplier=configFloat("HighG","NegativeMultiplier",1.20f,1.f,2.f);
    highGMinSpeed=configFloat("HighG","MinSpeedKmh",550.f,300.f,1800.f);
    highGNativeStallOn=GetPrivateProfileIntA("HighG","UseNativeStallSpeed",1,ini)!=0;
    highGStallExitRatio=configFloat("HighG","StallExitMultiplier",1.f,1.f,2.f);
    highGStallReentryRatio=configFloat("HighG","StallReentryMultiplier",1.1f,1.f,2.5f);
    highGPitchThreshold=configFloat("HighG","PitchThreshold",0.18f,0.05f,0.95f);
    positiveGDrag=configFloat("HighG","PositiveSpeedLossKmhPerSecond",70.f,0.f,250.f);
    negativeGDrag=configFloat("HighG","NegativeSpeedLossKmhPerSecond",35.f,0.f,150.f);
    highGRampSeconds=configFloat("HighG","RampSeconds",0.18f,0.05f,1.f);
}
