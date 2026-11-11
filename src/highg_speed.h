#pragma once
#include <cmath>

struct HighGSpeedGate {
    bool engaged=false;
    void reset() { engaged=false; }
    bool update(bool enabled,bool requested,float speed,float exitSpeed,float enterSpeed) {
        if(!enabled||!requested||!std::isfinite(speed)||!std::isfinite(exitSpeed)||
           !std::isfinite(enterSpeed)||exitSpeed<=0.f||enterSpeed<exitSpeed) {
            engaged=false; return false;
        }
        if(engaged) { if(speed<=exitSpeed) engaged=false; }
        else if(speed>=enterSpeed) engaged=true;
        return engaged;
    }
};

// Same density/reference ratio as AC0 0x12ab0b..0x12ac23. The native
// 1.422222 units conversion cancels the HUD's 0.703125 km/h conversion.
inline bool ac0StallSpeed(float baseStall,float density,float reference,float& speed) {
    if(!std::isfinite(baseStall)||baseStall<30.f||baseStall>900.f||
       !std::isfinite(density)||density<=0.f||!std::isfinite(reference)||reference<=0.f) return false;
    float result=baseStall/std::sqrt(density/reference);
    if(!std::isfinite(result)||result<30.f||result>1800.f) return false;
    speed=result; return true;
}

// AC0 starts its native nose-down branch when low-speed severity > 0.5
// (0x12b33b). Severity interpolates between buffet and full stall limits.
inline float ac0StallOnsetSpeed(float stall,float buffet) {
    return (stall+buffet)*0.5f;
}
