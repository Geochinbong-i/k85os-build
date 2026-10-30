#include "input.h"
#include "config.h"
#include "M5Unified.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static K85Navigation s_navigation;
static K85Nav s_nav_event=K85Nav::None;
void k85_input_update(void) {
    M5.update();
    s_nav_event=s_navigation.step(now_ms(),M5.BtnA.isPressed(),M5.BtnB.isPressed(),
        g_config.navigation_profile==1,g_config.navigation_swap);
}
K85Nav k85_nav_take() { K85Nav ev=s_nav_event; s_nav_event=K85Nav::None; return ev; }
bool k85_nav_is(K85Nav ev) { if(s_nav_event!=ev) return false; s_nav_event=K85Nav::None; return true; }
bool k85_ui_cancel() { return k85_nav_is(K85Nav::Back)||k85_nav_is(K85Nav::EmergencyBack)||k85_ab_held(500); }
void k85_nav_reset() { s_navigation.reset(); s_nav_event=K85Nav::None; }
const char *k85_nav_hint() {
    if (g_config.navigation_profile==1)
        return g_config.navigation_swap ? "B:next A:select x2:back" : "A:next B:select x2:back";
    return g_config.navigation_swap ? "B:select A:next x2:prev hold:back" : "A:select B:next x2:prev hold:back";
}

// K85CH1: a press made while the other button is held belongs to the A+B gesture, not to a single button
bool k85_btn_a_pressed(void) { return M5.BtnA.wasPressed() && !M5.BtnB.isPressed(); }
bool k85_btn_b_pressed(void) { return M5.BtnB.wasPressed() && !M5.BtnA.isPressed(); }
bool k85_btn_a_is_down(void) { return M5.BtnA.isPressed(); }
bool k85_btn_b_is_down(void) { return M5.BtnB.isPressed(); }

bool k85_check_ab_hold(void) {
    return M5.BtnA.isPressed() && M5.BtnB.isPressed();
}

static uint32_t s_ab_hold_start = 0;

bool k85_ab_held(uint32_t ms) {
    if (k85_check_ab_hold()) {
        if (s_ab_hold_start == 0) s_ab_hold_start = now_ms();
        return (now_ms() - s_ab_hold_start) >= ms;
    }
    s_ab_hold_start = 0;
    return false;
}

void k85_wait_ab_release(void) {
    while (M5.BtnA.isPressed() || M5.BtnB.isPressed()) {
        M5.update();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    s_ab_hold_start = 0;
    k85_nav_reset();
}
