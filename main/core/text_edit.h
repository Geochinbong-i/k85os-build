#pragma once
#include <cstddef>
#include <cstring>

// 원리: 편집은 임시 버퍼에만 하고 commit 성공 때에만 호출자 출력으로 복사한다.
// capacity는 널 종료를 포함한다. 최대 바이트를 넘는 추가는 전부 거절한다.
namespace k85edit {
inline bool initial(char *dst, size_t capacity, const char *value) {
    if (!dst || !capacity) return false;
    if (!value) value="";
    size_t n=std::strlen(value);
    if (n>=capacity) return false;
    std::memcpy(dst,value,n+1); return true;
}
inline bool append(char *dst, size_t capacity, const char *value) {
    size_t a=std::strlen(dst), b=std::strlen(value);
    if (a>=capacity || b>=capacity-a) return false;
    std::memcpy(dst+a,value,b+1); return true;
}
inline void erase(char *dst) {
    size_t n=std::strlen(dst);
    if (!n) return;
    --n;
    while (n && (static_cast<unsigned char>(dst[n])&0xc0)==0x80) --n;
    dst[n]=0;
}
inline bool commit(const char *src, char *out, size_t size, bool allow_empty) {
    if (!out || !size || (!allow_empty && !src[0]) || std::strlen(src)>=size) return false;
    std::memcpy(out,src,std::strlen(src)+1); return true;
}
inline bool wifi_valid(const char *ssid, const char *password) {
    if (!ssid || !ssid[0] || std::strlen(ssid)>32 || !password) return false;
    size_t n=std::strlen(password);
    return !n || (n>=8 && n<=63);
}
// 32개 기호를 빠짐없이 둘로 나눈다. 공백은 명시 SPACE 키로 제공한다.
constexpr const char *pages[]={"abcdefghijklmnopqrstuvwxyz", "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
    "0123456789", "!\"#$%&'()*+,-./:", ";<=>?@[\\]^_`{|}~"};
constexpr const char *names[]={"abc","ABC","123","SYM1","SYM2"};
}
// 용어: UTF-8=한 문자를 여러 바이트로 표현하는 형식, 널 종료=문자열 끝의 0바이트.
