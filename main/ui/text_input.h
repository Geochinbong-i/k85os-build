#pragma once
#include <cstddef>
// 튜닝: max_bytes=0이면 out_size-1. secret은 기본 숨김, allow_empty는 빈 확정 허용.
struct K85TextOptions { bool secret=false; size_t max_bytes=0; bool allow_empty=true; };
// OK만 true이며 X/BACK/A+B는 false와 출력 불변. 과길이 입력은 추가 거절한다.
bool k85_text_input(const char*,const char*,char*,size_t,const K85TextOptions&);
bool k85_text_input(const char*,const char*,char*,size_t);
bool k85_text_input_multiline(const char*,const char*,char*,size_t);
void k85_area_show(const char *const[],int,const char*);
