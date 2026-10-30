#pragma once
#include <cstdint>

// 튜닝: 더블 탭 창을 늘리면 단일 이동 반응도 그만큼 늦어진다(ms).
constexpr uint32_t K85_DOUBLE_MS = 250;
constexpr uint32_t K85_LONG_MS = 600;
constexpr uint32_t K85_CHORD_MS = 500;
enum class K85Nav { None, Select, Next, Previous, Back, EmergencyBack };

// 원리: M5가 디바운스한 물리 상태를 한 번 받아 해제/유지 시간으로 의미를 만든다.
// SELECT는 해제 때만 확정하므로 두 버튼을 누른 채 선택이 먼저 실행되지 않는다.
// 프로필/화면 전환 시 reset()하여 눌린 버튼이 모두 해제될 때까지 입력을 격리한다.
class K85Navigation {
    bool pa=false, pb=false, gated=true, chord=false, chord_done=false;
    bool nav_long=false, second=false, pending=false;
    bool sel_used=false;
    uint32_t nav_start=0, tap_end=0, chord_start=0;
public:
    void reset() { *this=K85Navigation(); }
    K85Nav step(uint32_t now, bool a, bool b, bool legacy=false, bool swap=false) {
        if (gated) { if (!a && !b) gated=false; pa=a; pb=b; return K85Nav::None; }
        if (a && b && !sel_used && !chord) {
            chord=true; chord_done=false; chord_start=now; pending=false;
        }
        if (chord) {
            pa=a; pb=b;
            if (a && b && !chord_done && uint32_t(now-chord_start)>=K85_CHORD_MS) {
                chord_done=true; return K85Nav::EmergencyBack;
            }
            if (!a && !b) { chord=false; sel_used=false; nav_long=false; second=false; }
            return K85Nav::None;
        }
        bool sel=swap ? b:a, prevsel=swap ? pb:pa;
        bool nav=swap ? a:b, prevnav=swap ? pa:pb;
        if (legacy) { bool t=sel; sel=nav; nav=t; t=prevsel; prevsel=prevnav; prevnav=t; }
        K85Nav ev=K85Nav::None;
        if (nav && !prevnav) {
            nav_start=now; nav_long=false;
            second=pending && uint32_t(now-tap_end)<=K85_DOUBLE_MS;
            if (!second && pending) { pending=false; ev=legacy ? K85Nav::Select:K85Nav::Next; }
        }
        if (nav && !nav_long && uint32_t(now-nav_start)>=K85_LONG_MS) {
            nav_long=true; pending=false; second=false;
            ev=legacy ? K85Nav::Previous:K85Nav::Back;
        }
        if (!nav && prevnav && !nav_long) {
            if (second) { pending=false; ev=legacy ? K85Nav::Back:K85Nav::Previous; }
            else { pending=true; tap_end=now; }
            second=false;
        }
        // Legacy는 A 이동/B 선택을 보존한다. B 두 번은 취소이므로 선택을 지연한다.
        if (legacy) {
            if (!nav && prevnav && !nav_long) { pending=false; ev=K85Nav::Next; }
            if (!sel && prevsel) {
                if (pending && uint32_t(now-tap_end)<=K85_DOUBLE_MS) { pending=false; ev=K85Nav::Back; }
                else { pending=true; tap_end=now; }
            }
        } else if (!sel && prevsel) { sel_used=true; ev=K85Nav::Select; }
        if (pending && !nav && !sel && uint32_t(now-tap_end)>K85_DOUBLE_MS) {
            pending=false; ev=legacy ? K85Nav::Select:K85Nav::Next;
        }
        if (!a && !b) sel_used=false;
        pa=a; pb=b; return ev;
    }
};
// 용어: 디바운스=접점 떨림 제거, unsigned 시간차=시계가 한 바퀴 돌아도 유효한 간격.
