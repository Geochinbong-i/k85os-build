#pragma once
#include <cstddef>
#include <cstring>
#include "nvs_flash.h"
using nvs_handle_t=int;
constexpr int NVS_READWRITE=1,ESP_ERR_NVS_NOT_FOUND=17;
inline unsigned char test_pepper[16]={};
inline bool test_have_pepper=false;
inline int nvs_open(const char*,int,int *out) {*out=1;return test_nvs_failure?-1:0;}
inline int nvs_get_blob(int,const char*,void *out,size_t *n) {if(!test_have_pepper)return ESP_ERR_NVS_NOT_FOUND;*n=16;memcpy(out,test_pepper,16);return 0;}
inline int nvs_set_blob(int,const char*,const void *p,size_t n) {if(test_nvs_failure)return -1;memcpy(test_pepper,p,n);test_have_pepper=true;return 0;}
inline int nvs_commit(int) {return test_nvs_failure?-1:0;}
inline void nvs_close(int) {}
