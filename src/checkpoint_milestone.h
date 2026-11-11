#pragma once
#include <cstdint>
#include <initializer_list>
namespace checkpoint {
// M18S shared SEQ profile, derived from native a9c80/aa20d/a9efa and a paused
// opening -> round-two capture. Never infer progress from all SEQ bytes or HP.
// Unknown scripts retain native-phase autosave only. Read-only recognition is
// cached by mission-session AND shared-definition identity, not by addresses alone.
struct MilestoneProbe {
    uintptr_t root{},shared{},definition{};uint64_t session{};bool cached{},pixy{};
    template<class R> static uintptr_t pointer(R& r,uintptr_t at){uintptr_t p{};r(at,&p,sizeof(p));return p;}
    template<class R> static bool rule(R& r,uintptr_t table,unsigned var,
                                      uint16_t assignment,unsigned type,unsigned slot,uint32_t parameter){
        auto desc=pointer(r,table+var*16),conditions=pointer(r,table+var*16+8);
        uint8_t n{};if(!r(desc,&n,1)||n!=1)return false;
        auto header=pointer(r,conditions),clauses=pointer(r,conditions+8);
        if(!r(header,&n,1)||n!=2)return false;
        uint8_t assignType{},conditionType{};
        if(!r(pointer(r,clauses),&assignType,1)||assignType!=0||
           !r(pointer(r,clauses+72),&conditionType,1)||conditionType!=type)return false;
        uint32_t action{},test{};
        auto a=pointer(r,pointer(r,clauses+8));
        auto b=pointer(r,pointer(r,clauses+72+slot*8));
        // action includes op=SET and flags=one-shot. Parameter includes native
        // group/node/percentage/flags, or variable/value (equality byte follows).
        if(!r(a,&action,4)||action!=assignment||!r(b,&test,4)||test!=parameter)return false;
        if(type==2){uint8_t comparison{};if(!r(b+4,&comparison,1)||comparison!=0)return false;}
        return true;
    }
    template<class R> unsigned sample(R& r,uintptr_t currentRoot,uint64_t currentSession,bool& ready){
        ready=true;auto currentShared=pointer(r,currentRoot+8);
        auto currentDefinition=pointer(r,currentShared);
        if(!cached||root!=currentRoot||shared!=currentShared||definition!=currentDefinition||session!=currentSession){
            root=currentRoot;shared=currentShared;definition=currentDefinition;session=currentSession;
            cached=true;pixy=false;uint16_t count{};uint8_t groups{};
            if(!r(pointer(r,definition),&count,2)||count!=93||!r(root+0x34,&groups,1)||groups!=61)return 0;
            auto table=pointer(r,definition+0x10);
            pixy=rule(r,table,1,1,2,3,0x0001000e)&&rule(r,table,3,1,2,3,0x00010012)&&
                 rule(r,table,14,1,5,6,0x0019000b)&&rule(r,table,16,1,5,6,0x0019001b)&&
                 rule(r,table,18,1,5,6,0x00640023);
        }
        if(!pixy)return 0;
        ready=false;int16_t values[19]{};auto bank=pointer(r,shared+0x18);
        if(!r(bank,values,sizeof(values)))return 0;
        // One-shot damage milestones must be coherent. Completion is never a
        // save candidate. A threshold alone is insufficient: wait for the next
        // round's target to become active after its scripted transition.
        for(unsigned i:{14u,16u,18u})if(values[i]<0||values[i]>1)return 0;
        if(values[18]||(!values[14]&&values[16]))return 0;
        unsigned stage=values[16]?3:values[14]?2:1;
        unsigned ids[3]={11,27,35};auto array=pointer(r,root);
        for(unsigned i=0;i<3;i++){
            auto group=pointer(r,array+ids[i]*8);uint32_t flags{},count{};
            if(!r(group+0x120,&flags,4)||!r(group+0x118,&count,4)||count!=(i==0?2u:1u))return stage;
            if(i!=stage-1){if(flags&6)return stage;continue;}
            if((flags&7)!=7)return stage;
            auto node=pointer(r,group+0x110);uint8_t nodeFlags{};int16_t hp{};
            if(!r(node+0x1f4,&nodeFlags,1)||!(nodeFlags&1)||(nodeFlags&10)||
               !r(node+0x428,&hp,2)||hp<=0)return stage;
        }
        ready=true;return stage;
    }
};
}
