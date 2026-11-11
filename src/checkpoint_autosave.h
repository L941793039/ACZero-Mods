#pragma once
#include <cstdint>

namespace checkpoint {
// Only semantic native phase identity belongs here: never elapsed time, HP,
// camera pose, or a hash of all SEQ bytes (many are continuously changing).
struct PhaseKey {
    uintptr_t manager{},player{},root{},mission{},owner{},definition{};
    uint64_t session{};
    unsigned phase{},subphase{},mode{},milestone{};
    bool operator==(const PhaseKey& b) const {
        return manager==b.manager&&player==b.player&&root==b.root&&mission==b.mission&&
            owner==b.owner&&definition==b.definition&&session==b.session&&
            phase==b.phase&&subphase==b.subphase&&mode==b.mode&&milestone==b.milestone;
    }
};
struct AutoSave {
    PhaseKey candidate{},saved{};
    bool haveCandidate{},haveSaved{};
    uint64_t stable{},lastAttempt{};
    bool attempted{};
    void interrupt(){haveCandidate=false;stable=0;attempted=false;}
    void reset(){*this=AutoSave{};}
    bool due(const PhaseKey& key,uint64_t frame,bool eligible,unsigned settle=30,unsigned retry=120){
        if(!eligible){interrupt();return false;}
        if(!haveCandidate||!(key==candidate)){
            candidate=key;haveCandidate=true;stable=0;attempted=false;
        }
        if(haveSaved&&saved==key)return false;
        if(stable<settle){++stable;return false;}
        if(attempted&&frame-lastAttempt<retry)return false;
        attempted=true;lastAttempt=frame;return true;
    }
    void committed(){saved=candidate;haveSaved=true;}
};
}
