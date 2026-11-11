// e9340 collects native radar rows; e9ce0 later projects/blinks them. Native
// collection only visits the two sortie type IDs at weapon+1f530/+1f531.
// Append missing DualSP missile pools to this disposable HUD cache, never to
// that global type list, flight pools, lock state or radar drawing code.
using NativeRadarMissiles=void(*)(U8*,U8*,U8*);
static NativeRadarMissiles originalRadarMissiles;
static std::atomic<unsigned long long> radarSpAdded{};
static std::atomic<unsigned> radarSpTypes{};
static void radarMissilesHook(U8* radar,U8* context,U8* weapon){
    PerfScope timer(Perf::Radar);
    MemoryReadScope memoryScope;
    perfInvoke(Perf::RadarNative,originalRadarMissiles,radar,context,weapon);
    radarSpTypes=0;
    if(!dualOn||!dualInitialized||dualPlayer!=player||!validPlayer(player)||dualPlane!=player[0x20a3]||
       weapon!=field<U8*>(player,0x1fc8)||!readable(radar,0xb90)||!radar[8]||
       !readable(weapon,0x1f640))return;
    U8* planeData=field<U8*>(player,0xff8);if(!readable(planeData,7))return;
    int count=field<int>(radar,0xb8c);if(count<0||count>=79)return;
    if(slotA<0||slotA>2||slotB<0||slotB>2)return;
    const size_t offsets[17]={0,0,0,0,0,0,0,0,0,0,0x10348,0x10b48,0x11248,0x12048,0x11948,0x12748,0x12e48};
    unsigned seen=0;
    for(int slot:{slotA,slotB}){
        unsigned type=planeData[4+slot];
        if(type<10||type>16||type==weapon[0x1f530]||type==weapon[0x1f531]||(seen&(1u<<type)))continue;
        seen|=1u<<type;
        U8* ctrl=weapon+0x1b10+offsets[type];
        if(field<U8*>(weapon,0x1f540+type*8)!=ctrl||!readable(ctrl,0x80))continue;
        uint32_t mask=field<uint32_t>(ctrl,0x5c);if(mask&0xffff0000u)continue;
        size_t stride=type==10?0x78:0x68;
        if(!mask||!readable(ctrl+0x80,stride*16))continue;
        unsigned added=0;
        for(unsigned i=0;i<16&&count<79;i++)if(mask&(1u<<i)){
            U8* missile=ctrl+0x80+i*stride;
            if(!finite(field<V>(missile,0))||!finite(field<V>(missile,0x20)))continue;
            U8* row=radar+0x38+size_t(count)*0x24;
            memcpy(row,missile,16);memcpy(row+0x10,missile+0x20,16);
            // Same phase seed as e9c01..e9c1b; native renderer owns blinking.
            field<unsigned>(row,0x20)=unsigned(missile[0x53])*3+missile[0x52];
            ++count;++added;
        }
        if(added){radarSpAdded.fetch_add(added);radarSpTypes.fetch_or(1u<<type);}
    }
    field<int>(radar,0xb8c)=count;
}
