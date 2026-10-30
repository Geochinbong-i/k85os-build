#include "ui/text_input.h"
#include "core/input.h"
#include "core/text_edit.h"
#include "core/config.h"
#include "core/control_motion.h"
#include "M5Unified.h"
#include <deque>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

k85_config_t g_config={};
static std::deque<K85Nav> gestures;
unsigned k85_get_bg() {return 0;} unsigned k85_get_fg() {return 0xffffff;} unsigned k85_get_accent() {return 0x00ffff;}
void k85_input_update() {assert(!gestures.empty());}
K85Nav k85_nav_take() {auto event=gestures.front();gestures.pop_front();return event;}
void k85_nav_reset() {}
const char *k85_nav_hint() {return "A:select B:next";}
bool k85_ab_held(uint32_t) {return false;} void k85_wait_ab_release() {}
bool k85_control_vector(K85Vec &out) {out={0,0};return false;}
void k85_control_motion_reset() {}
static void next(int count) {for(int i=0;i<count;i++) gestures.push_back(K85Nav::Next);}
static void select() {gestures.push_back(K85Nav::Select);}
static void all_ascii() {
    // 생산 키보드의 실제 포커스/페이지 전환을 실행한다. IMU 입력은 사용하지 않는다.
    std::string expected;
    for(int page=0;page<5;page++) {
        int n=std::strlen(k85edit::pages[page]);
        for(int i=0;i<n;i++) {if(i)next(1);select();expected+=k85edit::pages[page][i];}
        if(page<4) {next(page+2);select();} // 현재 마지막 문자→다음 페이지 제어 키.
        else {next(6);select();expected+=' ';next(2);select();}
    }
    char out[256]="sentinel";
    assert(k85_text_input("ASCII", "",out,sizeof(out)));
    assert(expected==out);assert(expected.size()==95);assert(gestures.empty());
}
static void cancel_and_limits() {
    for(auto event:{K85Nav::Back,K85Nav::EmergencyBack}) {
        char out[64]="unchanged";gestures.push_back(event);
        assert(!k85_text_input("Password","ONLY_SECRET_VALUE_982!",out,sizeof(out),K85TextOptions{true,63,true}));
        assert(!strcmp(out,"unchanged"));assert(gestures.empty());
        assert(M5.Display.trace.find("ONLY_SECRET_VALUE_982!")==std::string::npos);
    }
    char out[64]="unchanged";next(34);select(); // X 키: 26문자+제어 인덱스 8.
    assert(!k85_text_input("X", "",out,sizeof(out)));assert(!strcmp(out,"unchanged"));
    std::string max(63,'z');select();next(33);select();
    assert(k85_text_input("Full",max.c_str(),out,sizeof(out)));assert(max==out);assert(gestures.empty());
    std::strcpy(out,"unchanged");
    assert(!k85_text_input("Too long",std::string(64,'z').c_str(),out,sizeof(out)));assert(!strcmp(out,"unchanged"));
    next(37);select();for(int i=0;i<4;i++)gestures.push_back(K85Nav::Previous);select();
    assert(k85_text_input_multiline("Multiline","",out,sizeof(out)));assert(!strcmp(out,"\n"));assert(gestures.empty());
}
int main() {
    for(int rotation=0;rotation<4;rotation++) {
        M5.Display.w=(rotation&1)?240:135;M5.Display.h=(rotation&1)?135:240;
        all_ascii();M5.Display.trace.clear();cancel_and_limits();M5.Display.trace.clear();
        g_config.kbd_nav_mode=1;all_ascii();g_config.kbd_nav_mode=0;M5.Display.trace.clear();
    }
    std::cout<<"UI: production keyboard entered all 95 ASCII bytes using buttons, cancel/output invariance, secret masking, full/initial limits, multiline; 4 rotations with geometry checks passed\n";
}
