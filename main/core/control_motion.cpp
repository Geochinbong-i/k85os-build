#include "control_motion.h"
#include "config.h"
#include "input.h"
#include "cursor.h"
#include "ui/list_menu.h"
#include "ui/common.h"
#include "M5Unified.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <cmath>

// 튜닝: 감도 배율 0.6/1/1.6, 데드존 0.04/0.09/0.16g, 평활 시정수 50ms.
// 감도를 올리면 빨라지고 데드존을 올리면 작은 기울기 반응이 줄어든다.
static K85Vec s_smoothed{0,0};
static int64_t s_last=0;
void k85_control_motion_reset() { s_smoothed={0,0}; s_last=0; }
bool k85_control_vector(K85Vec &out) {
    float ax=0,ay=0,az=0; out={0,0};
    if (!M5.Imu.getAccel(&ax,&ay,&az) || !std::isfinite(ax) || !std::isfinite(ay)) {
        k85_control_motion_reset(); return false;
    }
    int64_t now=esp_timer_get_time();
    float dt=s_last?k85_safe_dt(float(now-s_last)/1000000):0.03f; s_last=now;
    // IR 등 앱의 임시 회전도 실제 표시 방향을 따른다. 저장 설정은 바꾸지 않는다.
    int quarter=g_config.imu_follow_rotation ? ((int(M5.Display.getRotation())+3)&3):0;
    K85Vec v=k85_imu_transform(-ax-g_config.imu_neutral_x,ay-g_config.imu_neutral_y,quarter,
        g_config.imu_swap_xy,g_config.imu_invert_x,g_config.imu_invert_y);
    const float dead[]={0.04f,0.09f,0.16f},gain[]={0.6f,1.0f,1.6f};
    int d=g_config.imu_deadzone,s=g_config.imu_sensitivity;
    if (d<0 || d>2) d=1;
    if (s<0 || s>2) s=1;
    v.x=k85_imu_deadzone(v.x,dead[d])*gain[s]; v.y=k85_imu_deadzone(v.y,dead[d])*gain[s];
    float alpha=dt/(0.05f+dt);
    s_smoothed.x+=alpha*(v.x-s_smoothed.x); s_smoothed.y+=alpha*(v.y-s_smoothed.y);
    out=s_smoothed; return true;
}
// 원리: 64개 안정 샘플 평균을 중립으로 저장. 중력 자체를 0g 오류로 보지 않는다.
bool k85_control_calibrate() {
    constexpr int samples=64;
    float sx=0,sy=0,sxx=0,syy=0;
    k85_show_message("Hold normal pose\nKeep still 1.3s"); k85_nav_reset();
    for (int i=0;i<samples;i++) {
        k85_input_update(); auto ev=k85_nav_take();
        if (ev==K85Nav::Back || ev==K85Nav::EmergencyBack) return false;
        float x=0,y=0,z=0;
        if (!M5.Imu.getAccel(&x,&y,&z) || !std::isfinite(x) || !std::isfinite(y)) return false;
        x=-x; sx+=x; sy+=y; sxx+=x*x; syy+=y*y;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    float mx=sx/samples,my=sy/samples;
    if (sxx/samples-mx*mx>0.0025f || syy/samples-my*my>0.0025f) return false;
    g_config.imu_neutral_x=mx; g_config.imu_neutral_y=my; k85_control_motion_reset(); return true;
}
// 용어: g=중력가속도 기준 단위, 시정수=평활값이 새 측정값을 따라가는 시간 척도.
void k85_display_rotation(int absolute_rotation) {
    M5.Display.setRotation(absolute_rotation&3);
    k85_cursor_reset(); k85_control_motion_reset(); k85_nav_reset();
}

// 원리: 새 조작/회전을 미리 적용하되 10초 안에 확인되지 않으면 호출자가 원복한다.
static bool confirm_preview() {
    k85_nav_reset(); int64_t until=esp_timer_get_time()+10000000;
    while (esp_timer_get_time()<until) {
        M5.Display.fillScreen(0); M5.Display.setTextSize(1); M5.Display.setTextColor(0xffffff,0);
        M5.Display.setCursor(3,4); M5.Display.printf("Apply? %ds\n%s\nSelect:yes Back:no",
            int((until-esp_timer_get_time())/1000000+1),k85_nav_hint());
        k85_input_update(); auto ev=k85_nav_take();
        if (ev==K85Nav::Select) { k85_nav_reset(); return true; }
        if (ev==K85Nav::Back || ev==K85Nav::EmergencyBack) { k85_nav_reset(); return false; }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    k85_nav_reset(); return false;
}
static void reset_imu() {
    g_config.imu_follow_rotation=true; g_config.imu_swap_xy=false;
    g_config.imu_invert_x=false; g_config.imu_invert_y=false;
    g_config.imu_sensitivity=1; g_config.imu_deadzone=1;
    g_config.imu_neutral_x=0; g_config.imu_neutral_y=0; k85_control_motion_reset();
}
static void input_test(bool imu) {
    k85_nav_reset(); const char *name="None";
    while (true) {
        k85_input_update(); auto ev=k85_nav_take();
        if (ev==K85Nav::Back || ev==K85Nav::EmergencyBack) { k85_nav_reset(); return; }
        if (ev==K85Nav::Next) name="NEXT";
        if (ev==K85Nav::Previous) name="PREVIOUS";
        if (ev==K85Nav::Select) name="SELECT";
        M5.Display.fillScreen(0); M5.Display.setTextSize(1); M5.Display.setTextColor(0xffffff,0);
        M5.Display.setCursor(2,4); M5.Display.printf("Input test\n%s\nA:%d B:%d Event:%s\nSelect on RELEASE\nHold both 500ms:exit",
            k85_nav_hint(),int(k85_btn_a_is_down()),int(k85_btn_b_is_down()),name);
        if (imu) {
            K85Vec v; bool ok=k85_control_vector(v);
            M5.Display.printf("\nIMU:%s X:%.2f Y:%.2f",ok?"OK":"ERROR",double(v.x),double(v.y));
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
static void imu_menu() {
    while (true) {
        char row[7][50]; const char *items[10];
        std::snprintf(row[0],50,"Follow rotation: %s",g_config.imu_follow_rotation?"On":"Off");
        std::snprintf(row[1],50,"Swap X/Y: %s",g_config.imu_swap_xy?"On":"Off");
        std::snprintf(row[2],50,"Invert X: %s",g_config.imu_invert_x?"On":"Off");
        std::snprintf(row[3],50,"Invert Y: %s",g_config.imu_invert_y?"On":"Off");
        const char *levels[]={"Low","Normal","High"};
        std::snprintf(row[4],50,"Sensitivity: %s",levels[g_config.imu_sensitivity]);
        std::snprintf(row[5],50,"Deadzone: %s",levels[g_config.imu_deadzone]);
        for (int i=0;i<6;i++) items[i]=row[i];
        items[6]="Calibrate neutral"; items[7]="Live test"; items[8]="Reset IMU"; items[9]="Back";
        int n=k85_run_list_menu("IMU",items,10,nullptr);
        if (n<0 || n==9) return;
        if (n==0) g_config.imu_follow_rotation=!g_config.imu_follow_rotation;
        if (n==1) g_config.imu_swap_xy=!g_config.imu_swap_xy;
        if (n==2) g_config.imu_invert_x=!g_config.imu_invert_x;
        if (n==3) g_config.imu_invert_y=!g_config.imu_invert_y;
        if (n==4) g_config.imu_sensitivity=(g_config.imu_sensitivity+1)%3;
        if (n==5) g_config.imu_deadzone=(g_config.imu_deadzone+1)%3;
        if (n==6 && !k85_control_calibrate()) { k85_show_message("Unstable/IMU error\nRetry calibration"); vTaskDelay(pdMS_TO_TICKS(1400)); }
        if (n==7) input_test(true);
        if (n==8) reset_imu();
        k85_control_motion_reset(); if (!k85_config_save()) k85_show_message("Settings save FAILED");
    }
}
void k85_controls_menu() {
    while (true) {
        char nav[40],swap[40];
        std::snprintf(nav,sizeof(nav),"Navigation: %s",g_config.navigation_profile?"Legacy":"Bruce");
        std::snprintf(swap,sizeof(swap),"Swap A/B: %s",g_config.navigation_swap?"On":"Off");
        const char *items[]={nav,swap,"Input test","IMU","Reset controls","Back"};
        int n=k85_run_list_menu("Controls",items,6,nullptr);
        if (n<0 || n==5) return;
        struct ControlsState {
            int profile,sensitivity,deadzone; bool swap,follow,xy,ix,iy; float nx,ny;
            void restore() const {
                g_config.navigation_profile=profile; g_config.navigation_swap=swap;
                g_config.imu_sensitivity=sensitivity; g_config.imu_deadzone=deadzone;
                g_config.imu_follow_rotation=follow; g_config.imu_swap_xy=xy;
                g_config.imu_invert_x=ix; g_config.imu_invert_y=iy;
                g_config.imu_neutral_x=nx; g_config.imu_neutral_y=ny;
            }
        } old{g_config.navigation_profile,g_config.imu_sensitivity,g_config.imu_deadzone,
            g_config.navigation_swap,g_config.imu_follow_rotation,g_config.imu_swap_xy,
            g_config.imu_invert_x,g_config.imu_invert_y,g_config.imu_neutral_x,g_config.imu_neutral_y};
        if (n==0 || n==1) {
            if (n==0) g_config.navigation_profile=1-g_config.navigation_profile;
            else g_config.navigation_swap=!g_config.navigation_swap;
            if (!confirm_preview()) old.restore();
        }
        if (n==2) input_test(false);
        if (n==3) imu_menu();
        if (n==4) {
            g_config.navigation_profile=0; g_config.navigation_swap=false; reset_imu();
            if (!confirm_preview()) old.restore();
        }
        k85_nav_reset(); if (!k85_config_save()) { old.restore(); k85_show_message("Settings save FAILED"); }
    }
}
void k85_display_menu() {
    const char *items[]={"Rotation 0","Rotation 90","Rotation 180","Rotation 270","Back"};
    int n=k85_run_list_menu("Display",items,5,nullptr);
    if (n<0 || n==4) return;
    int old=g_config.rotation, proposed=(n+1)&3;
    k85_display_rotation(proposed);
    if (confirm_preview()) {
        g_config.rotation=proposed;
        if (!k85_config_save()) { g_config.rotation=old; k85_show_message("Settings save FAILED"); }
    }
    k85_display_rotation(g_config.rotation);
}
