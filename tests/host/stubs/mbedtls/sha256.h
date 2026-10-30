#pragma once
#include_next <mbedtls/sha256.h>
#include <mbedtls/version.h>
// Ubuntu 호스트의 mbedTLS 2.x 반환형을 IDF 5.3의 mbedTLS 3.x 이름에 맞춘다.
#if MBEDTLS_VERSION_MAJOR < 3
#define mbedtls_sha256 mbedtls_sha256_ret
#endif
