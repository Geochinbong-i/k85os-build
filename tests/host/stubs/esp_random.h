#pragma once
#include <cstddef>
// 재현 가능한 호스트 시험 전용. 펌웨어는 실제 esp_fill_random을 링크한다.
inline void esp_fill_random(void *p,size_t n) {static unsigned char v=1;auto out=static_cast<unsigned char*>(p);for(size_t i=0;i<n;i++)out[i]=v++;}
