#pragma once
#include "esp_timer.h"
#define pdMS_TO_TICKS(ms) (ms)
inline void vTaskDelay(int ticks) {host_us+=int64_t(ticks)*1000;}
