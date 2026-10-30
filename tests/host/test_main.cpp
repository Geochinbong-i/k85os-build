#include "core/navigation.h"
#include "core/text_edit.h"
#include "core/imu_math.h"
#include "core/secret_store.h"
#include "core/config.h"
#include "core/config_file_io.h"
#include "core/fs_guard.h"
#include "nvs_flash.h"
#include <cassert>
#include <cstring>
#include <string>
#include <set>
#include <fstream>
#include <iostream>
#include <iterator>
#include <unistd.h>
#include <climits>

void k85_profiles_defaults(k85_profile_t *p) {std::memset(p,0,sizeof(k85_profile_t)*K85_MAX_PROFILES);}
static void navigation() {
    K85Navigation n; n.step(0,0,0);
    assert(n.step(10,0,1)==K85Nav::None); assert(n.step(50,0,0)==K85Nav::None);
    assert(n.step(300,0,0)==K85Nav::None); assert(n.step(301,0,0)==K85Nav::Next);
    assert(n.step(900,0,0)==K85Nav::None);
    n.reset(); n.step(0,0,0); n.step(10,0,1); n.step(30,0,0); n.step(100,0,1);
    assert(n.step(120,0,0)==K85Nav::Previous); assert(n.step(500,0,0)==K85Nav::None);
    n.reset();n.step(0,0,0);n.step(10,0,1);
    assert(n.step(609,0,1)==K85Nav::None); assert(n.step(610,0,1)==K85Nav::Back);
    assert(n.step(900,0,1)==K85Nav::None); assert(n.step(910,0,0)==K85Nav::None);
    n.reset();n.step(0,0,0);n.step(10,1,0);n.step(20,1,1);
    assert(n.step(519,1,1)==K85Nav::None); assert(n.step(520,1,1)==K85Nav::EmergencyBack);
    assert(n.step(530,1,0)==K85Nav::None); assert(n.step(540,0,0)==K85Nav::None);
    assert(n.step(900,0,0)==K85Nav::None);
    n.reset();n.step(0,0,0);n.step(10,1,0);n.step(20,1,1);assert(n.step(40,0,0)==K85Nav::None);
    n.reset();n.step(0,1,1);n.step(500,1,0);assert(n.step(700,0,0)==K85Nav::None);
    n.step(800,1,0);assert(n.step(820,0,0)==K85Nav::Select);
    n.reset();n.step(UINT_MAX-100,0,0);n.step(UINT_MAX-90,0,1);n.step(UINT_MAX-50,0,0);
    assert(n.step(201,0,0)==K85Nav::Next);
    n.reset();n.step(0,0,0,false,true);n.step(10,0,1,false,true);assert(n.step(30,0,0,false,true)==K85Nav::Select);
    n.reset();n.step(0,0,0,true);n.step(10,1,0,true);assert(n.step(30,0,0,true)==K85Nav::Next);
    n.step(100,0,1,true);n.step(120,0,0,true);assert(n.step(371,0,0,true)==K85Nav::Select);
    n.reset();n.step(0,0,0,true);n.step(10,0,1,true);n.step(30,0,0,true);n.step(80,0,1,true);
    assert(n.step(100,0,0,true)==K85Nav::Back); assert(n.step(700,0,0,true)==K85Nav::None);
    n.reset();n.step(0,0,0);n.step(10,0,1);n.step(30,0,0);n.step(60,1,0);
    assert(n.step(80,0,0)==K85Nav::None);assert(n.step(281,0,0)==K85Nav::Next);
    assert(n.step(282,0,0)==K85Nav::Select);assert(n.step(600,0,0)==K85Nav::None);
    n.reset();n.step(0,0,0,true);n.step(10,0,1,true);n.step(30,0,0,true);n.step(60,1,0,true);
    assert(n.step(80,0,0,true)==K85Nav::Next);assert(n.step(281,0,0,true)==K85Nav::Select);
    std::cout<<"BTN: single/double/long/chord/reset/wrap/swap/Legacy passed\n";
}
static void text() {
    std::set<char> chars{' '};
    for(auto page:k85edit::pages) for(const char *p=page;*p;p++) chars.insert(*p);
    assert(chars.size()==95); for(int i=32;i<=126;i++) assert(chars.count(char(i)));
    char buf[96],out[96]="unchanged"; assert(k85edit::initial(buf,sizeof(buf),""));
    for(char c:chars) {char one[]={c,0};assert(k85edit::append(buf,sizeof(buf),one));}
    assert(!k85edit::append(buf,sizeof(buf),"!"));assert(std::strlen(buf)==95);
    assert(!k85edit::commit(buf,out,95,true));assert(!std::strcmp(out,"unchanged"));
    assert(k85edit::commit(buf,out,96,true));assert(!std::strcmp(out,buf));
    assert(k85edit::initial(buf,sizeof(buf),u8"a가"));k85edit::erase(buf);assert(!std::strcmp(buf,"a"));
    k85edit::erase(buf);k85edit::erase(buf);assert(!buf[0]);
    assert(!k85edit::commit(buf,out,96,false));assert(k85edit::commit(buf,out,96,true));
    assert(!k85edit::initial(buf,2,"abc"));assert(!k85edit::commit("a",nullptr,10,true));
    for(const char *s:{"Sugar!2026","Abc!@#123","a\"b\\c!23"," abc!123 "}) {
        assert(k85edit::initial(buf,sizeof(buf),s));assert(k85edit::commit(buf,out,sizeof(out),true));assert(!strcmp(s,out));
    }
    std::string max(63,'a'),too(64,'a');
    assert(k85edit::wifi_valid("AP",max.c_str()));assert(!k85edit::wifi_valid("AP",too.c_str()));
    assert(k85edit::wifi_valid("AP",""));assert(!k85edit::wifi_valid("AP","short"));
    assert(!k85edit::wifi_valid(std::string(33,'s').c_str(),max.c_str()));
    assert(k85_is_blocked_path("/littlefs/k85os_config.json.tmp"));
    assert(k85_is_blocked_path("/littlefs/k85os_config.json.bak"));
    assert(k85_is_blocked_path("/littlefs/k85os_config.json.manual.bak"));
    std::cout<<"TXT: 95 ASCII, exact bytes, buffer limits, UTF-8 delete, Wi-Fi lengths, protected paths passed\n";
}
static void imu() {
    const K85Vec expected[]={{1,2},{-2,1},{-1,-2},{2,-1}};
    for(int r=0;r<4;r++)for(int swap=0;swap<2;swap++)for(int ix=0;ix<2;ix++)for(int iy=0;iy<2;iy++) {
        auto v=k85_imu_transform(1,2,r,swap,ix,iy),e=expected[r];
        if(swap)std::swap(e.x,e.y);if(ix)e.x=-e.x;if(iy)e.y=-e.y;
        assert(v.x==e.x&&v.y==e.y);
    }
    assert(k85_imu_deadzone(0.05f,0.1f)==0);assert(k85_imu_deadzone(-0.2f,0.1f)<0);
    assert(k85_safe_dt(5)==0.1f);assert(k85_safe_dt(-1)==0);
    std::cout<<"IMU: 32 rotation/swap/inversion combinations, deadzone, dt bounds passed\n";
}
static void secrets() {
    unsigned char key[16]={},nonce[12]={};char encoded[192],out[64]="untouched";
    for(int size:{0,8,63}) {
        std::string plain(size,'!'); assert(k85secret::encrypt(plain.c_str(),encoded,sizeof(encoded),key,nonce));
        assert(k85secret::decrypt(encoded,out,sizeof(out),key));assert(plain==out);
        encoded[std::strlen(encoded)-1]=(encoded[std::strlen(encoded)-1]=='a'?'b':'a');
        std::strcpy(out,"untouched");assert(!k85secret::decrypt(encoded,out,sizeof(out),key));assert(!strcmp(out,"untouched"));
    }
    assert(!k85secret::encrypt(std::string(64,'a').c_str(),encoded,sizeof(encoded),key,nonce));
    assert(!k85secret::encrypt("test!123",encoded,10,key,nonce));
    assert(!k85secret::decrypt(std::string(64,'z').c_str(),out,sizeof(out),key));
    // 이전 CBC 형식의 독립 암호화 fixture로 마이그레이션 해독을 검증한다.
    unsigned char plain[16]={},cipher[16],iv[16]={},ivcopy[16]={};
    std::memcpy(plain,"Test!123",8);std::memset(plain+8,8,8);
    mbedtls_aes_context aes;mbedtls_aes_init(&aes);mbedtls_aes_setkey_enc(&aes,key,128);
    assert(!mbedtls_aes_crypt_cbc(&aes,MBEDTLS_AES_ENCRYPT,16,ivcopy,plain,cipher));mbedtls_aes_free(&aes);
    k85secret::hex(iv,16,encoded);k85secret::hex(cipher,16,encoded+32);
    assert(k85secret::decrypt(encoded,out,sizeof(out),key));assert(!strcmp(out,"Test!123"));
    std::cout<<"SEC: GCM round trip/tag corruption/length/HEX and legacy CBC migration passed\n";
}
static std::string read(const char *path) {std::ifstream f(path);return {std::istreambuf_iterator<char>(f),{}};}
static void write(const char *path,const std::string &value) {std::ofstream f(path);f<<value;}
static int fault=0,renames=0,writes=0;
static size_t fail_write(const void *p,size_t a,size_t b,FILE *f) {++writes;return fault==1?0:std::fwrite(p,a,b,f);}
static int fail_flush(FILE *f) {int rc=std::fflush(f);return fault==2?-1:rc;}
static int fail_sync(int f) {return fault==3?-1:fsync(f);}
static int fail_close(FILE *f) {int rc=std::fclose(f);return fault==4?-1:rc;}
static int fail_rename(const char *a,const char *b) {++renames;return (fault==5 || (fault==6&&renames==2))?-1:std::rename(a,b);}
static void storage() {
    char dir[]="/tmp/k85-host-XXXXXX";assert(mkdtemp(dir));assert(!chdir(dir));
    assert(k85_fs_init());assert(!k85_config_load());
    write("test-config.json",R"({"wifi":{"ssid":"TestAP","password":"Fake!2026","saved":true},"wifi_networks":[{"ssid":"TestAP2","password":"Fake!222"}],"mqtt_password":"FakeMqtt!","high_scores":{"snake":7},"custom":{"x":42},"rotation":99})");
    assert(k85_config_load());assert(g_config.high_scores.snake==7);assert(g_config.rotation==1);
    assert(!strcmp(g_config.wifi_password,"Fake!2026"));assert(g_config.wifi_networks_count==1);assert(!strcmp(g_config.mqtt_password,"FakeMqtt!"));
    g_config.brightness_active=81;assert(k85_config_save());
    auto file=read("test-config.json");assert(file.find("Fake!2026")==std::string::npos);assert(file.find("FakeMqtt!")==std::string::npos);
    assert(file.find("gcm2:")!=std::string::npos);assert(file.find("\"custom\":{\"x\":42}")!=std::string::npos);
    assert(k85_config_load());assert(!strcmp(g_config.wifi_password,"Fake!2026"));assert(!strcmp(g_config.mqtt_password,"FakeMqtt!"));
    assert(g_config.wifi_networks_count==1);assert(!strcmp(g_config.wifi_networks[0].password,"Fake!222"));
    auto &io=k85_config_io();io={fail_write,fail_flush,fail_sync,fail_close,fail_rename};
    writes=0;assert(k85_config_save());assert(writes==0);assert(read("test-config.json")==file);
    for(int f=1;f<=6;f++) {
        assert(k85_config_load());auto original=g_config.brightness_active;g_config.brightness_active=original+1;
        fault=f;renames=0;assert(!k85_config_save());fault=0;
        assert(k85_config_load());assert(g_config.brightness_active==original);assert(g_config.high_scores.snake==7);
        // rename 실패로 main이 없으면 다음 정상 저장으로 복구본을 다시 정상본으로 승격한다.
        g_config.brightness_active=82;assert(k85_config_save());g_config.brightness_active=81;assert(k85_config_save());
    }
    file=read("test-config.json");test_nvs_failure=true;g_config.brightness_active=88;
    assert(!k85_config_save());assert(read("test-config.json")==file);
    assert(k85_config_load());assert(!g_config.wifi_saved);assert(g_config.high_scores.snake==7);
    test_nvs_failure=false;assert(k85_config_load());assert(g_config.wifi_saved);
    // 손상된 최신본을 제외하고 직전 정상 세대를 선택한다.
    write("test-config.json","{broken");assert(k85_config_load());assert(g_config.high_scores.snake==7);
    g_config.brightness_active=83;assert(k85_config_save());
    file=read("test-config.json");write("test-config.json.tmp",file);write("test-config.json","{broken");
    assert(k85_config_load());assert(g_config.brightness_active==83);
    // 체크섬 손상은 유효 JSON이어도 거절한다.
    auto pos=file.find("\"brightness_active\":83");assert(pos!=std::string::npos);
    file.replace(pos,std::strlen("\"brightness_active\":83"),"\"brightness_active\":84");
    write("test-config.json.tmp",file);assert(k85_config_load());assert(g_config.brightness_active!=84);
    assert(k85_config_load());g_config.brightness_active=71;assert(k85_config_save());
    write("manual-backup.json",read("test-config.json"));g_config.brightness_active=91;assert(k85_config_save());
    assert(k85_config_restore_file("manual-backup.json"));assert(g_config.brightness_active==71);
    assert(k85_config_load());assert(g_config.brightness_active==71);
    write("bad-backup.json","{broken");assert(!k85_config_restore_file("bad-backup.json"));assert(g_config.brightness_active==71);
    std::cout<<"CFG: legacy import/unknown keys/secrets, no-op save, six I/O failure points, NVS failure, corrupt main/tmp recovery passed\n";
}
int main() {navigation();text();imu();secrets();storage();std::cout<<"All host checks passed\n";}
