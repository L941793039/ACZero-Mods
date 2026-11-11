#pragma once
#include <algorithm>
#include <cmath>

namespace guntrack {
constexpr float pi=3.14159265358979323846f;
struct Vec { float x,y,z; };
inline Vec add(Vec a,Vec b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec mul(Vec a,float n){return {a.x*n,a.y*n,a.z*n};}
inline float dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline bool finite(Vec a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
inline Vec unit(Vec a){float n=std::sqrt(dot(a,a));return finite(a)&&n>1e-6f?mul(a,1.f/n):Vec{0,0,-1};}
inline float angle(Vec a,Vec b){return std::acos(std::clamp(dot(unit(a),unit(b)),-1.f,1.f));}
inline Vec toward(Vec from,Vec to,float radians){
    from=unit(from);to=unit(to);float a=angle(from,to);
    if(a<1e-6f||a<=radians)return to;
    if(!(radians>0.f))return from;
    float s=std::sin(a);
    if(std::fabs(s)<1e-5f)return from;
    return unit(add(mul(from,std::sin(a-radians)/s),mul(to,std::sin(radians)/s)));
}
struct State {
    Vec direction{0,0,-1};
    void reset(){direction={0,0,-1};}
    bool step(Vec desired,bool eligible,float dt,float coneDegrees,float trackDegrees,float returnDegrees){
        const Vec center{0,0,-1};
        float cone=std::clamp(coneDegrees,0.f,10.f)*pi/180.f;
        if(!std::isfinite(dt)||dt<=0.f||!finite(desired))eligible=false;
        float desiredAngle=angle(center,desired);
        eligible=eligible&&desiredAngle<=cone;
        direction=toward(center,direction,cone); // also respects a reduced live setting
        float rate=eligible?trackDegrees:returnDegrees;
        direction=toward(direction,eligible?desired:center,std::clamp(dt,0.f,.25f)*std::max(0.f,rate)*pi/180.f);
        return eligible;
    }
};
inline bool intercept(Vec relative,Vec velocity,float speed,float& time){
    if(!finite(relative)||!finite(velocity)||!std::isfinite(speed)||speed<=1e-5f)return false;
    float a=dot(velocity,velocity)-speed*speed,b=2.f*dot(relative,velocity),c=dot(relative,relative);
    if(c<1e-6f){time=0;return true;}
    if(std::fabs(a)<1e-6f){if(b>=-1e-6f)return false;time=-c/b;}
    else {
        float d=b*b-4.f*a*c;if(d<0.f)return false;
        float r=std::sqrt(d),t1=(-b-r)/(2.f*a),t2=(-b+r)/(2.f*a);
        if(t1>0.f&&t2>0.f)time=std::min(t1,t2);
        else if(t1>0.f)time=t1;else if(t2>0.f)time=t2;else return false;
    }
    return std::isfinite(time)&&time>=0.f&&time<=180.f;
}
}
