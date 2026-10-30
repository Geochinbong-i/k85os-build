#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "navigation.h"

// 한 화면에서 한 번 소비. raw 버튼 함수는 게임 등 기존 동작을 위해 유지한다.
K85Nav k85_nav_take();
bool k85_nav_is(K85Nav event);
bool k85_ui_cancel();
void k85_nav_reset();
const char *k85_nav_hint();

// Вызывать раз за цикл (обёртка над M5.update())
void k85_input_update(void);

bool k85_btn_a_pressed(void);   // wasPressed()
bool k85_btn_b_pressed(void);
bool k85_btn_a_is_down(void);   // isPressed()
bool k85_btn_b_is_down(void);

// Аналог check_ab_hold() из MicroPython: обе кнопки зажаты прямо сейчас
bool k85_check_ab_hold(void);

// Аналог ab_held(ms): обе кнопки удерживаются непрерывно >= ms миллисекунд.
// Нужно вызывать каждый цикл, копит время сама.
bool k85_ab_held(uint32_t ms);

// Аналог wait_ab_release(): блокирует, пока обе кнопки не отпустят
void k85_wait_ab_release(void);
