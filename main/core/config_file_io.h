#pragma once
#include <cstdio>
#include <unistd.h>

// 원리: 입출력 경계를 한곳에 두어 실 코드에 쓰기/flush/close/rename 실패를 주입할 수 있다.
struct K85ConfigIO {
    size_t (*write)(const void*,size_t,size_t,FILE*)=std::fwrite;
    int (*flush)(FILE*)=std::fflush;
    int (*sync)(int)=::fsync;
    int (*close)(FILE*)=std::fclose;
    int (*rename)(const char*,const char*)=std::rename;
};
inline K85ConfigIO &k85_config_io() { static K85ConfigIO ops; return ops; }
