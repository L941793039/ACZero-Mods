#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>
#include <stdexcept>

// Reusable transactional byte plan. Validation is completed before any mutation.
// The caller owns lifecycle/phase validation and runs it on the paused game thread.
namespace checkpoint {
using Bytes=std::vector<unsigned char>;
struct Span { uintptr_t address; Bytes bytes; };
struct Journal {
    std::vector<Span> writes;
    template<class Read> void add(uintptr_t address,const void* bytes,size_t size,Read read) {
        if(!size)return;
        if(!address||address+size<address)throw std::runtime_error("Invalid checkpoint range");
        for(auto& s:writes)if(address<s.address+s.bytes.size()&&s.address<address+size)
            throw std::runtime_error("Overlapping checkpoint ranges");
        Bytes current(size);read(address,current.data(),size);
        if(std::memcmp(current.data(),bytes,size))writes.push_back({address,Bytes(static_cast<const unsigned char*>(bytes),static_cast<const unsigned char*>(bytes)+size)});
    }
    template<class Read,class Write> void apply(Read read,Write write) {
        std::vector<Span> undo;undo.reserve(writes.size());
        for(auto& s:writes){Bytes b(s.bytes.size());read(s.address,b.data(),b.size());undo.push_back({s.address,std::move(b)});}
        // Catch externally changing state during preparation. No partial writes.
        for(auto& s:undo){Bytes b(s.bytes.size());read(s.address,b.data(),b.size());if(b!=s.bytes)throw std::runtime_error("Checkpoint state changed during validation");}
        size_t touched=0;
        try {
            for(auto& s:writes){++touched;write(s.address,s.bytes.data(),s.bytes.size());}
            for(auto& s:writes){Bytes b(s.bytes.size());read(s.address,b.data(),b.size());if(b!=s.bytes)throw std::runtime_error("Checkpoint readback failed");}
        } catch(...) {
            while(touched){auto& s=undo[--touched];write(s.address,s.bytes.data(),s.bytes.size());}
            throw;
        }
    }
};
}
