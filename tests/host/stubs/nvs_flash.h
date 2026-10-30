#pragma once
inline bool test_nvs_failure=false;
inline int nvs_flash_init() {return test_nvs_failure?-1:0;}
