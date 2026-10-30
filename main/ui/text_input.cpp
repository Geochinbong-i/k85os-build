#include "text_input.h"
#include "core/text_edit.h"
#include "theme.h"
#include "input.h"
#include "config.h"
#include "core/control_motion.h"
#include "M5Unified.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

// 튜닝: 편집 메모리 상한 2047바이트, 센서 반복 220ms, 기울기 진입 0.25/해제 0.15.
// 센서 반복 간격을 줄이면 이동이 빨라진다. 버튼으로 모든 기능에 접근 가능하다.
static constexpr size_t EDIT_CAPACITY=2048;
static constexpr int IMU_REPEAT_MS=220;
static constexpr float IMU_ENTER=0.25f, IMU_LEAVE=0.15f;

static bool edit(const char *prompt,const char *initial,char *out,size_t size,
                 K85TextOptions opt,bool multiline) {
    if (!out || !size) return false;
    static char text[EDIT_CAPACITY];
    size_t limit=std::min(size-1,EDIT_CAPACITY-1);
    if (opt.max_bytes) limit=std::min(limit,opt.max_bytes);
    if (!k85edit::initial(text,limit+1,initial)) return false;
    int page=0,focus=0;
    bool shown=!opt.secret, dirty=true, tilted=false;
    int64_t last_step=0;
    const char *error="";
    k85_nav_reset();
    // 원리: 현재 페이지 문자와 명시 제어 키를 한 목록으로 연결해 빈 칸을 건너뛴다.
    while (true) {
        const int chars=std::strlen(k85edit::pages[page]);
        const char *controls[]={"abc","ABC","123","SYM1","SYM2","SP","DEL","OK","X",shown?"HIDE":"SHOW","RU","NL"};
        const int control_count=multiline?12:11, count=chars+control_count;
        if (dirty) {
            const int w=M5.Display.width(), h=M5.Display.height();
            const int cols=w<200?5:8, rows=(count+cols-1)/cols;
            const int top=55, cell_w=w/cols, cell_h=(h-top-12)/rows;
            M5.Display.fillScreen(k85_get_bg()); M5.Display.setTextSize(1);
            M5.Display.setTextColor(k85_get_fg(),k85_get_bg());
            M5.Display.setCursor(2,1); M5.Display.printf("%.*s [%s]",w/6-8,prompt?prompt:"Text",k85edit::names[page]);
            M5.Display.setCursor(2,12); M5.Display.printf("%u/%u bytes %s",unsigned(std::strlen(text)),unsigned(limit),error);
            char preview[40]; size_t len=std::strlen(text), n=std::min(len,size_t(w/6-1));
            if (shown) { std::memcpy(preview,text+len-n,n); preview[n]=0; }
            else { std::memset(preview,'*',n); preview[n]=0; }
            // 줄바꿈은 미리보기에서만 공백으로 표시하며 실제 편집값은 보존한다.
            for (size_t i=0;i<n;i++) if (preview[i]=='\n') preview[i]=' ';
            M5.Display.setCursor(2,25); M5.Display.print(preview);
            M5.Display.setCursor(2,39); M5.Display.print("OK:save X:cancel");
            for (int i=0;i<count;i++) {
                int x=(i%cols)*cell_w,y=top+(i/cols)*cell_h;
                bool selected=i==focus;
                uint32_t bg=selected?k85_get_accent():0x222222, fg=selected?0:k85_get_fg();
                M5.Display.fillRect(x+1,y+1,cell_w-2,cell_h-2,bg);
                M5.Display.setTextColor(fg,bg); M5.Display.setCursor(x+2,y+(cell_h-8)/2);
                if (i<chars) M5.Display.printf("%c",k85edit::pages[page][i]);
                else M5.Display.print(controls[i-chars]);
            }
            M5.Display.setTextColor(k85_get_fg(),k85_get_bg());
            M5.Display.setCursor(1,h-10); M5.Display.printf("%.*s",w/6,k85_nav_hint());
            dirty=false;
        }
        k85_input_update(); K85Nav ev=k85_nav_take();
        if (ev==K85Nav::Back || ev==K85Nav::EmergencyBack || k85_ab_held(500)) {
            k85_wait_ab_release(); std::memset(text,0,sizeof(text)); return false;
        }
        if (g_config.kbd_nav_mode==1) {
            K85Vec v; bool ok=k85_control_vector(v);
            if (ok) {
                int64_t now=esp_timer_get_time()/1000;
                float magnitude=std::fmax(std::fabs(v.x),std::fabs(v.y));
                if (magnitude<IMU_LEAVE) tilted=false;
                if (magnitude>IMU_ENTER && (!tilted || now-last_step>=IMU_REPEAT_MS)) {
                    int cols=M5.Display.width()<200?5:8;
                    int delta=std::fabs(v.x)>std::fabs(v.y)?(v.x>0?1:-1):(v.y>0?cols:-cols);
                    focus=(focus+delta+count)%count; last_step=now; tilted=true; dirty=true;
                }
            }
        }
        if (ev==K85Nav::Next || ev==K85Nav::Previous) {
            focus=(focus+(ev==K85Nav::Next?1:count-1))%count; dirty=true;
        }
        if (ev==K85Nav::Select) {
            error=""; dirty=true;
            if (focus<chars) {
                char add[]={k85edit::pages[page][focus],0};
                if (!k85edit::append(text,limit+1,add)) error="FULL";
            } else {
                int key=focus-chars;
                if (key<5) { page=key; focus=0; }
                else if (key==5) { if (!k85edit::append(text,limit+1," ")) error="FULL"; }
                else if (key==6) k85edit::erase(text);
                else if (key==7) {
                    if (k85edit::commit(text,out,size,opt.allow_empty)) {
                        std::memset(text,0,sizeof(text)); k85_nav_reset(); return true;
                    }
                    error="EMPTY";
                } else if (key==8) { std::memset(text,0,sizeof(text)); k85_nav_reset(); return false; }
                else if (key==9) shown=!shown;
                else if (key==10) error="RU unavailable";
                else if (key==11 && !k85edit::append(text,limit+1,"\n")) error="FULL";
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
// 용어: 포커스=현재 선택할 키, commit=명시 확정 때 호출자에게 복사.
bool k85_text_input(const char *p,const char *i,char *o,size_t s,const K85TextOptions &opt) { return edit(p,i,o,s,opt,false); }
bool k85_text_input(const char *p,const char *i,char *o,size_t s) { return edit(p,i,o,s,{},false); }
bool k85_text_input_multiline(const char *p,const char *i,char *o,size_t s) { return edit(p,i,o,s,{},true); }
void k85_area_show(const char *const lines[],int count,const char *title) {
    M5.Display.fillScreen(k85_get_bg()); M5.Display.setTextSize(1);
    M5.Display.setTextColor(k85_get_fg(),k85_get_bg()); M5.Display.setCursor(2,2); M5.Display.println(title);
    for (int i=0;i<count;i++) M5.Display.println(lines[i]);
    k85_nav_reset();
    while (true) {
        k85_input_update(); auto ev=k85_nav_take();
        if (ev==K85Nav::Back || ev==K85Nav::EmergencyBack || k85_ab_held(500)) { k85_wait_ab_release(); return; }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
