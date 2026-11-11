// Shared SEQ data, distinct from the immutable shared-context pointer header.
// Standalone native cca2a..cca47 allocates N*2 variable bytes followed by
// sum(descriptor.conditionCount) latch bytes. a9900 resets those exact banks.
static void cpCaptureScriptState(CheckpointNative& s,UP shared){
    auto context=reinterpret_cast<void*>(shared);
    UP definition=field<UP>(context,0),variables=field<UP>(context,0x18),latches=field<UP>(context,0x20);
    if(!definition){
        cpRequire(!variables&&!latches,"Script banks without a definition");
        return;
    }
    cpGuard(s,definition,0x18);
    UP header=field<UP>(reinterpret_cast<void*>(definition),0);
    cpGuard(s,header,2);uint16_t count{};cpRead(header,&count,2);
    cpRequire(count<=4096,"Too many shared script variables");
    UP records=field<UP>(reinterpret_cast<void*>(definition),0x10);
    if(count)cpGuard(s,records,size_t(count)*16);
    size_t latchCount=0;
    for(unsigned i=0;i<count;i++){
        UP descriptor=field<UP>(reinterpret_cast<void*>(records),size_t(i)*16);
        cpGuard(s,descriptor,1);U8 n{};cpRead(descriptor,&n,1);
        latchCount+=n;cpRequire(latchCount<=65536,"Too many shared script conditions");
    }
    size_t variableBytes=size_t(count)*2;
    cpRequire(variables+variableBytes>=variables&&latches==variables+variableBytes,
        "Shared script bank layout changed");
    cpSaveSpan(s,variables,variableBytes);cpSaveSpan(s,latches,latchCount);
}
