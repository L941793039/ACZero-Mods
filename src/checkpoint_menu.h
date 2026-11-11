// Virtual extra row over the five native items: no changes to native menu
// allocations, ownership, resource tables or destruction. Existing submenus
// continue to use their original input/render path.
static volatile LONG checkpointMenuOn=1;
static U8* checkpointPause;
static bool checkpointMenuSelected,checkpointSubmenu;
static unsigned checkpointSubrow;
static std::atomic<unsigned long long> checkpointMenuInputs{},checkpointMenuDraws{};
using PauseMenuInput=UP(*)(U8*,U8,U8*,U8*,U8*,U8*);
using PauseMenuList=UP(*)(U8*,UP,U8*,unsigned);
using PauseMenuInit=UP(*)(U8*);
static PauseMenuInput originalPauseMenuInput;
static PauseMenuList originalPauseMenuList;
static PauseMenuInit originalPauseMenuInit;
static bool cpIsPause(U8* p){return readable(p,0x2b0)&&field<UP>(p,0)==gameAddress(0x2ac520);}
static bool cpIsFailure(U8* p){return readable(p,0x264)&&field<UP>(p,0)==gameAddress(0x2ac4a8);}
static UP checkpointFailureInput(U8*,U8,U8*,U8*,U8*,U8*);
static UP checkpointFailureList(U8*,UP,U8*,unsigned);
static std::atomic<unsigned long long> checkpointFailureInputs{},checkpointFailureDraws{};
static int cpMenuIndex(U8* root){
    if(!readable(root,0x98)||root[0x96]!=5)return -1;
    for(int i=0;i<5;i++){U8* item=field<U8*>(root,8+i*8);if(!readable(item,0x98))return -1;if(item[0x94])return i;}
    return root[0x97]<5?root[0x97]:-1;
}
static bool cpNativeSubmenu(U8* root){
    for(int i=0;i<5;i++){U8* item=field<U8*>(root,8+i*8);if(item[0x95])return true;}return false;
}
static void cpMenuChoose(U8* menu,U8* root,int index){
    for(int i=0;i<5;i++)field<U8*>(root,8+i*8)[0x94]=U8(i==index);
    field<int>(menu,0x58)=field<int>(field<U8*>(root,8+index*8),0x88);
}
static void cpMenuBeep(uint16_t event){uint16_t bank=0;proc<U8(*)(void*,const uint16_t*,const uint16_t*)>(0x1c6150)(nullptr,&bank,&event);}
static void cpMenuLifecycleConfirmed(U8* menu,U8 confirmed){
    if(!confirmed)return;
    int id=field<int>(menu,0x54);
    if(cpIsPause(menu)){
        if(id==1)cpInvalidate("pause Replay confirmed");
        else if(id==3)cpInvalidate("pause Retry confirmed");
        else if(id==6)cpInvalidate("pause Quit Mission confirmed");
    }else if(cpIsFailure(menu)){
        if(id==1)cpInvalidate("failure Restart confirmed");
        else if(id==4)cpInvalidate("failure Quit Mission confirmed");
    }
}
static UP checkpointPauseInitHook(U8* menu){
    if(cpIsPause(menu)){checkpointPause=menu;checkpointMenuSelected=checkpointSubmenu=false;checkpointSubrow=0;}
    return originalPauseMenuInit(menu);
}
static UP checkpointPauseInputHook(U8* menu,U8 index,U8* confirm,U8* cancel,U8* up,U8* down){
    if(checkpointMenuOn&&cpIsFailure(menu))return checkpointFailureInput(menu,index,confirm,cancel,up,down);
    if(!checkpointMenuOn||!cpIsPause(menu))return originalPauseMenuInput(menu,index,confirm,cancel,up,down);
    U8* root=field<U8*>(menu,0x48);int before=cpMenuIndex(root);
    if(before<0||cpNativeSubmenu(root)){
        UP result=originalPauseMenuInput(menu,index,confirm,cancel,up,down);
        cpMenuLifecycleConfirmed(menu,*confirm);return result;
    }
    if(checkpointPause!=menu){checkpointPause=menu;checkpointMenuSelected=checkpointSubmenu=false;}
    bool virtualRow=checkpointMenuSelected;bool sub=checkpointSubmenu;
    UP result=originalPauseMenuInput(menu,index,confirm,cancel,up,down);
    int after=cpMenuIndex(root);if(after<0)return result;
    bool accepted=*confirm!=0,back=*cancel!=0;
    if(virtualRow){
        if(sub){
            if(back){checkpointSubmenu=false;cpMenuBeep(0);}
            else if(accepted){InterlockedExchange(&checkpointRequest,checkpointSubrow?2:1);cpMenuBeep(2);}
            else if(after!=before){checkpointSubrow^=1;cpMenuBeep(1);}
            cpMenuChoose(menu,root,0);*confirm=*cancel=0;menu[0x260]=menu[0x261]=0;field<int>(menu,0x54)=-1;
        }else if(accepted){
            checkpointSubmenu=true;checkpointSubrow=0;cpMenuChoose(menu,root,0);
            *confirm=0;menu[0x260]=0;field<int>(menu,0x54)=-1;cpMenuBeep(2);
        }else if(back){checkpointMenuSelected=false;}
        else if(after!=before){
            checkpointMenuSelected=false;
            if(after==4){cpMenuChoose(menu,root,0);cpMenuBeep(1);} // CP -> RETURN on Up
            // CP -> REPLAY on Down already has the correct native selection.
        }
    }else if(!accepted&&!back&&((before==0&&after==1)||(before==1&&after==0))){
        checkpointMenuSelected=true;cpMenuChoose(menu,root,0);
        if(before==0)cpMenuBeep(1);
    }
    cpMenuLifecycleConfirmed(menu,*confirm);++checkpointMenuInputs;return result;
}
static UP checkpointPauseListHook(U8* menu,UP commands,U8* root,unsigned fps){
    if(checkpointMenuOn&&cpIsFailure(menu))return checkpointFailureList(menu,commands,root,fps);
    if(!checkpointMenuOn||!cpIsPause(menu)||root!=field<U8*>(menu,0x48)||cpMenuIndex(root)<0||cpNativeSubmenu(root))
        return originalPauseMenuList(menu,commands,root,fps);
    U8* indicator=field<U8*>(reinterpret_cast<void*>(image),gameRva(0x328418));
    if(!readable(indicator,0x3a0))return originalPauseMenuList(menu,commands,root,fps);
    uint32_t color=field<uint32_t>(indicator,0x2ec);UP texture=field<UP>(indicator,0x398);
    using Text=UP(*)(U8*,UP,U8,U8,int,int,int,UP,const uint32_t*,const char*);
    using Rect=UP(*)(U8*,UP,int,int,int,int,const uint32_t*);
    auto text=[&](int x,int y,int font,const char* s){commands=proc<Text>(0xd5ab0)(menu,commands,U8(font),0,x,y,2,texture,&color,s);};
    auto box=[&](int x,int y,int w,int h){commands=proc<Rect>(0xd93b0)(menu,commands,(x+1792)*16,(y+1824)*16,(x+w+1792)*16,(y+h+1824)*16,&color);};
    const char* labels[]={"RETURN TO GAME","CHECKPOINT","REPLAY","RETRY","QUIT MISSION","FLIGHT OPTIONS"};
    int native=cpMenuIndex(root),selected=checkpointMenuSelected?1:(native==0?0:native+1);
    for(int row=0;row<6;row++){int y=90+row*25;text(67,y,0,labels[row]);if(row==selected)box(60,y-6,150,22);}
    if(checkpointSubmenu){
        text(224,115,0,"SAVE");text(224,140,0,"LOAD");box(217,109+int(checkpointSubrow)*25,83,22);
        text(67,255,4,checkpointMessage(checkpointResult.load()));
    }
    ++checkpointMenuDraws;return commands;
}
// CMenuOver has two native rows; both Up and Down map 0 -> 1, so selection
// deltas cannot identify direction. Temporarily present a third leaf to native
// input only, then restore the owned pointer array before returning. The mod's
// leaf is never passed to native allocation/destruction or original drawing.
static U8* checkpointFailure;
alignas(16) static U8 checkpointFailureLeaf[0x98];
static bool cpFailureRoot(U8* menu,U8* root){
    if(!readable(root,0x98)||root!=field<U8*>(menu,0x48)||root[0x96]!=2)return false;
    for(int i=0;i<2;i++){U8* n=field<U8*>(root,8+i*8);if(!readable(n,0x98)||n[0x95])return false;}
    return true;
}
static void cpFailureInitLeaf(U8* menu,U8* root){
    if(checkpointFailure==menu)return;
    checkpointFailure=menu;memset(checkpointFailureLeaf,0,sizeof(checkpointFailureLeaf));
    field<UP>(checkpointFailureLeaf,0)=field<UP>(field<U8*>(root,8),0);
    field<int>(checkpointFailureLeaf,0x88)=0x100000;
}
struct CheckpointFailureTree {
    U8* root;UP old1,old2;U8 cursor;
    explicit CheckpointFailureTree(U8* r):root(r),old1(field<UP>(r,0x10)),old2(field<UP>(r,0x18)),cursor(r[0x97]){
        field<UP>(r,0x10)=UP(checkpointFailureLeaf);field<UP>(r,0x18)=old1;r[0x96]=3;
    }
    ~CheckpointFailureTree(){field<UP>(root,0x10)=old1;field<UP>(root,0x18)=old2;root[0x96]=2;root[0x97]=cursor;}
};
static UP checkpointFailureInput(U8* menu,U8 index,U8* confirm,U8* cancel,U8* other1,U8* other2){
    U8* root=field<U8*>(menu,0x48);
    if(!cpFailureRoot(menu,root)){
        UP result=originalPauseMenuInput(menu,index,confirm,cancel,other1,other2);
        cpMenuLifecycleConfirmed(menu,*confirm);return result;
    }
    cpFailureInitLeaf(menu,root);UP result;
    {CheckpointFailureTree temporary(root);result=originalPauseMenuInput(menu,index,confirm,cancel,other1,other2);}
    if(checkpointFailureLeaf[0x94]&&*confirm){
        InterlockedExchange(&checkpointRequest,3);cpMenuBeep(2);
        *confirm=*cancel=0;menu[0x260]=menu[0x261]=0;field<int>(menu,0x54)=-1;
        checkpointFailureLeaf[0x95]=checkpointFailureLeaf[0x94]=0;
        field<U8*>(root,8)[0x94]=1;field<U8*>(root,0x10)[0x94]=0;field<int>(menu,0x58)=0;
    }
    cpMenuLifecycleConfirmed(menu,*confirm);++checkpointFailureInputs;return result;
}
static UP checkpointFailureList(U8* menu,UP commands,U8* root,unsigned fps){
    if(!cpFailureRoot(menu,root))return originalPauseMenuList(menu,commands,root,fps);
    cpFailureInitLeaf(menu,root);U8* indicator=field<U8*>(reinterpret_cast<void*>(image),gameRva(0x328418));
    if(!readable(indicator,0x3a0))return originalPauseMenuList(menu,commands,root,fps);
    uint32_t color=field<uint32_t>(indicator,0x2ec);UP texture=field<UP>(indicator,0x398);
    using Text=UP(*)(U8*,UP,U8,U8,int,int,int,UP,const uint32_t*,const char*);
    using Rect=UP(*)(U8*,UP,int,int,int,int,const uint32_t*);
    const char* labels[]={"RESTART","LOAD CHECKPOINT","QUIT MISSION"};
    int selected=checkpointFailureLeaf[0x94]?1:(field<U8*>(root,0x10)[0x94]?2:0);
    for(int row=0;row<3;row++){
        int y=321+row*25;commands=proc<Text>(0xd5ab0)(menu,commands,0,0,67,y,2,texture,&color,labels[row]);
        if(row==selected)commands=proc<Rect>(0xd93b0)(menu,commands,(60+1792)*16,(y-6+1824)*16,(260+1792)*16,(y+16+1824)*16,&color);
    }
    // The native panel already ends just below the third row. Keep refusal
    // feedback above the panel; do not alter its background allocation.
    if(checkpointResult<0)commands=proc<Text>(0xd5ab0)(menu,commands,4,0,67,270,2,texture,&color,checkpointMessage(checkpointResult.load()));
    ++checkpointFailureDraws;return commands;
}
