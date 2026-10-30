#pragma once
#include <cstdint>
inline int64_t host_us=0;
inline int64_t esp_timer_get_time() {return host_us;}
