// Add only the selected offscreen target's type to the native direction cue.
// Capture its native sprite anchor; do not reconstruct camera/arrow geometry.
static volatile LONG targetIndicatorNameOn;
static std::atomic<unsigned long long> targetIndicatorDraws{},targetIndicatorMissing{};
static std::atomic<unsigned long long> targetIndicatorDesignationDraws{};
static std::atomic<float> targetIndicatorX{},targetIndicatorY{};
static std::atomic<unsigned> targetIndicatorId{65535};
static std::atomic<unsigned long long> targetIndicatorOnscreenHidden{};
static bool selectedTargetInViewport(U8* render,const V* position){
    if(!readable(render,0x150)||!readable(position,sizeof(V))||!finite(*position))return false;
    const float* m=reinterpret_cast<const float*>(render+0x110);
    for(unsigned i=0;i<16;i++)if(!std::isfinite(m[i]))return false;
    float w=m[3]*position->x+m[7]*position->y+m[11]*position->z+m[15];
    if(!std::isfinite(w)||w<=.0001f)return false;
    float x=(m[0]*position->x+m[4]*position->y+m[8]*position->z+m[12])/w-1792.f;
    float y=(m[1]*position->x+m[5]*position->y+m[9]*position->z+m[13])/w-1824.f;
    // Use the full native viewport, not NEXT's inset label-safe rectangle or
    // the nose cone. Include the small marker's 8-pixel half extent so its
    // edge can leave too. Camera movement affects this, never NEXT ranking.
    return std::isfinite(x)&&std::isfinite(y)&&x>=-8.f&&x<=520.f&&y>=-8.f&&y<=456.f;
}
using NativeTargetDirection=UP(*)(U8*,UP,U8*,const uint32_t*,const V*,const V*,float,UP,float,U8);
static NativeTargetDirection originalTargetDirection;
extern "C" {void targetDirectionBridge();void* targetDirectionReturn;}
struct TargetDirectionScope {U8* owner{};U8* node{};bool captured{};float x{},y{};uint32_t color{};};
static thread_local TargetDirectionScope targetDirectionScope;
using NativeDirectionMarkers=UP(*)(U8*,UP,U8*,U8*);
static NativeDirectionMarkers originalDirectionMarkers;
static UP targetDirectionMarkersHook(U8* owner,UP commands,U8* render,U8* root){
    MemoryReadScope memoryScope;
    U8* oldNode=directionVisibilityNode;float oldDistance=directionVisibilityDistance;
    directionVisibilityNode=nullptr;directionVisibilityDistance=0;
    if(extendedTargetSelectionOn&&extendedTargetBoxesOn&&validPlayer(player)&&
       owner==player+0x1868&&root==field<U8*>(player,0x2040)){
        unsigned id=targetId(player);U8* n=node(id&255,id>>8);
        U8* own=node(player[0x1eea],player[0x1eeb]);
        if(n&&own&&field<U8*>(owner,0x328)==n){
            float distance=length(sub(field<V>(n,0x258),field<V>(own,0x258)));
            if(std::isfinite(distance)&&distance>0&&
               nexttarget::nativeToFeet(distance)<=std::min(nextSelectFeet.load(),targetBoxFeet.load())){
                directionVisibilityNode=n;directionVisibilityDistance=distance;
                directionRealFeet=nexttarget::nativeToFeet(distance);
            }
        }
    }
    // Native relationship, sensor map, stealth/hidden, angle and arrow geometry
    // all remain in this traversal. Only this selected node's range argument
    // to bf770 is corrected, without writing its distance cache.
    UP result=originalDirectionMarkers(owner,commands,render,root);
    directionVisibilityNode=oldNode;directionVisibilityDistance=oldDistance;return result;
}

extern "C" UP targetDirectionPacket(UP commands,uint16_t primitive,unsigned count,U8* vertices,UP texture){
    auto& s=targetDirectionScope;
    // Only the call at b056d, inside the real direction triangle b00c0.
    // Vertex zero is the triangle tip, already clamped by the native routine.
    if(s.owner&&!s.captured&&count==3&&vertices){
        float px=float(field<int>(vertices,0x14))/16.f-1792.f;
        float py=float(field<int>(vertices,0x18))/16.f-1824.f;
        if(std::isfinite(px)&&std::isfinite(py)&&px>=0&&px<=512&&py>=0&&py<=448){
            s.x=px;s.y=py;s.color=field<uint32_t>(vertices,0);s.captured=true;
        }
    }
    return proc<UP(*)(UP,uint16_t,unsigned,U8*,UP)>(0x1fe080)(commands,primitive,count,vertices,texture);
}
static bool targetDirectionText(U8* owner,U8* n,size_t indexOffset,size_t tableOffset,char (&text)[32]){
    U8* holder=field<U8*>(n,0x1a0);if(!readable(holder,8))return false;
    U8* spec=field<U8*>(holder,0);if(!readable(spec,indexOffset+2))return false;
    int index=field<int16_t>(spec,indexOffset);if(index<0||index>=4096)return false;
    U8* indicator=field<U8*>(owner,0x300);if(!readable(indicator,0x402))return false;
    unsigned language=indicator[0x401];if(language>=6)return false;
    UP base=field<UP>(indicator,tableOffset),offset=(size_t(index)*6+language)*32+0x10;
    if(!base||base+offset<base)return false;
    const char* value=reinterpret_cast<const char*>(base+offset);
    if(!readable(value,32))return false;
    // Same type table/localization as b6c10. Never overrun its 32-byte slot.
    auto end=static_cast<const char*>(memchr(value,0,32));if(!end||end==value)return false;
    memcpy(text,value,size_t(end-value)+1);return true;
}
static UP targetDirectionHook(U8* owner,UP commands,U8* render,const uint32_t* color,const V* position,const V* origin,float angle,UP unused,float scale,U8 other){
    auto old=targetDirectionScope;targetDirectionScope={};
    U8* selected=nullptr;
    // afd00 uses other=1 for own formation arrows and other=0 for selected
    // hostile direction cue. b05e0 was the gun lead circle, not this function.
    if(targetIndicatorNameOn&&!other&&player&&owner==player+0x1868&&validPlayer(player)){
        U8* n=field<U8*>(owner,0x328);
        if(readable(n,0x520)&&(n[0x1f0]&2)&&
           targetId(player)==(unsigned(n[0x234])|(unsigned(n[0x235])<<8))&&
           readable(position,sizeof(V))&&!memcmp(position,n+0x258,12))selected=n;
    }
    if(selected&&selectedTargetInViewport(render,position)){
        ++targetIndicatorOnscreenHidden;targetDirectionScope=old;return commands;
    }
    if(selected)targetDirectionScope={owner,selected};
    UP result=originalTargetDirection(owner,commands,render,color,position,origin,angle,unused,scale,other);
    auto captured=targetDirectionScope;targetDirectionScope=old;
    // No native arrow packet means no label, including onscreen/culling cases.
    if(!selected||!captured.captured||result==commands||field<U8*>(owner,0x328)!=selected||
       targetId(player)!=(unsigned(selected[0x234])|(unsigned(selected[0x235])<<8)))return result;
    char text[32]{},designation[32]{};
    if(!targetDirectionText(owner,selected,0x82,0x3f0,text)){++targetIndicatorMissing;return result;}
    bool named=targetDirectionText(owner,selected,0x84,0x3f8,designation);
    U8* indicator=field<U8*>(owner,0x300);
    // Conservative 8 HUD-pixel glyph bound for this native font. Prefer right
    // of the cue, flip left near the edge, clamp both sides and top/bottom.
    float typeHalf=std::max(8.f,float(strlen(text))*4.f),nameHalf=std::max(8.f,float(strlen(designation))*4.f);
    float half=named?std::max(typeHalf,nameHalf):typeHalf;
    float x=captured.x+12.f+half;if(x+half>504.f)x=captured.x-12.f-half;
    x=std::clamp(x,half+8.f,504.f-half);
    float left=x-half,y=std::clamp(captured.y+12.f,12.f,named?426.f:436.f);
    U8 dummy[32]{};
    using Text=UP(*)(U8*,UP,U8,U8,float,float,int,UP,const uint32_t*,const char*);
    result=proc<Text>(0xd5d80)(dummy,result,4,0,left+typeHalf,y,2,field<UP>(indicator,0x398),&captured.color,text);
    if(named){
        result=proc<Text>(0xd5d80)(dummy,result,4,0,left+nameHalf,y+10.f,2,field<UP>(indicator,0x398),&captured.color,designation);
        ++targetIndicatorDesignationDraws;
    }
    ++targetIndicatorDraws;targetIndicatorX=left+typeHalf;targetIndicatorY=y;
    targetIndicatorId=unsigned(selected[0x234])|(unsigned(selected[0x235])<<8);return result;
}
