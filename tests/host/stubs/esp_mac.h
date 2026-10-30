#pragma once
#include <cstdint>
enum {ESP_MAC_WIFI_STA};
inline int esp_read_mac(uint8_t *out,int) {for(int i=0;i<6;i++)out[i]=i;return 0;}
