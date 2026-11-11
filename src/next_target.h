// Shared, read-only candidate decision for preview and explicit target switching.
// Reference-inspired local cycling; not a claim to reproduce AC7/8's formula.
#include <array>
#include <vector>
namespace nexttarget {
struct Candidate { unsigned id; float cosine,distance; };
struct History {
    std::array<unsigned,65536> stamp{};
    unsigned serial=1;
    void reset(){stamp.fill(0);serial=1;}
    void selected(unsigned id){if(id<65535){if(++serial==0)reset();stamp[id]=serial;}}
};
inline float feetToNative(float feet){return feet/(.1953125f*3.2808f);}
inline float nativeToFeet(float distance){return distance*(.1953125f*3.2808f);}
inline float cosineDegrees(float degrees){return cosf(degrees*3.14159265f/180.f);}
struct Policy {
    float noseDegrees=15.f,angularBandDegrees=3.f,fallbackDegrees=120.f;
    float fallbackMultiplier=2.f,fallbackPaddingFeet=4000.f;
};
inline bool valid(const Candidate& c){
    return c.id<65535&&std::isfinite(c.cosine)&&c.cosine>=-1.0001f&&c.cosine<=1.0001f&&
        std::isfinite(c.distance)&&c.distance>0.f;
}
inline bool noseAvailable(const std::vector<Candidate>& candidates,unsigned current,const Policy& p){
    for(const auto& c:candidates)if(valid(c)&&c.id!=current&&c.cosine>=cosineDegrees(p.noseDegrees))return true;
    return false;
}
inline unsigned choose(const std::vector<Candidate>& candidates,unsigned current,
                       const History& history,const Policy& p=Policy{}) {
    float noseCos=cosineDegrees(p.noseDegrees),best=-2.f,nearest=1e30f;
    for(const auto& c:candidates)if(valid(c)){
        nearest=std::min(nearest,c.distance); // current target anchors local combat distance too
        if(c.id!=current&&c.cosine>=noseCos)best=std::max(best,std::min(c.cosine,1.f));
    }
    bool front=best>=-1.f,wideAvailable=false;
    float directionCutoff=front?std::max(noseCos,cosf(std::min(3.14159265f,
        acosf(best)+p.angularBandDegrees*3.14159265f/180.f))):cosineDegrees(p.fallbackDegrees);
    float distanceLimit=std::max(nearest*p.fallbackMultiplier,nearest+feetToNative(p.fallbackPaddingFeet));
    if(!front)for(const auto& c:candidates)if(valid(c)&&c.id!=current&&c.distance<=distanceLimit&&c.cosine>=directionCutoff){wideAvailable=true;break;}
    unsigned result=65535,oldest=~0u;float bestCos=-2.f,bestDistance=1e30f;
    for(const auto& c:candidates){
        if(!valid(c)||c.id==current)continue;
        // A pointed-at target wins even when farther away. History can only
        // cycle alternatives within a narrow band around that direction.
        // With no nose candidate, widen ANGLE while keeping combat DISTANCE
        // local; do not select a distant aircraft merely because it is forward.
        if(front?(c.cosine<directionCutoff):
            (c.distance>distanceLimit||(wideAvailable&&c.cosine<directionCutoff)))continue;
        unsigned age=history.stamp[c.id];
        bool better=front?(c.cosine>bestCos||(c.cosine==bestCos&&c.distance<bestDistance)):
            (c.distance<bestDistance||(c.distance==bestDistance&&c.cosine>bestCos));
        if(age<oldest||(age==oldest&&(better||(c.cosine==bestCos&&c.distance==bestDistance&&c.id<result)))){
            oldest=age;bestCos=c.cosine;bestDistance=c.distance;result=c.id;
        }
    }
    return result;
}
inline bool projectLabel(const float* m,const float* position,float& x,float& y){
    // The native world-to-screen matrix gives GS pixel coordinates. Native
    // drawing multiplies by 16 *after* projection to encode 12.4 packet fields;
    // its matrix output must not be divided by 16 again.
    float w=m[3]*position[0]+m[7]*position[1]+m[11]*position[2]+m[15];
    if(!std::isfinite(w)||w<=.0001f)return false;
    x=(m[0]*position[0]+m[4]*position[1]+m[8]*position[2]+m[12])/w-1792.f;
    y=(m[1]*position[0]+m[5]*position[1]+m[9]*position[2]+m[13])/w-1824.f;
    return std::isfinite(x)&&std::isfinite(y)&&x>=12&&x<=500&&y>=12&&y<=425;
}
inline bool extendFarClip(float* m,float rangeNative){
    // Perspective z=A*w+B. Preserve the near plane z+w exactly up to a
    // positive scale, and extend only the far plane. No FOV/x/y/w changes.
    if(!std::isfinite(rangeNative)||rangeNative<=0)return false;
    for(unsigned i=0;i<16;i++)if(!std::isfinite(m[i]))return false;
    double norm2=0,zw=0;
    for(unsigned i=0;i<3;i++){norm2+=double(m[4*i+3])*m[4*i+3];zw+=double(m[4*i+2])*m[4*i+3];}
    if(norm2<1e-12)return false; // orthographic/invalid projection
    double a=zw/norm2,b=double(m[14])-a*m[15];
    if(a<=1.0000001||a>=2||b>=0)return false;
    double nearPlane=-b/(a+1),farPlane=-b/(a-1),requested=double(rangeNative)*sqrt(norm2);
    if(nearPlane<=0||farPlane<=nearPlane||requested<=farPlane)return false;
    double ratio=requested*(farPlane-nearPlane)/(farPlane*(requested-nearPlane));
    float z[4];
    for(unsigned i=0;i<4;i++){
        z[i]=float(ratio*m[4*i+2]+(ratio-1)*m[4*i+3]);
        if(!std::isfinite(z[i]))return false;
    }
    for(unsigned i=0;i<4;i++)m[4*i+2]=z[i];
    return true;
}
}

static volatile LONG nextTargetOn,extendedTargetBoxesOn,extendedTargetSelectionOn;
static std::atomic<float> nextConeDegrees{15.f},nextAngularBand{3.f},nextFallbackDegrees{120.f};
static std::atomic<float> nextFallbackMultiplier{2.f},nextFallbackPaddingFeet{4000.f},targetBoxFeet{30000.f};
static std::atomic<float> nextLabelX{-24.f},nextLabelY{18.f};
static std::atomic<float> nextSelectFeet{30000.f},nextNativeSelectFeet{},nextEffectiveSelectFeet{},nextNativeLockFeet{};
static std::atomic<unsigned> nextSelectionMode{}; // 0 none, 1 nose, 2 local fallback
static std::atomic<unsigned> nextPreviewId{65535},nextCandidateCount{};
static std::atomic<unsigned long long> nextSwitches{},nextDraws{},extendedBoxDraws{};
static std::atomic<unsigned long long> nextMarkerMatches{},nextLabelClipped{},extendedBoxPackets{};
static std::atomic<float> extendedBoxMaxFeet{};
static std::atomic<unsigned long long> extendedClipCalls{};
static std::atomic<unsigned long long> nextScanUs{},nextScanMaxUs{},markerUs{},markerMaxUs{};
static unsigned long long nextScanAt;
struct NextTimer {
    LARGE_INTEGER start{},frequency{};std::atomic<unsigned long long>& last;std::atomic<unsigned long long>& maximum;
    NextTimer(std::atomic<unsigned long long>& l,std::atomic<unsigned long long>& m):last(l),maximum(m){QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&start);}
    ~NextTimer(){LARGE_INTEGER end;QueryPerformanceCounter(&end);auto us=static_cast<unsigned long long>((end.QuadPart-start.QuadPart)*1000000/frequency.QuadPart);last=us;auto old=maximum.load();while(us>old&&!maximum.compare_exchange_weak(old,us)){} }
};
static nexttarget::History nextHistory;
static U8* nextPlayer;static U8* nextRoot;static int nextWeapon=-1;
static unsigned nextCurrent=65535;static V nextHeading{};
static std::vector<nexttarget::Candidate> nextCandidates;
static void nextReset(){nextHistory.reset();nextPlayer=nullptr;nextRoot=nullptr;nextWeapon=-1;nextCurrent=65535;nextHeading={};nextPreviewId=65535;nextCandidateCount=0;nextSelectionMode=0;nextCandidates.clear();}
static unsigned targetId(U8* p){return unsigned(p[0x1eec])|(unsigned(p[0x1eed])<<8);}
struct NextScanRanges {
    struct Range {UP begin,end;};Range ranges[64]{};unsigned count=0;
    bool canRead(const void* pointer,size_t size){
        UP at=UP(pointer),end=at+size;if(!at||!size||end<at)return false;
        for(unsigned i=0;i<count;i++)if(at>=ranges[i].begin&&end<=ranges[i].end)return true;
        MEMORY_BASIC_INFORMATION m{};
        if(!VirtualQuery(pointer,&m,sizeof(m))||m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        UP limit=UP(m.BaseAddress)+m.RegionSize;if(end>limit)return false;
        if(count<64)ranges[count++]={UP(m.BaseAddress),limit};return true;
    }
};
static bool nextScan(U8* p){
    NextTimer timer(nextScanUs,nextScanMaxUs);
    // Every validation failure must clear the published preview, not retain
    // an old object ID after target/root/aircraft state becomes invalid.
    nextPreviewId=65535;nextCandidateCount=0;nextSelectionMode=0;
    if(!nextTargetOn||!validPlayer(p)){nextPreviewId=65535;return false;}
    // Reuse VirtualQuery results only within this synchronous scan, never
    // across frames/mission changes. The native heap often spans many nodes.
    NextScanRanges ranges;
    U8* root=field<U8*>(p,0x2040);U8* lock=p+0x1e38;
    if(!ranges.canRead(root,0x38)||field<U8*>(lock,0x98)!=root||field<U8*>(lock,0xa0)!=p){nextPreviewId=65535;return false;}
    int groups=root[0x34];auto array=field<U8**>(root,0);
    if(groups<1||!ranges.canRead(array,size_t(groups)*8)||lock[0xb2]>=groups){nextPreviewId=65535;return false;}
    U8* ownGroup=array[lock[0xb2]];
    if(!ranges.canRead(ownGroup,0x160)||lock[0xb3]>=field<int>(ownGroup,0x118))return false;
    U8* own=field<U8*>(ownGroup,0x110)+size_t(lock[0xb3])*0x520;
    if(!ranges.canRead(own,0x520)||!(own[0x1f0]&1))return false;
    alignas(16) V basis[4]{};proc<void(*)(V*,const V*)>(0x202920)(basis,&field<V>(own,0x268));
    V forward=unit(mul(basis[2],-1.f)),origin=field<V>(own,0x258);
    if(!finite(forward)||length(forward)<.9f||!finite(origin))return false;
    int weapon=(p[0x2098]?int(p[0x279a])+1:0)*256+(lock[0xb6]&0xc);
    if(nextPlayer!=p||nextRoot!=root||nextWeapon!=weapon||dot(forward,nextHeading)<.990268f){
        nextReset();nextPlayer=p;nextRoot=root;nextWeapon=weapon;nextHeading=forward;
    }
    unsigned current=targetId(p);if(current!=nextCurrent){nextHistory.selected(current);nextCurrent=current;}
    int ownTeam=ownGroup[0x15d];if(ownTeam>=8)return false;
    auto relations=reinterpret_cast<U8*>(gameAddress(0x2accc0));
    float nativeRange=field<float>(lock,0xa8);if(!std::isfinite(nativeRange)||nativeRange<=0.f||nativeRange>1e7f)return false;
    // Designation range is separate from the weapon lock controller's +8c.
    // Extend only this private candidate scan, never write either native range.
    U8* sensors=field<U8*>(p+0x1868,0x2f8);
    bool extend=extendedTargetSelectionOn&&extendedTargetBoxesOn&&ranges.canRead(sensors,32);
    float range=extend?std::max(nativeRange,nexttarget::feetToNative(std::min(nextSelectFeet.load(),targetBoxFeet.load()))):nativeRange;
    extend=range>nativeRange;
    nextNativeSelectFeet=nexttarget::nativeToFeet(nativeRange);nextEffectiveSelectFeet=nexttarget::nativeToFeet(range);
    nextNativeLockFeet=nexttarget::nativeToFeet(field<float>(lock,0x8c));
    nextCandidates.clear();
    for(int g=0;g<groups;g++){
        U8* group=array[g];if(!ranges.canRead(group,0x160)||!(group[0x120]&(extend?2:4))||group[0x15d]>=8||!relations[ownTeam*8+group[0x15d]])continue;
        int count=field<int>(group,0x118);U8* nodes=field<U8*>(group,0x110);
        if(count<0||count>255||!ranges.canRead(nodes,size_t(count)*0x520))continue;
        for(int i=0;i<count&&nextCandidates.size()<2048;i++){
            U8* n=nodes+size_t(i)*0x520;
            V delta=sub(field<V>(n,0x258),origin);float distance=length(delta);
            if(!finite(delta)||!std::isfinite(distance)||distance<.01f||distance>range)continue;
            U8* specHolder=field<U8*>(n,0x1a0);
            if(!ranges.canRead(specHolder,8)||!ranges.canRead(field<U8*>(specHolder,0),0x72))continue;
            if(!proc<U8(*)(U8*,U8*)>(0xc10f0)(lock,n))continue;
            // A distant renderable group may not be in the native active
            // selection list yet. Respect native sensors/hidden rules instead
            // of setting its activation bits or manufacturing a weapon lock.
            if(extend&&(distance>nativeRange||!(group[0x120]&4))&&
               !proc<U8(*)(U8*,U8*,float,UP,U8)>(0xbf770)(n,sensors,std::min(distance,32768.f),0,group[0x15d]))continue;
            nextCandidates.push_back({unsigned(g)|(unsigned(i)<<8),dot(mul(delta,1.f/distance),forward),distance});
        }
    }
    nextCandidateCount=unsigned(nextCandidates.size());
    nexttarget::Policy policy{nextConeDegrees.load(),nextAngularBand.load(),nextFallbackDegrees.load(),nextFallbackMultiplier.load(),nextFallbackPaddingFeet.load()};
    nextPreviewId=nexttarget::choose(nextCandidates,current,nextHistory,policy);
    if(nextPreviewId!=65535)nextSelectionMode=nexttarget::noseAvailable(nextCandidates,current,policy)?1:2;
    return true;
}
static bool nextRefresh(U8* p,bool force=false){
    if(!nextTargetOn||!p)return false;
    auto now=GetTickCount64();
    int weapon=(p[0x2098]?int(p[0x279a])+1:0)*256+(p[0x1eee]&0xc);
    bool changed=nextPlayer!=p||nextRoot!=field<U8*>(p,0x2040)||nextWeapon!=weapon||nextCurrent!=targetId(p);
    // Paused frames cannot change geometry. Ordinary flight samples at 10 Hz;
    // an explicit target press always revalidates immediately on this thread.
    if(!force&&!changed&&((p[0x2058]&2)||now-nextScanAt<100))return true;
    nextScanAt=now;return nextScan(p);
}
using NativeSelect=void(*)(U8*);
static NativeSelect originalSelectNearest,originalSelectCycle;
static bool nextSelect(U8* lock){
    // Only handle the player's explicit native target input; native automatic
    // acquisition, AI, menus and camera tracking keep their original paths.
    if(!nextTargetOn||inputPlayer!=player||!validPlayer(player)||lock!=player+0x1e38||!(lock[0xb6]&3))return false;
    if(!nextRefresh(player,true))return false;
    unsigned id=nextPreviewId.load();
    if(id==65535)return true; // no alternative: retain the current selection
    lock[0xb4]=U8(id);lock[0xb5]=U8(id>>8);lock[0x93]=U8(id);lock[0x94]=U8(id>>8);
    lock[0xb6]&=~0x20;
    proc<void(*)(U8*)>(0xc1030)(lock);
    field<uint16_t>(lock,0xb0)=uint16_t(lock[0xb7])*2;
    ++nextSwitches;nextRefresh(player,true);return true;
}
static bool nextWeaponTypeAllowed(U8* lock,U8* n){
    if(!readable(n,0x520))return false;
    U8* holder=field<U8*>(n,0x1a0);if(!readable(holder,8))return false;
    U8* spec=field<U8*>(holder,0);if(!readable(spec,0x72))return false;
    unsigned category=spec[0x71]?spec[0x71]:n[0x42b];
    return category!=3&&!((lock[0xb6]&4)&&category==1)&&!((lock[0xb6]&8)&&category==2);
}
static void nextNearestHook(U8* lock){
    if(nextSelect(lock))return;
    // Native c15d0's last fallback (c1a30..c1b76) deliberately skips the
    // weapon-category predicate: it can designate a crossed-out close tank
    // with XMAA after a weapon switch. Its existing +b6 bit 0x10 bypasses
    // exactly that fallback, including retaining an incompatible old target.
    // Use it only during this call and only for a player restricted SP with
    // no compatible current designation. Compatible auto tracking stays native.
    bool scoped=nextTargetOn&&validPlayer(player)&&lock==player+0x1e38&&
        player[0x2098]&&(lock[0xb6]&0xc);
    if(scoped){
        unsigned id=targetId(player);U8* current=node(id&255,id>>8);
        scoped=!current||!nextWeaponTypeAllowed(lock,current);
    }
    U8 previous=scoped?lock[0xb6]:0;
    if(scoped)lock[0xb6]|=0x10;
    originalSelectNearest(lock);
    if(scoped){
        lock[0xb6]=U8((lock[0xb6]&~0x10)|(previous&0x10));
        if(targetId(player)==65535)lock[0xb6]&=~0x20;
    }
}
static void nextCycleHook(U8* lock){if(!nextSelect(lock))originalSelectCycle(lock);}

using NativeMarkers=UP(*)(U8*,UP,U8*,U8*);
static NativeMarkers originalMarkers;
using NativeMarker=UP(*)(U8*,UP,U8*,U8*,U8*,U8*,float,U8);
static NativeMarker originalMarker;
using NativeVisible=U8(*)(U8*,U8*,float,UP,U8);
static NativeVisible originalMarkerVisible;
using NativeMarkerName=UP(*)(U8*,UP,U8*,U8*,U8,const uint32_t*);
static NativeMarkerName originalMarkerName;
using NativeMarkerDistance=UP(*)(U8*,UP,U8*,float,const uint32_t*);
static NativeMarkerDistance originalMarkerDistance;
struct SelectedDistanceDraw {U8* owner{};U8* node{};float distance{};bool drawn{};};
static thread_local SelectedDistanceDraw selectedDistanceDraw;
static std::atomic<unsigned long long> farDistanceDraws{};
static std::atomic<float> farDistanceFeet{};
static UP nextMarkerDistanceHook(U8* owner,UP commands,U8* rectangle,float distance,const uint32_t* color){
    auto& scope=selectedDistanceDraw;
    if(scope.owner==owner&&scope.node){
        // b9d80 has several name/distance branches (active/forced/scripted
        // groups). Emit once across all of them, always from real geometry.
        if(scope.drawn)return commands;
        scope.drawn=true;distance=scope.distance;
        ++farDistanceDraws;farDistanceFeet=nexttarget::nativeToFeet(distance);
    }
    return originalMarkerDistance(owner,commands,rectangle,distance,color);
}
static UP nextMarkerNameHook(U8* owner,UP commands,U8* rectangle,U8* n,U8 team,const uint32_t* color){
    UP result=originalMarkerName(owner,commands,rectangle,n,team,color);
    auto& scope=selectedDistanceDraw;
    if(scope.owner==owner&&scope.node==n&&!scope.drawn){
        // Add even when the native group/path skips its distance call. A later
        // native distance call is deduplicated inside this marker's scope.
        result=nextMarkerDistanceHook(owner,result,rectangle,scope.distance,color);
    }
    return result;
}
static thread_local bool extendedMarkerScope;
// Only while the native direction-indicator traversal processes our currently
// selected target; its cached distance can be stale for renderable far groups.
static thread_local U8* directionVisibilityNode;
static thread_local float directionVisibilityDistance;
static std::atomic<unsigned long long> directionRangeChecks{};
static std::atomic<float> directionRealFeet{};
extern "C" {float targetRangeForDraw=32768.f;void* targetRangeReturn;void targetRangeBridge();}
extern "C" {
    alignas(16) float targetDrawOrigin[4]{};
    uint32_t targetDrawDistanceOn{};
    void* targetDistanceReturn;void targetDistanceBridge();
}
static UP nextDrawLabel(U8* owner,UP commands,U8* render,U8* n){
    if(!nextTargetOn||nextPreviewId.load()!=(unsigned(n[0x234])|(unsigned(n[0x235])<<8)))return commands;
    ++nextMarkerMatches;
    // The native marker uses this world-to-GS matrix. Project independently
    // without advancing target state or invoking the native lock updater.
    float px,py;
    if(!nexttarget::projectLabel(reinterpret_cast<float*>(render+0x110),reinterpret_cast<float*>(n+0x258),px,py)){
        ++nextLabelClipped;return commands;
    }
    U8* indicator=field<U8*>(owner,0x300);if(!readable(indicator,0x3a0))return commands;
    // Native unselected hostile-marker palette entry (0xe). +0x1fc is the
    // red warning palette, not the normal HUD green.
    U8 dummy[32]{};uint32_t color=field<uint32_t>(owner,0x1e8);
    using Text=UP(*)(U8*,UP,U8,U8,float,float,int,UP,const uint32_t*,const char*);
    ++nextDraws;
    // Native text measurement centers both axes. Offset that center down-left
    // of the marker, rather than assuming a fixed width for four glyphs.
    float textX=std::clamp(px+nextLabelX.load(),16.f,496.f),textY=std::clamp(py+nextLabelY.load(),8.f,440.f);
    return proc<Text>(0xd5d80)(dummy,commands,4,0,textX,textY,2,field<UP>(indicator,0x398),&color,"NEXT");
}
static UP nextMarkersHook(U8* owner,UP commands,U8* render,U8* root){
    PerfScope perfTimer(Perf::Markers);
    MemoryReadScope memoryScope;
    if((!nextTargetOn&&!extendedTargetBoxesOn)||owner!=player+0x1868||!validPlayer(player)||root!=field<U8*>(player,0x2040))return perfInvoke(Perf::MarkerNative,originalMarkers,owner,commands,render,root);
    NextTimer timer(markerUs,markerMaxUs);
    bool oldScope=extendedMarkerScope;float oldRange=targetRangeForDraw;
    uint32_t oldDistanceOn=targetDrawDistanceOn;float oldOrigin[4];memcpy(oldOrigin,targetDrawOrigin,sizeof(oldOrigin));
    extendedMarkerScope=extendedTargetBoxesOn!=0;
    targetRangeForDraw=extendedMarkerScope?std::max(32768.f,nexttarget::feetToNative(targetBoxFeet.load())):32768.f;
    targetDrawDistanceOn=0;
    if(extendedMarkerScope){
        U8* own=node(player[0x20a1],player[0x20a2]);
        if(own&&finite(field<V>(own,0x258))){
            memcpy(targetDrawOrigin,own+0x258,sizeof(targetDrawOrigin));targetDrawDistanceOn=1;
        }
    }
    // Run the original traversal, native visibility checks, fades and draw calls.
    UP result=perfInvoke(Perf::MarkerNative,originalMarkers,owner,commands,render,root);
    targetDrawDistanceOn=oldDistanceOn;memcpy(targetDrawOrigin,oldOrigin,sizeof(oldOrigin));
    targetRangeForDraw=oldRange;extendedMarkerScope=oldScope;return result;
}
static U8 nextVisibleHook(U8* n,U8* sensors,float distance,UP unused,U8 team){
    if(n==directionVisibilityNode&&directionVisibilityDistance>0.f){
        distance=std::min(directionVisibilityDistance,32768.f);++directionRangeChecks;
    }else if(extendedMarkerScope&&std::isfinite(distance))distance=std::min(distance,32768.f);
    return originalMarkerVisible(n,sensors,distance,unused,team);
}
static UP nextMarkerHook(U8* owner,UP commands,U8* root,U8* render,U8* n,U8* own,float alpha,U8 team){
    auto oldDistanceDraw=selectedDistanceDraw;selectedDistanceDraw={};
    if(extendedTargetSelectionOn&&extendedTargetBoxesOn&&player&&owner==player+0x1868&&
       targetId(player)==(unsigned(n[0x234])|(unsigned(n[0x235])<<8))){
            float distance=length(sub(field<V>(n,0x258),field<V>(own,0x258)));
            if(std::isfinite(distance)&&distance>0&&nexttarget::nativeToFeet(distance)<=std::min(nextSelectFeet.load(),targetBoxFeet.load()))
                selectedDistanceDraw={owner,n,distance,false};
    }
    // M07 additionally rejects far points in 202ed0 via render+150's depth
    // plane (~19k feet), even after b9a60 range and sensor checks pass. Give
    // this single marker a local render-context copy; scene render state and
    // camera matrices stay untouched. Forced mission markers keep their path.
    alignas(16) U8 markerRender[0x1a0];U8* drawRender=render;
    if(extendedTargetBoxesOn&&player&&owner==player+0x1868&&validPlayer(player)&&
       root==field<U8*>(player,0x2040)&&!(n[0x1f4]&2)&&readable(render,sizeof(markerRender))){
        float range=nexttarget::feetToNative(targetBoxFeet.load());
        float distance=length(sub(field<V>(n,0x258),field<V>(own,0x258)));
        float cameraOffset=length(sub(field<V>(render,0x30),field<V>(own,0x258)));
        if(std::isfinite(distance)&&distance<=range&&std::isfinite(cameraOffset)){
            memcpy(markerRender,render,sizeof(markerRender));
            if(nexttarget::extendFarClip(reinterpret_cast<float*>(markerRender+0x150),range+cameraOffset+1.f)){
                drawRender=markerRender;++extendedClipCalls;
            }
        }
    }
    UP result=originalMarker(owner,commands,root,drawRender,n,own,alpha,team);
    selectedDistanceDraw=oldDistanceDraw;
    if(extendedMarkerScope){
        float distance=targetDrawDistanceOn?length(sub(field<V>(n,0x258),field<V>(own,0x258))):field<float>(n,0x20c+owner[0x3ca]*4);
        if(distance>32768.f&&!(n[0x1f4]&2)){
            ++extendedBoxDraws;
            if(result!=commands){
                ++extendedBoxPackets;float feet=nexttarget::nativeToFeet(distance),old=extendedBoxMaxFeet.load();
                while(feet>old&&!extendedBoxMaxFeet.compare_exchange_weak(old,feet)){}
            }
        }
    }
    // Do not put a floating NEXT label on a marker the native renderer culled.
    if(result!=commands&&nextTargetOn&&owner==player+0x1868)result=nextDrawLabel(owner,result,render,n);
    return result;
}
