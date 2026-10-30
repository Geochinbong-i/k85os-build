#pragma once
#include <cstddef>
#include <cstring>
#include <cstdio>
#include "mbedtls/gcm.h"
#include "mbedtls/aes.h"

// 원리: 새 비밀은 gcm2:nonce:cipher:tag 형식으로 인증해 손상/오키를 거절한다.
// 키·nonce 생성은 호출자 책임이며, 해독 실패를 평문으로 재해석하지 않는다.
namespace k85secret {
inline int nibble(char c) {
    if (c>='0'&&c<='9') return c-'0';
    if (c>='a'&&c<='f') return c-'a'+10;
    if (c>='A'&&c<='F') return c-'A'+10;
    return -1;
}
inline void hex(const unsigned char *p,size_t n,char *out) {
    const char *digits="0123456789abcdef";
    for (size_t i=0;i<n;i++) { out[2*i]=digits[p[i]>>4]; out[2*i+1]=digits[p[i]&15]; }
    out[n*2]=0;
}
inline bool unhex(const char *s,size_t n,unsigned char *out,size_t cap) {
    if ((n&1) || n/2>cap) return false;
    for(size_t i=0;i<n;i+=2) {
        int a=nibble(s[i]),b=nibble(s[i+1]); if(a<0||b<0) return false;
        out[i/2]=static_cast<unsigned char>((a<<4)|b);
    }
    return true;
}
inline bool encrypt(const char *plain,char *out,size_t cap,const unsigned char key[16],const unsigned char nonce[12]) {
    size_t n=std::strlen(plain),need=5+24+1+2*n+1+32+1;
    if (n>63 || cap<need) return false;
    unsigned char cipher[64]={},tag[16]={}; mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    int err=mbedtls_gcm_setkey(&ctx,MBEDTLS_CIPHER_ID_AES,key,128);
    if (!err) err=mbedtls_gcm_crypt_and_tag(&ctx,MBEDTLS_GCM_ENCRYPT,n,nonce,12,
        reinterpret_cast<const unsigned char*>("k85-secret-v2"),13,
        reinterpret_cast<const unsigned char*>(plain),cipher,16,tag);
    mbedtls_gcm_free(&ctx); if(err) return false;
    std::memcpy(out,"gcm2:",5); hex(nonce,12,out+5); out[29]=':';
    hex(cipher,n,out+30); out[30+2*n]=':'; hex(tag,16,out+31+2*n); return true;
}
inline bool decrypt(const char *encoded,char *out,size_t cap,const unsigned char key[16]) {
    if(!encoded || !out || !cap) return false;
    size_t len=std::strlen(encoded);
    unsigned char plain[80]={}; size_t n=0;
    if(std::strncmp(encoded,"gcm2:",5)==0) {
        if(len<63 || encoded[29]!=':') return false;
        const char *separator=std::strchr(encoded+30,':');
        if(!separator || std::strlen(separator+1)!=32) return false;
        size_t clen=separator-(encoded+30); n=clen/2;
        unsigned char nonce[12],cipher[64],tag[16];
        if(n>63 || !unhex(encoded+5,24,nonce,12) || !unhex(encoded+30,clen,cipher,64) || !unhex(separator+1,32,tag,16)) return false;
        mbedtls_gcm_context ctx; mbedtls_gcm_init(&ctx);
        int err=mbedtls_gcm_setkey(&ctx,MBEDTLS_CIPHER_ID_AES,key,128);
        if(!err) err=mbedtls_gcm_auth_decrypt(&ctx,n,nonce,12,
            reinterpret_cast<const unsigned char*>("k85-secret-v2"),13,tag,16,cipher,plain);
        mbedtls_gcm_free(&ctx); if(err) return false;
    } else {
        // Legacy CBC는 인증 태그가 없으며 엄격한 길이/HEX/PKCS7 검사까지만 가능하다.
        if(len<64 || len>192 || (len%32)) return false;
        unsigned char iv[16],cipher[80];
        if(!unhex(encoded,32,iv,16)||!unhex(encoded+32,len-32,cipher,80)) return false;
        size_t size=(len-32)/2;
        mbedtls_aes_context aes; mbedtls_aes_init(&aes);
        int err=mbedtls_aes_setkey_dec(&aes,key,128);
        if(!err) err=mbedtls_aes_crypt_cbc(&aes,MBEDTLS_AES_DECRYPT,size,iv,cipher,plain);
        mbedtls_aes_free(&aes); if(err) return false;
        unsigned char pad=plain[size-1]; if(!pad || pad>16 || pad>size) return false;
        for(size_t i=0;i<pad;i++) if(plain[size-1-i]!=pad) return false;
        n=size-pad; if(n>63) return false;
    }
    if(n>=cap || std::memchr(plain,0,n)) { std::memset(plain,0,sizeof(plain)); return false; }
    std::memcpy(out,plain,n); out[n]=0; std::memset(plain,0,sizeof(plain)); return true;
}
}
// 용어: GCM tag=변조 검증값, nonce=매 암호화마다 새로 생성할 12바이트 값, CBC=이전 비인증 암호 형식.
