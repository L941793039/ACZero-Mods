#pragma once
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

enum class PnWeapon : unsigned { MSL, QAAM, XAAM, XLAA, SAAM, LASM, LAGM, XAGM, SOD, Other, Count };
constexpr unsigned pnWeaponCount=static_cast<unsigned>(PnWeapon::Count);
inline constexpr const char* pnWeaponKeys[pnWeaponCount]={
    "MSL","QAAM","XAAM","XLAA","SAAM","LASM","LAGM","XAGM","SOD","Other"
};
// Default is a native-guidance bypass, not an invented vanilla PN coefficient.
constexpr float pnDefaultGain=0.f;

inline bool parsePnGain(const char* text,float& result) {
    if(!text) return false;
    errno=0;
    char* end=nullptr;
    float parsed=std::strtof(text,&end);
    if(end==text||errno==ERANGE||!std::isfinite(parsed)||parsed<0.f||parsed>20.f) return false;
    while(std::isspace(static_cast<unsigned char>(*end))) ++end;
    if(*end && *end!=';' && *end!='#') return false;
    result=parsed;
    return true;
}

// Embedded controller boundaries from the verified 1.0.2.1 constructor at
// 0x1408fc..0x140abb, relative to manager+0x1b10. Identify the projectile's
// owner, never the player's currently selected SP: both can be in flight.
inline PnWeapon spPnWeapon(std::uintptr_t offset) {
    struct Span { std::uintptr_t begin,end; PnWeapon weapon; };
    constexpr Span spans[]={
        {0x0bec8,0x10348,PnWeapon::SOD},
        {0x10348,0x10b48,PnWeapon::LASM},
        {0x10b48,0x11248,PnWeapon::LAGM},
        {0x11248,0x11948,PnWeapon::XAGM},
        {0x11948,0x12048,PnWeapon::XAAM},
        {0x12048,0x12748,PnWeapon::XLAA},
        {0x12748,0x12e48,PnWeapon::SAAM},
        {0x12e48,0x13548,PnWeapon::QAAM}
    };
    for(auto span:spans) if(offset>=span.begin+0x80 && offset<span.end) return span.weapon;
    return PnWeapon::Other;
}
