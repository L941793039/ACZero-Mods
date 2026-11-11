// Opt-in CPU wall-time diagnostics. Inclusive timings are nested; never sum
// parent and child buckets as if they were disjoint frame costs.
enum class Perf : unsigned { Mission, Player, PlayerNative, Steer, SteerNative,
    NormalPN, Velocity, VelocityNative, FlightLimit, Lock, LockNative,
    Markers, MarkerNative, Radar, RadarNative, XagmPool, AirPool, QaamPool, LasmPool, LagmPool, SaamPool, SodPool, Count };
static const char* perfNames[]={"mission","player","player_native","steer","steer_native",
    "normal_pn","velocity","velocity_native","flight_limit","lock","lock_native",
    "markers","marker_native","radar","radar_native","xagm_pool","air_pool","qaam_pool","lasm_pool","lagm_pool","saam_pool","sod_pool"};
static volatile LONG performanceOn;
struct PerfMetric {std::atomic<unsigned long long> ticks{},calls{},maximum{};};
static PerfMetric perfMetrics[unsigned(Perf::Count)];
static unsigned long long perfFrequency;
struct PerfScope {
    Perf which;unsigned long long start{};
    explicit PerfScope(Perf p):which(p){if(performanceOn){LARGE_INTEGER t;QueryPerformanceCounter(&t);start=t.QuadPart;}}
    ~PerfScope(){if(start){LARGE_INTEGER t;QueryPerformanceCounter(&t);auto elapsed=static_cast<unsigned long long>(t.QuadPart)-start;
        auto& m=perfMetrics[unsigned(which)];m.ticks.fetch_add(elapsed,std::memory_order_relaxed);m.calls.fetch_add(1,std::memory_order_relaxed);
        auto old=m.maximum.load(std::memory_order_relaxed);while(elapsed>old&&!m.maximum.compare_exchange_weak(old,elapsed,std::memory_order_relaxed)){}
    }}
};
template<class F,class... Args> static auto perfInvoke(Perf which,F fn,Args... args)->decltype(fn(args...)){
    PerfScope scope(which);return fn(args...);
}
static void performanceStatus(FILE* file){
    fprintf(file,"performance_profile=%ld\nmemory_region_queries=%llu\nmemory_region_cache_hits=%llu\n",performanceOn,memoryRegionQueries.exchange(0),memoryRegionHits.exchange(0));
    for(unsigned i=0;i<unsigned(Perf::Count);i++){
        auto& m=perfMetrics[i];auto ticks=m.ticks.exchange(0),calls=m.calls.exchange(0),peak=m.maximum.exchange(0);
        double us=perfFrequency?double(ticks)*1000000./double(perfFrequency):0.;
        double maxUs=perfFrequency?double(peak)*1000000./double(perfFrequency):0.;
        fprintf(file,"perf_%s_calls=%llu\nperf_%s_total_us=%.2f\nperf_%s_max_us=%.2f\n",perfNames[i],calls,perfNames[i],us,perfNames[i],maxUs);
    }
}
