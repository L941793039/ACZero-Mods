// A committed-region query is expensive in this game. Reuse positive results
// only inside a synchronous embedded-pool update or read-only HUD traversal.
// Never carry an address permission result across calls/frames or threads.
struct MemoryReadRegions {
    struct Region {UP first,last;};Region regions[64]{};
    unsigned depth{},count{};
};
static thread_local MemoryReadRegions memoryReadRegions;
static std::atomic<unsigned long long> memoryRegionQueries{},memoryRegionHits{};
struct MemoryReadScope {
    MemoryReadScope(){auto& r=memoryReadRegions;if(!r.depth++)r.count=0;}
    ~MemoryReadScope(){auto& r=memoryReadRegions;if(!--r.depth)r.count=0;}
    MemoryReadScope(const MemoryReadScope&)=delete;
    MemoryReadScope& operator=(const MemoryReadScope&)=delete;
};
static bool memoryReadable(const void* pointer,size_t bytes){
    UP first=UP(pointer),last=first+bytes;if(!first||!bytes||last<first)return false;
    auto& r=memoryReadRegions;
    if(r.depth)for(unsigned i=0;i<r.count;i++)if(first>=r.regions[i].first&&last<=r.regions[i].last){
        memoryRegionHits.fetch_add(1,std::memory_order_relaxed);return true;
    }
    MEMORY_BASIC_INFORMATION m{};memoryRegionQueries.fetch_add(1,std::memory_order_relaxed);
    if(!VirtualQuery(pointer,&m,sizeof(m))||m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    UP end=UP(m.BaseAddress)+m.RegionSize;
    if(end<UP(m.BaseAddress)||first<UP(m.BaseAddress)||last>end)return false;
    if(r.depth&&r.count<64)r.regions[r.count++]={UP(m.BaseAddress),end};
    return true;
}
