// Only the inspected LodModel instance layout. Resources, callback tables,
// parent/child ownership and GPU handles remain identity guards, never restores.
#include <unordered_set>
struct CpModelCapture {
    CheckpointNative& point;
    std::unordered_set<UP> models,submodels,poses,motions,visiting;
    void motion(UP at){
        if(!at||!motions.insert(at).second)return;
        cpRequire(motions.size()<=16384,"Too many model motion channels");
        // Native translation and rotation samplers (15ecc0 / 15fcc0) both
        // read resource links at +8/+10 and the current sample frame at +18.
        cpGuard(point,at,0x18);cpSaveSpan(point,at+0x18,4);
    }
    void pose(UP at,unsigned depth=0){
        if(!at)return;
        cpRequire(!visiting.count(at),"Cyclic model pose hierarchy");
        if(poses.count(at))return;
        cpRequire(depth<64&&poses.size()<16384,"Model pose hierarchy exceeds bound");
        auto b=cpBytes(at,0x320);int count{};memcpy(&count,b.data()+0xe0,4);
        cpRequire(count>=0&&count<=64,"Invalid model child count");
        poses.insert(at);visiting.insert(at);
        cpGuard(point,at+0xd8,0x10);cpGuard(point,at+0xe8,size_t(count)*8);
        cpGuard(point,at+0x2e8,0x30);
        // Inline numeric local/world transforms; child pointers start at d8.
        cpSaveSpan(point,at,0xd8);cpSaveSpan(point,at+0x318,8);
        for(size_t off:{0x2f8,0x300})motion(field<UP>(reinterpret_cast<void*>(at),off));
        for(int i=0;i<count;i++){
            UP child=field<UP>(reinterpret_cast<void*>(at),0xe8+i*8);
            cpRequire(child&&readable(reinterpret_cast<void*>(child),0x320)&&
                field<UP>(reinterpret_cast<void*>(child),0xd8)==at,"Model child ownership changed");
            pose(child,depth+1);
        }
        visiting.erase(at);
    }
    void submodel(UP at){
        if(!at||!submodels.insert(at).second)return;
        cpRequire(submodels.size()<=8192,"Too many model instances");
        auto b=cpBytes(at,0x100);uint16_t counts[4];memcpy(counts,b.data()+0x98,8);
        for(auto count:counts)cpRequire(count<=4096,"Invalid model property extent");
        cpGuard(point,at,0x18);cpGuard(point,at+0x90,0x40);
        // Property indices are absolute: banks begin at 0, 40 and 60.
        // Each bank has numeric values and a separate immutable callback table.
        const unsigned first[3]={0,0x40,0x60},limit[3]={0x40,0x60,4096};
        for(unsigned i=0;i<3;i++){
            unsigned end=std::min<unsigned>(counts[0],std::min<unsigned>(counts[i+1],limit[i]));
            if(end>first[i]){
                UP values=field<UP>(reinterpret_cast<void*>(at),0xa0+i*0x10);
                cpRequire(values!=0,"Missing model property bank");
                cpSaveSpan(point,values,(end-first[i])*4);
            }
        }
        pose(field<UP>(reinterpret_cast<void*>(at),0x90));
    }
    void model(UP at){
        if(!at||!models.insert(at).second)return;
        cpRequire(models.size()<=8192,"Too many unit models");
        cpGuard(point,at,8);
        if(field<UP>(reinterpret_cast<void*>(at),0)!=gameAddress(0x2ab6b0))return;
        // Native 14da40 constructs FIVE model instances, and 14dfe0 updates
        // all five (50/58/60/68/70). The last instance is not necessarily an
        // alias of one of the four distance LODs; M17S CTRL has a distinct one.
        cpGuard(point,at+0x50,0x28);
        // LodModel scalar interpolation state ends before embedded ObjAnimCtrl.
        cpSaveSpan(point,at+0x120,0x30);
        for(size_t off:{0x50,0x58,0x60,0x68,0x70})submodel(field<UP>(reinterpret_cast<void*>(at),off));
    }
    void unit(UP at){
        cpGuard(point,at+0x390,0x10);
        model(field<UP>(reinterpret_cast<void*>(at),0x390));
        model(field<UP>(reinterpret_cast<void*>(at),0x398));
    }
};
static void cpCaptureHudTimer(CheckpointNative& s,UP array,unsigned groups){
    // HUD owner = player+1868. b863d..b86ec decrements +36c once/frame
    // against tracked group/index +3c7/+3c8; b16c0 renders that counter.
    UP p=UP(s.player),source=field<UP>(s.player,0x1ba0);
    int frames=field<int>(s.player,0x1bd4);
    cpRequire(frames>=-1,"Invalid HUD countdown");
    if(source){
        bool found=false;
        for(unsigned g=0;g<groups;g++){
            auto head=reinterpret_cast<void*>(field<UP>(reinterpret_cast<void*>(array),g*8));
            UP pool=field<UP>(head,0x110);unsigned count=field<unsigned>(head,0x118);
            if(source>=pool&&(source-pool)%0x520==0&&(source-pool)/0x520<count){found=true;break;}
        }
        cpRequire(found,"HUD timer source outside saved units");
    }
    if(frames>=0){
        unsigned g=s.player[0x1c2f],n=s.player[0x1c30];
        cpRequire(g<groups,"Invalid HUD timer group");
        auto head=reinterpret_cast<void*>(field<UP>(reinterpret_cast<void*>(array),g*8));
        cpRequire(n<field<unsigned>(head,0x118),"Invalid HUD timer unit");
    }
    cpGuard(s,p+0x1c28,1); // Native frames-per-second remains the same.
    cpSaveSpan(s,p+0x1ba0,8);cpSaveSpan(s,p+0x1bd4,4);cpSaveSpan(s,p+0x1c2f,2);
}
