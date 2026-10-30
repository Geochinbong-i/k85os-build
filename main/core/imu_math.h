#pragma once
#include <cmath>
struct K85Vec { float x,y; };
// 원리: 기준 장착 축→중립 제거 후 상대 화면 회전→사용자 교환/반전을 적용한다.
inline K85Vec k85_imu_transform(float x,float y,int quarter,bool swap,bool ix,bool iy) {
    K85Vec v{x,y};
    switch (quarter&3) {
    case 1: v={-y,x}; break; case 2: v={-x,-y}; break; case 3: v={y,-x}; break;
    }
    if (swap) { float t=v.x; v.x=v.y; v.y=t; }
    if (ix) v.x=-v.x;
    if (iy) v.y=-v.y;
    return v;
}
inline float k85_imu_deadzone(float value,float threshold) {
    return std::fabs(value)<=threshold ? 0.0f : std::copysign(std::fabs(value)-threshold,value);
}
inline float k85_safe_dt(float dt) { return dt<0 ? 0 : (dt>0.1f ? 0.1f:dt); }
// 용어: 데드존=미세 기울기 무시 구간, dt=마지막 처리부터 경과한 초.
