#include "config.h"
#include "core/profiles.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_log.h"
#include "esp_littlefs.h"
#include "cJSON.h"
#include "mbedtls/aes.h"
#include "mbedtls/sha256.h"
#include "esp_wifi.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_mac.h"
#include "secret_store.h"
#include "config_file_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <cmath>
#include <algorithm>

static const char *TAG = "k85_cfg";

// ============================ LittleFS ============================
bool k85_fs_init(void) {
    esp_vfs_littlefs_conf_t conf = {
        .base_path = K85_LITTLEFS_BASE_PATH,
        .partition_label = K85_LITTLEFS_PART_LABEL,
        .format_if_mount_failed = false,
        .dont_mount = false,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS mount failed: %s", esp_err_to_name(err));
        return false;
    }
    if (!esp_littlefs_mounted(K85_LITTLEFS_PART_LABEL)) {
        ESP_LOGE(TAG, "LittleFS not mounted");
        return false;
    }
    size_t total = 0, used = 0;
    if (esp_littlefs_info(K85_LITTLEFS_PART_LABEL, &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "LittleFS OK, used=%uB / total=%uB", (unsigned)used, (unsigned)total);
    }
    return true;
}

bool k85_fs_info(size_t *total_bytes, size_t *used_bytes) {
    return esp_littlefs_info(K85_LITTLEFS_PART_LABEL, total_bytes, used_bytes) == ESP_OK;
}

k85_config_t g_config;

// ============================ defaults ============================
void k85_config_defaults(k85_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->theme_idx        = 0;
    cfg->battery_mode_idx = 1;
    cfg->brightness_active = 80;
    cfg->device_name_idx  = 0;
    cfg->rotation         = 1;
    cfg->navigation_profile=0; cfg->imu_follow_rotation=true;
    cfg->imu_sensitivity=1; cfg->imu_deadzone=1;
    cfg->bootstyle_idx    = 0;
    cfg->kb_layout        = 0;
    cfg->sound_volume     = 20;
    cfg->utc_offset       = 3; // MSK по умолчанию
    cfg->ota_locked = false;
    cfg->wifi_disabled = false;
    cfg->bt_disabled = false;
    cfg->ota_bg_check_enabled = true;
    cfg->mqtt_autostart = false;
    cfg->lock_enabled = false;
    cfg->lock_password[0] = 0;
    cfg->post_beep_enabled = true;
    cfg->alarm_volume_idx = 2; // High по умолчанию
    cfg->sound_muted = false;
    cfg->sb_flags = 0x3; // по умолчанию: battery% + time
    cfg->sb_bg_color = 0xFFFFFFFF;
    cfg->ap_channel = 1;
    cfg->ap_open = false;
    cfg->bg_gradient_enabled = false;
    cfg->menu_ui_style = 0;
    cfg->bios_ui_style = 0;
    cfg->sleep_wake_mode = 0;
    cfg->lock_shape = 0;
    cfg->lock_particle_color = 0xFFFFFFFF;
    cfg->grub_enabled = true;
    cfg->default_boot_choice = 0;
    cfg->bios_bg_color = 0xFFFFFFFF;
    cfg->bios_hl_color = 0xFFFFFFFF;
    cfg->bios_text_color = 0xFFFFFFFF;
    cfg->bios_selection_style = 0;
    cfg->wifi_saved       = false;
    cfg->wifi_networks_count = 0;

    cfg->ssh_enabled = false;
    cfg->ssh_username[0] = 0;
    cfg->ssh_password_hash[0] = 0;

    k85_profiles_defaults(cfg->profiles);
    cfg->active_profile_idx = 0;
    cfg->totp_entries_count = 0;
    cfg->pwmgr_master_verifier[0] = 0;
    cfg->pw_entries_count = 0;
    cfg->mqtt_broker_uri[0] = 0;
    cfg->mqtt_client_id[0] = 0;
    cfg->mqtt_username[0] = 0;
    cfg->mqtt_password[0] = 0;
    cfg->font_idx = 0;
    // high_scores, step_count, step_record, step_date вЂ” СѓР¶Рµ 0/""
}

// ============================ JSON <-> struct ============================

// ============================ Шифрование паролей WiFi на диске ============================
// Ключ выводится из MAC-адреса устройства (SHA-256), пароли хранятся в JSON
// зашифрованными (AES-128-CBC), в рантайме g_config всегда содержит plaintext
// (нужен для esp_wifi_connect). Честно: ключ вычисляется тем же алгоритмом,
// что известен из исходников прошивки — это защита от простого чтения JSON
// "как есть", а не от продвинутого реверс-инжиниринга дампа флеша целиком.
// Секретная добавка к ключу - генерируется один раз на устройстве и
// хранится в NVS (не в LittleFS, недостижима через cat/ls/веб-менеджер).
// Без неё ключ AES был бы полностью выводим из MAC-адреса + публичной
// строки в открытом исходном коде - то есть вычислим кем угодно.
// 키 생성 실패는 저장 실패다. NVS 전체 삭제나 0으로 채운 키로 진행하지 않는다.
static bool get_aes_key(unsigned char key_out[16]) {
    uint8_t mac[6]; if(esp_read_mac(mac,ESP_MAC_WIFI_STA)!=ESP_OK) return false;
    if(nvs_flash_init()!=ESP_OK) return false;
    nvs_handle_t h; if(nvs_open("k85_sec",NVS_READWRITE,&h)!=ESP_OK) return false;
    unsigned char pepper[16]; size_t len=sizeof(pepper);
    esp_err_t err=nvs_get_blob(h,"wifi_pepper",pepper,&len);
    if(err==ESP_ERR_NVS_NOT_FOUND) {
        esp_fill_random(pepper,sizeof(pepper));
        err=nvs_set_blob(h,"wifi_pepper",pepper,sizeof(pepper));
        if(err==ESP_OK) err=nvs_commit(h);
    }
    nvs_close(h); if(err!=ESP_OK || len!=16) return false;
    const char salt[]="k85os-wifi-store-v1";
    unsigned char input[6+sizeof(salt)+16],hash[32];
    memcpy(input,mac,6); memcpy(input+6,salt,sizeof(salt)); memcpy(input+6+sizeof(salt),pepper,16);
    int rc=mbedtls_sha256(input,sizeof(input),hash,0);
    if(!rc) memcpy(key_out,hash,16);
    memset(pepper,0,sizeof(pepper)); memset(input,0,sizeof(input)); memset(hash,0,sizeof(hash));
    return rc==0;
}
static bool encrypt_to_hex(const char *plain,char *out,size_t size) {
    if(!plain[0]) { if(!size) return false; out[0]=0; return true; }
    unsigned char key[16],nonce[12]; if(!get_aes_key(key)) return false;
    esp_fill_random(nonce,sizeof(nonce)); bool ok=k85secret::encrypt(plain,out,size,key,nonce);
    memset(key,0,sizeof(key)); return ok;
}
static bool restore_secret(const char *encoded,char *out,size_t size) {
    if(!out || !size) return false; out[0]=0;
    if(!encoded) return false;
    size_t n=strlen(encoded);
    if(!n) return true;
    if(strncmp(encoded,"gcm2:",5) && n<=63) {
        // 명시 버전 접두사는 모르는 형식이면 평문으로 오인하지 않는다.
        if(strchr(encoded,':') && !strncmp(encoded,"gcm",3)) return false;
        if(n>=size) return false; memcpy(out,encoded,n+1); return true;
    }
    unsigned char key[16]; if(!get_aes_key(key)) return false;
    bool ok=k85secret::decrypt(encoded,out,size,key); memset(key,0,sizeof(key)); return ok;
}
static void set_str(char *dst, size_t dst_sz, const char *src) {
    if (src) snprintf(dst, dst_sz, "%s", src);
}

static cJSON *cfg_to_json(const k85_config_t *c) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddNumberToObject(root, "theme_idx", c->theme_idx);
    cJSON_AddNumberToObject(root, "battery_mode_idx", c->battery_mode_idx);
    cJSON_AddNumberToObject(root, "brightness_active", c->brightness_active);
    cJSON_AddNumberToObject(root, "device_name_idx", c->device_name_idx);
    cJSON_AddNumberToObject(root, "rotation", c->rotation);
    cJSON_AddNumberToObject(root,"navigation_profile",c->navigation_profile);
    cJSON_AddNumberToObject(root,"imu_sensitivity",c->imu_sensitivity);
    cJSON_AddNumberToObject(root,"imu_deadzone",c->imu_deadzone);
    cJSON_AddNumberToObject(root,"imu_neutral_x",c->imu_neutral_x);
    cJSON_AddNumberToObject(root,"imu_neutral_y",c->imu_neutral_y);
    cJSON_AddBoolToObject(root,"navigation_swap",c->navigation_swap);
    cJSON_AddBoolToObject(root,"imu_follow_rotation",c->imu_follow_rotation);
    cJSON_AddBoolToObject(root,"imu_swap_xy",c->imu_swap_xy);
    cJSON_AddBoolToObject(root,"imu_invert_x",c->imu_invert_x);
    cJSON_AddBoolToObject(root,"imu_invert_y",c->imu_invert_y);
    cJSON_AddNumberToObject(root, "bootstyle_idx", c->bootstyle_idx);
    cJSON_AddNumberToObject(root, "kb_layout", c->kb_layout);
    cJSON_AddNumberToObject(root, "kbd_nav_mode", c->kbd_nav_mode);
    cJSON_AddNumberToObject(root, "cursor_mode", c->cursor_mode);
    cJSON_AddBoolToObject(root, "desktop_mode", c->desktop_mode);
    cJSON_AddStringToObject(root, "alt_slot_name", c->alt_slot_name);
    cJSON_AddNumberToObject(root, "sound_volume", c->sound_volume);
    cJSON_AddNumberToObject(root, "utc_offset", c->utc_offset);
    cJSON_AddBoolToObject(root, "ota_locked", c->ota_locked);
    cJSON_AddBoolToObject(root, "wifi_disabled", c->wifi_disabled);
    cJSON_AddBoolToObject(root, "bt_disabled", c->bt_disabled);
    cJSON_AddBoolToObject(root, "ota_bg_check_enabled", c->ota_bg_check_enabled);
    cJSON_AddBoolToObject(root, "mqtt_autostart", c->mqtt_autostart);
    cJSON_AddBoolToObject(root, "lock_enabled", c->lock_enabled);
    cJSON_AddStringToObject(root, "lock_password", c->lock_password);
    cJSON_AddBoolToObject(root, "post_beep_enabled", c->post_beep_enabled);
    cJSON_AddNumberToObject(root, "alarm_volume_idx", c->alarm_volume_idx);
    cJSON_AddBoolToObject(root, "sound_muted", c->sound_muted);
    cJSON_AddNumberToObject(root, "sb_flags", (double)c->sb_flags);
    cJSON_AddNumberToObject(root, "sb_bg_color", (double)c->sb_bg_color);
    cJSON_AddNumberToObject(root, "ap_channel", c->ap_channel);
    cJSON_AddBoolToObject(root, "ap_open", c->ap_open);
    cJSON_AddBoolToObject(root, "bg_gradient_enabled", c->bg_gradient_enabled);
    cJSON_AddNumberToObject(root, "menu_ui_style", c->menu_ui_style);
    cJSON_AddNumberToObject(root, "bios_ui_style", c->bios_ui_style);
    cJSON_AddNumberToObject(root, "boot_loader_style", c->boot_loader_style);
    cJSON_AddNumberToObject(root, "sleep_wake_mode", c->sleep_wake_mode);
    cJSON_AddNumberToObject(root, "lock_shape", c->lock_shape);
    cJSON_AddNumberToObject(root, "lock_particle_color", (double)c->lock_particle_color);
    cJSON_AddBoolToObject(root, "grub_enabled", c->grub_enabled);
    cJSON_AddNumberToObject(root, "default_boot_choice", c->default_boot_choice);
    cJSON_AddNumberToObject(root, "bios_bg_color", (double)c->bios_bg_color);
    cJSON_AddNumberToObject(root, "bios_hl_color", (double)c->bios_hl_color);
    cJSON_AddNumberToObject(root, "bios_text_color", (double)c->bios_text_color);
    cJSON_AddNumberToObject(root, "bios_selection_style", c->bios_selection_style);

    cJSON *hs = cJSON_CreateObject();
    cJSON_AddNumberToObject(hs, "2048",    c->high_scores.game_2048);
    cJSON_AddNumberToObject(hs, "tetris",  c->high_scores.tetris);
    cJSON_AddNumberToObject(hs, "snake",   c->high_scores.snake);
    cJSON_AddNumberToObject(hs, "memory",  c->high_scores.memory);
    cJSON_AddNumberToObject(hs, "reaction", c->high_scores.reaction);
    cJSON_AddNumberToObject(hs, "flappy",  c->high_scores.flappy);
    cJSON_AddNumberToObject(hs, "balance", c->high_scores.balance);
    cJSON_AddNumberToObject(hs, "pong",    c->high_scores.pong);
    cJSON_AddItemToObject(root, "high_scores", hs);

    cJSON *w = cJSON_CreateObject();
    cJSON_AddStringToObject(w, "ssid", c->wifi_ssid);
    char wifi_password_enc[192];
    if(!encrypt_to_hex(c->wifi_password,wifi_password_enc,sizeof(wifi_password_enc))) { cJSON_Delete(w); cJSON_Delete(root); return nullptr; }
    cJSON_AddStringToObject(w, "password", wifi_password_enc);
    cJSON_AddBoolToObject(w, "saved", c->wifi_saved);
    cJSON_AddItemToObject(root, "wifi", w);

    cJSON *nets = cJSON_CreateArray();
    for (int i = 0; i < c->wifi_networks_count && i < K85_MAX_WIFI_NETWORKS; i++) {
        cJSON *el = cJSON_CreateObject();
        cJSON_AddStringToObject(el, "ssid", c->wifi_networks[i].ssid);
        char net_pass_enc[192];
        if(!encrypt_to_hex(c->wifi_networks[i].password,net_pass_enc,sizeof(net_pass_enc))) { cJSON_Delete(el); cJSON_Delete(nets); cJSON_Delete(root); return nullptr; }
        cJSON_AddStringToObject(el, "password", net_pass_enc);
        cJSON_AddItemToArray(nets, el);
    }
    cJSON_AddItemToObject(root, "wifi_networks", nets);

    cJSON *profs = cJSON_CreateArray();
    for (int i = 0; i < K85_MAX_PROFILES; i++) {
        cJSON *pel = cJSON_CreateObject();
        cJSON_AddStringToObject(pel, "name", c->profiles[i].name);
        cJSON_AddNumberToObject(pel, "theme_idx", c->profiles[i].theme_idx);
        cJSON_AddNumberToObject(pel, "brightness_active", c->profiles[i].brightness_active);
        cJSON_AddNumberToObject(pel, "sound_volume", c->profiles[i].sound_volume);
        cJSON_AddBoolToObject(pel, "sound_muted", c->profiles[i].sound_muted);
        cJSON_AddNumberToObject(pel, "battery_mode_idx", c->profiles[i].battery_mode_idx);
        cJSON_AddNumberToObject(pel, "bootstyle_idx", c->profiles[i].bootstyle_idx);
        cJSON_AddBoolToObject(pel, "bg_gradient_enabled", c->profiles[i].bg_gradient_enabled);
        cJSON_AddStringToObject(pel, "wifi_ssid", c->profiles[i].wifi_ssid);
        cJSON_AddItemToArray(profs, pel);
    }
    cJSON_AddItemToObject(root, "profiles", profs);
    cJSON_AddNumberToObject(root, "active_profile_idx", c->active_profile_idx);

    cJSON *totps = cJSON_CreateArray();
    for (int i = 0; i < c->totp_entries_count; i++) {
        cJSON *tel = cJSON_CreateObject();
        cJSON_AddStringToObject(tel, "name", c->totp_entries[i].name);
        cJSON_AddStringToObject(tel, "secret_base32", c->totp_entries[i].secret_base32);
        cJSON_AddItemToArray(totps, tel);
    }
    cJSON_AddItemToObject(root, "totp_entries", totps);

    cJSON_AddStringToObject(root, "pwmgr_master_verifier", c->pwmgr_master_verifier);
    cJSON *pws = cJSON_CreateArray();
    for (int i = 0; i < c->pw_entries_count; i++) {
        cJSON *pel = cJSON_CreateObject();
        cJSON_AddStringToObject(pel, "name", c->pw_entries[i].name);
        cJSON_AddStringToObject(pel, "username", c->pw_entries[i].username);
        char iv_hex[40]; for (int b = 0; b < 16; b++) snprintf(iv_hex + b*2, 3, "%02x", c->pw_entries[i].iv[b]);
        cJSON_AddStringToObject(pel, "iv", iv_hex);
        char ct_hex[200]; for (int b = 0; b < c->pw_entries[i].ciphertext_len; b++) snprintf(ct_hex + b*2, 3, "%02x", c->pw_entries[i].ciphertext[b]);
        ct_hex[c->pw_entries[i].ciphertext_len*2] = 0;
        cJSON_AddStringToObject(pel, "ciphertext", ct_hex);
        cJSON_AddNumberToObject(pel, "ciphertext_len", c->pw_entries[i].ciphertext_len);
        cJSON_AddItemToArray(pws, pel);
    }
    cJSON_AddItemToObject(root, "pw_entries", pws);

    cJSON_AddStringToObject(root, "mqtt_broker_uri", c->mqtt_broker_uri);
    cJSON_AddStringToObject(root, "mqtt_client_id", c->mqtt_client_id);
    cJSON_AddStringToObject(root, "mqtt_username", c->mqtt_username);
    char mqtt_enc[192];
    if(!encrypt_to_hex(c->mqtt_password,mqtt_enc,sizeof(mqtt_enc))) { cJSON_Delete(root); return nullptr; }
    cJSON_AddStringToObject(root,"mqtt_password",mqtt_enc);
    cJSON_AddNumberToObject(root, "font_idx", c->font_idx);

    cJSON_AddBoolToObject(root, "ssh_enabled", c->ssh_enabled);
    cJSON_AddStringToObject(root, "ssh_username", c->ssh_username);
    cJSON_AddStringToObject(root, "ssh_password_hash", c->ssh_password_hash);
    cJSON_AddBoolToObject(root, "setup_completed", c->setup_completed);

    cJSON_AddNumberToObject(root, "step_count", c->step_count);
    cJSON_AddNumberToObject(root, "step_record", c->step_record);
    cJSON_AddStringToObject(root, "step_date", c->step_date);
    return root;
}

static void cfg_from_json(cJSON *root, k85_config_t *out) {
#define GET_INT(key, field) \
    do { cJSON *_x = cJSON_GetObjectItemCaseSensitive(root, key); \
         if (_x && cJSON_IsNumber(_x)) out->field = _x->valueint; } while (0)

    GET_INT("theme_idx", theme_idx);
    GET_INT("battery_mode_idx", battery_mode_idx);
    GET_INT("brightness_active", brightness_active);
    GET_INT("device_name_idx", device_name_idx);
    GET_INT("rotation", rotation);
    GET_INT("navigation_profile",navigation_profile);
    GET_INT("imu_sensitivity",imu_sensitivity);
    GET_INT("imu_deadzone",imu_deadzone);
    { cJSON *x=cJSON_GetObjectItemCaseSensitive(root,"navigation_swap"); if(cJSON_IsBool(x)) out->navigation_swap=cJSON_IsTrue(x); }
    { cJSON *x=cJSON_GetObjectItemCaseSensitive(root,"imu_follow_rotation"); if(cJSON_IsBool(x)) out->imu_follow_rotation=cJSON_IsTrue(x); }
    { cJSON *x=cJSON_GetObjectItemCaseSensitive(root,"imu_swap_xy"); if(cJSON_IsBool(x)) out->imu_swap_xy=cJSON_IsTrue(x); }
    { cJSON *x=cJSON_GetObjectItemCaseSensitive(root,"imu_invert_x"); if(cJSON_IsBool(x)) out->imu_invert_x=cJSON_IsTrue(x); }
    { cJSON *x=cJSON_GetObjectItemCaseSensitive(root,"imu_invert_y"); if(cJSON_IsBool(x)) out->imu_invert_y=cJSON_IsTrue(x); }
    { cJSON *x=cJSON_GetObjectItemCaseSensitive(root,"imu_neutral_x"); if(cJSON_IsNumber(x)) out->imu_neutral_x=x->valuedouble; }
    { cJSON *x=cJSON_GetObjectItemCaseSensitive(root,"imu_neutral_y"); if(cJSON_IsNumber(x)) out->imu_neutral_y=x->valuedouble; }
    GET_INT("bootstyle_idx", bootstyle_idx);
    GET_INT("kb_layout", kb_layout);
    GET_INT("kbd_nav_mode", kbd_nav_mode);
    GET_INT("cursor_mode", cursor_mode);
    { cJSON *_x = cJSON_GetObjectItemCaseSensitive(root, "desktop_mode"); if (_x) out->desktop_mode = cJSON_IsTrue(_x); }
    { cJSON *_x = cJSON_GetObjectItemCaseSensitive(root, "alt_slot_name"); if (_x && cJSON_IsString(_x)) set_str(out->alt_slot_name, sizeof(out->alt_slot_name), _x->valuestring); }
    GET_INT("sound_volume", sound_volume);
    GET_INT("utc_offset", utc_offset);
    { cJSON *_x = cJSON_GetObjectItemCaseSensitive(root, "ota_locked"); if (_x) out->ota_locked = cJSON_IsTrue(_x); }
    { cJSON *_x = cJSON_GetObjectItemCaseSensitive(root, "wifi_disabled"); if (_x) out->wifi_disabled = cJSON_IsTrue(_x); }
    { cJSON *_x = cJSON_GetObjectItemCaseSensitive(root, "bt_disabled"); if (_x) out->bt_disabled = cJSON_IsTrue(_x); }
    { cJSON *_x = cJSON_GetObjectItemCaseSensitive(root, "ota_bg_check_enabled"); if (_x) out->ota_bg_check_enabled = cJSON_IsTrue(_x); }
    { cJSON *_x = cJSON_GetObjectItemCaseSensitive(root, "mqtt_autostart"); if (_x) out->mqtt_autostart = cJSON_IsTrue(_x); }
    { cJSON *_x = cJSON_GetObjectItemCaseSensitive(root, "lock_enabled"); if (_x) out->lock_enabled = cJSON_IsTrue(_x); }
    { cJSON *_x = cJSON_GetObjectItemCaseSensitive(root, "lock_password"); if (_x && cJSON_IsString(_x)) set_str(out->lock_password, sizeof(out->lock_password), _x->valuestring); }
    { cJSON *_x = cJSON_GetObjectItemCaseSensitive(root, "post_beep_enabled"); if (_x) out->post_beep_enabled = cJSON_IsTrue(_x); }
    { cJSON *_x = cJSON_GetObjectItemCaseSensitive(root, "alarm_volume_idx"); if (_x && cJSON_IsNumber(_x)) out->alarm_volume_idx = _x->valueint; }
    { cJSON *_x = cJSON_GetObjectItemCaseSensitive(root, "sound_muted"); if (_x) out->sound_muted = cJSON_IsTrue(_x); }
    { cJSON *_x = cJSON_GetObjectItemCaseSensitive(root, "sb_flags"); if (_x && cJSON_IsNumber(_x)) out->sb_flags = (uint32_t)_x->valuedouble; }
    { cJSON *_x = cJSON_GetObjectItemCaseSensitive(root, "sb_bg_color"); if (_x && cJSON_IsNumber(_x)) out->sb_bg_color = (uint32_t)_x->valuedouble; }
    { cJSON *_x = cJSON_GetObjectItemCaseSensitive(root, "ap_channel"); if (_x && cJSON_IsNumber(_x)) out->ap_channel = _x->valueint; }
    { cJSON *_x = cJSON_GetObjectItemCaseSensitive(root, "ap_open"); if (_x) out->ap_open = cJSON_IsTrue(_x); }
    { cJSON *_x = cJSON_GetObjectItemCaseSensitive(root, "bg_gradient_enabled"); if (_x) out->bg_gradient_enabled = cJSON_IsTrue(_x); }
    { cJSON *_y = cJSON_GetObjectItemCaseSensitive(root, "menu_ui_style"); if (_y) out->menu_ui_style = _y->valueint; }
    { cJSON *_z = cJSON_GetObjectItemCaseSensitive(root, "bios_ui_style"); if (_z) out->bios_ui_style = _z->valueint; }
    { cJSON *_bl = cJSON_GetObjectItemCaseSensitive(root, "boot_loader_style"); if (_bl) out->boot_loader_style = _bl->valueint; }
    { cJSON *_sw = cJSON_GetObjectItemCaseSensitive(root, "sleep_wake_mode"); if (_sw) out->sleep_wake_mode = _sw->valueint; }
    { cJSON *_x = cJSON_GetObjectItemCaseSensitive(root, "lock_shape"); if (_x && cJSON_IsNumber(_x)) out->lock_shape = _x->valueint; }
    { cJSON *_x = cJSON_GetObjectItemCaseSensitive(root, "lock_particle_color"); if (_x && cJSON_IsNumber(_x)) out->lock_particle_color = (uint32_t)_x->valuedouble; }
    { cJSON *_x = cJSON_GetObjectItemCaseSensitive(root, "grub_enabled"); if (_x) out->grub_enabled = cJSON_IsTrue(_x); }
    { cJSON *_x = cJSON_GetObjectItemCaseSensitive(root, "default_boot_choice"); if (_x && cJSON_IsNumber(_x)) out->default_boot_choice = _x->valueint; }
    { cJSON *_x = cJSON_GetObjectItemCaseSensitive(root, "bios_bg_color"); if (_x && cJSON_IsNumber(_x)) out->bios_bg_color = (uint32_t)_x->valuedouble; }
    { cJSON *_x = cJSON_GetObjectItemCaseSensitive(root, "bios_hl_color"); if (_x && cJSON_IsNumber(_x)) out->bios_hl_color = (uint32_t)_x->valuedouble; }
    { cJSON *_x = cJSON_GetObjectItemCaseSensitive(root, "bios_text_color"); if (_x && cJSON_IsNumber(_x)) out->bios_text_color = (uint32_t)_x->valuedouble; }
    { cJSON *_x = cJSON_GetObjectItemCaseSensitive(root, "bios_selection_style"); if (_x && cJSON_IsNumber(_x)) out->bios_selection_style = _x->valueint; }
#undef GET_INT

    cJSON *hs = cJSON_GetObjectItemCaseSensitive(root, "high_scores");
    if (hs && cJSON_IsObject(hs)) {
#define GET_HS(key, field) \
        do { cJSON *_x = cJSON_GetObjectItemCaseSensitive(hs, key); \
             if (_x && cJSON_IsNumber(_x)) out->high_scores.field = _x->valueint; } while (0)
        GET_HS("2048", game_2048);
        GET_HS("tetris", tetris);
        GET_HS("snake", snake);
        GET_HS("memory", memory);
        GET_HS("reaction", reaction);
        GET_HS("flappy", flappy);
        GET_HS("balance", balance);
        GET_HS("pong", pong);
#undef GET_HS
    }

    cJSON *w = cJSON_GetObjectItemCaseSensitive(root, "wifi");
    if (w && cJSON_IsObject(w)) {
        cJSON *ssid = cJSON_GetObjectItemCaseSensitive(w, "ssid");
        if (ssid && cJSON_IsString(ssid)) set_str(out->wifi_ssid, sizeof(out->wifi_ssid), ssid->valuestring);
        cJSON *pass = cJSON_GetObjectItemCaseSensitive(w, "password");
        bool pass_ok=cJSON_IsString(pass) && restore_secret(pass->valuestring,out->wifi_password,sizeof(out->wifi_password));
        cJSON *saved = cJSON_GetObjectItemCaseSensitive(w, "saved");
        if (saved) out->wifi_saved = cJSON_IsTrue(saved) && pass_ok;
    }

    cJSON *nets = cJSON_GetObjectItemCaseSensitive(root, "wifi_networks");
    if (nets && cJSON_IsArray(nets)) {
        out->wifi_networks_count = 0;
        int n = cJSON_GetArraySize(nets);
        for (int i = 0; i < n && out->wifi_networks_count < K85_MAX_WIFI_NETWORKS; i++) {
            cJSON *el = cJSON_GetArrayItem(nets, i);
            if (!el || !cJSON_IsObject(el)) continue;
            cJSON *s = cJSON_GetObjectItemCaseSensitive(el, "ssid");
            if (!s || !cJSON_IsString(s)) continue;
            k85_wifi_net_t *net = &out->wifi_networks[out->wifi_networks_count];
            set_str(net->ssid, sizeof(net->ssid), s->valuestring);
            cJSON *p = cJSON_GetObjectItemCaseSensitive(el, "password");
            if(!cJSON_IsString(p) || !restore_secret(p->valuestring,net->password,sizeof(net->password))) continue;
            out->wifi_networks_count++;
        }
    }

    cJSON *profs = cJSON_GetObjectItemCaseSensitive(root, "profiles");
    if (profs && cJSON_IsArray(profs)) {
        int n = cJSON_GetArraySize(profs);
        for (int i = 0; i < n && i < K85_MAX_PROFILES; i++) {
            cJSON *pel = cJSON_GetArrayItem(profs, i);
            if (!pel || !cJSON_IsObject(pel)) continue;
            k85_profile_t *p = &out->profiles[i];
            cJSON *x;
            if ((x = cJSON_GetObjectItemCaseSensitive(pel, "name")) && cJSON_IsString(x)) set_str(p->name, sizeof(p->name), x->valuestring);
            if ((x = cJSON_GetObjectItemCaseSensitive(pel, "theme_idx")) && cJSON_IsNumber(x)) p->theme_idx = x->valueint;
            if ((x = cJSON_GetObjectItemCaseSensitive(pel, "brightness_active")) && cJSON_IsNumber(x)) p->brightness_active = x->valueint;
            if ((x = cJSON_GetObjectItemCaseSensitive(pel, "sound_volume")) && cJSON_IsNumber(x)) p->sound_volume = x->valueint;
            if ((x = cJSON_GetObjectItemCaseSensitive(pel, "sound_muted"))) p->sound_muted = cJSON_IsTrue(x);
            if ((x = cJSON_GetObjectItemCaseSensitive(pel, "battery_mode_idx")) && cJSON_IsNumber(x)) p->battery_mode_idx = x->valueint;
            if ((x = cJSON_GetObjectItemCaseSensitive(pel, "bootstyle_idx")) && cJSON_IsNumber(x)) p->bootstyle_idx = x->valueint;
            if ((x = cJSON_GetObjectItemCaseSensitive(pel, "bg_gradient_enabled"))) p->bg_gradient_enabled = cJSON_IsTrue(x);
            if ((x = cJSON_GetObjectItemCaseSensitive(pel, "wifi_ssid")) && cJSON_IsString(x)) set_str(p->wifi_ssid, sizeof(p->wifi_ssid), x->valuestring);
        }
    }
    { cJSON *x = cJSON_GetObjectItemCaseSensitive(root, "active_profile_idx"); if (x && cJSON_IsNumber(x)) out->active_profile_idx = x->valueint; }
    {
        cJSON *totps = cJSON_GetObjectItemCaseSensitive(root, "totp_entries");
        if (totps && cJSON_IsArray(totps)) {
            int n = cJSON_GetArraySize(totps);
            if (n > K85_MAX_TOTP_ENTRIES) n = K85_MAX_TOTP_ENTRIES;
            for (int i = 0; i < n; i++) {
                cJSON *tel = cJSON_GetArrayItem(totps, i);
                if (!tel || !cJSON_IsObject(tel)) continue;
                cJSON *nm = cJSON_GetObjectItemCaseSensitive(tel, "name");
                cJSON *sc = cJSON_GetObjectItemCaseSensitive(tel, "secret_base32");
                if (nm && cJSON_IsString(nm)) snprintf(out->totp_entries[i].name, sizeof(out->totp_entries[i].name), "%s", nm->valuestring);
                if (sc && cJSON_IsString(sc)) snprintf(out->totp_entries[i].secret_base32, sizeof(out->totp_entries[i].secret_base32), "%s", sc->valuestring);
            }
            out->totp_entries_count = n;
        }
    }
    {
        cJSON *_pw = cJSON_GetObjectItemCaseSensitive(root, "pwmgr_master_verifier");
        if (_pw && cJSON_IsString(_pw)) snprintf(out->pwmgr_master_verifier, sizeof(out->pwmgr_master_verifier), "%s", _pw->valuestring);
    }
    {
        cJSON *pws = cJSON_GetObjectItemCaseSensitive(root, "pw_entries");
        if (pws && cJSON_IsArray(pws)) {
            int n = cJSON_GetArraySize(pws);
            if (n > K85_MAX_PW_ENTRIES) n = K85_MAX_PW_ENTRIES;
            for (int i = 0; i < n; i++) {
                cJSON *pel = cJSON_GetArrayItem(pws, i);
                if (!pel || !cJSON_IsObject(pel)) continue;
                cJSON *nm = cJSON_GetObjectItemCaseSensitive(pel, "name");
                cJSON *un = cJSON_GetObjectItemCaseSensitive(pel, "username");
                cJSON *ivh = cJSON_GetObjectItemCaseSensitive(pel, "iv");
                cJSON *cth = cJSON_GetObjectItemCaseSensitive(pel, "ciphertext");
                cJSON *ctl = cJSON_GetObjectItemCaseSensitive(pel, "ciphertext_len");
                if (nm && cJSON_IsString(nm)) snprintf(out->pw_entries[i].name, sizeof(out->pw_entries[i].name), "%s", nm->valuestring);
                if (un && cJSON_IsString(un)) snprintf(out->pw_entries[i].username, sizeof(out->pw_entries[i].username), "%s", un->valuestring);
                if (ivh && cJSON_IsString(ivh)) {
                    const char *h = ivh->valuestring;
                    for (int b = 0; b < 16 && h[b*2] && h[b*2+1]; b++) {
                        char byte_str[3] = { h[b*2], h[b*2+1], 0 };
                        out->pw_entries[i].iv[b] = (uint8_t)strtoul(byte_str, nullptr, 16);
                    }
                }
                if (cth && cJSON_IsString(cth)) {
                    const char *h = cth->valuestring;
                    int hl = (int)strlen(h);
                    for (int b = 0; b < hl / 2 && b < 80; b++) {
                        char byte_str[3] = { h[b*2], h[b*2+1], 0 };
                        out->pw_entries[i].ciphertext[b] = (uint8_t)strtoul(byte_str, nullptr, 16);
                    }
                }
                if (ctl && cJSON_IsNumber(ctl)) out->pw_entries[i].ciphertext_len = ctl->valueint;
            }
            out->pw_entries_count = n;
        }
    }
    { cJSON *_m = cJSON_GetObjectItemCaseSensitive(root, "mqtt_broker_uri"); if (_m && cJSON_IsString(_m)) snprintf(out->mqtt_broker_uri, sizeof(out->mqtt_broker_uri), "%s", _m->valuestring); }
    { cJSON *_m = cJSON_GetObjectItemCaseSensitive(root, "mqtt_client_id"); if (_m && cJSON_IsString(_m)) snprintf(out->mqtt_client_id, sizeof(out->mqtt_client_id), "%s", _m->valuestring); }
    { cJSON *_m = cJSON_GetObjectItemCaseSensitive(root, "mqtt_username"); if (_m && cJSON_IsString(_m)) snprintf(out->mqtt_username, sizeof(out->mqtt_username), "%s", _m->valuestring); }
    { cJSON *_m = cJSON_GetObjectItemCaseSensitive(root, "mqtt_password"); if (_m && cJSON_IsString(_m)) restore_secret(_m->valuestring,out->mqtt_password,sizeof(out->mqtt_password)); }
    { cJSON *_f = cJSON_GetObjectItemCaseSensitive(root, "font_idx"); if (_f) out->font_idx = _f->valueint; }

    { cJSON *x = cJSON_GetObjectItemCaseSensitive(root, "ssh_enabled"); if (x) out->ssh_enabled = cJSON_IsTrue(x); }
    { cJSON *x = cJSON_GetObjectItemCaseSensitive(root, "ssh_username"); if (x && cJSON_IsString(x)) set_str(out->ssh_username, sizeof(out->ssh_username), x->valuestring); }
    { cJSON *x = cJSON_GetObjectItemCaseSensitive(root, "ssh_password_hash"); if (x && cJSON_IsString(x)) set_str(out->ssh_password_hash, sizeof(out->ssh_password_hash), x->valuestring); }
    { cJSON *x = cJSON_GetObjectItemCaseSensitive(root, "setup_completed"); if (x) out->setup_completed = cJSON_IsTrue(x); }

    cJSON *sc = cJSON_GetObjectItemCaseSensitive(root, "step_count");
    if (sc && cJSON_IsNumber(sc)) out->step_count = sc->valueint;
    cJSON *sr = cJSON_GetObjectItemCaseSensitive(root, "step_record");
    if (sr && cJSON_IsNumber(sr)) out->step_record = sr->valueint;
    cJSON *sd = cJSON_GetObjectItemCaseSensitive(root, "step_date");
    if (sd && cJSON_IsString(sd)) set_str(out->step_date, sizeof(out->step_date), sd->valuestring);
}

// РђРЅР°Р»РѕРі _deep_merge_defaults РёР· MicroPython
static void deep_merge(cJSON *dst, const cJSON *src) {
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, src) {
        if (!item->string) continue;
        cJSON *existing = cJSON_GetObjectItemCaseSensitive(dst, item->string);
        if (!existing) {
            cJSON *copy = cJSON_Duplicate(item, 1);
            if (copy) cJSON_AddItemToObject(dst, item->string, copy);
        } else if (cJSON_IsObject(item) && cJSON_IsObject(existing)) {
            deep_merge(existing, item);
        }
    }
}

// ============================ load / save ============================
// 튜닝: 설정 파일 최대 256KiB. 비정상 파일로 메모리를 고갈시키지 않는다.
static constexpr long CONFIG_MAX_BYTES=256*1024;
static cJSON *s_retained=nullptr;
static uint64_t s_generation=0;
static bool s_has_digest=false;
static unsigned char s_digest[32]={};
static bool s_main_is_latest=false;
static SemaphoreHandle_t config_mutex() { static SemaphoreHandle_t m=xSemaphoreCreateMutex(); return m; }
struct ConfigGuard {
    bool ok; ConfigGuard():ok(config_mutex() && xSemaphoreTake(config_mutex(),portMAX_DELAY)==pdTRUE) {}
    ~ConfigGuard() { if(ok) xSemaphoreGive(config_mutex()); }
};
static void memory_digest(const k85_config_t *cfg,unsigned char out[32]) {
    mbedtls_sha256(reinterpret_cast<const unsigned char*>(cfg),sizeof(*cfg),out,0);
}
// 원리: checksum 필드를 제외한 JSON을 직렬화해 손상 여부를 검사한다(인증용은 아니다).
static bool checksum(cJSON *root,char out[65]) {
    char *txt=cJSON_PrintUnformatted(root); if(!txt) return false;
    unsigned char hash[32]; int err=mbedtls_sha256(reinterpret_cast<unsigned char*>(txt),strlen(txt),hash,0);
    cJSON_free(txt); if(err) return false; k85secret::hex(hash,32,out); return true;
}
static cJSON *read_record(const char *path,uint64_t &generation) {
    FILE *f=fopen(path,"rb"); if(!f) return nullptr;
    if(fseek(f,0,SEEK_END)!=0) { fclose(f); return nullptr; }
    long size=ftell(f);
    if(size<=0 || size>CONFIG_MAX_BYTES || fseek(f,0,SEEK_SET)!=0) { fclose(f); return nullptr; }
    char *buf=static_cast<char*>(malloc(size+1)); if(!buf) { fclose(f); return nullptr; }
    size_t n=fread(buf,1,size,f); bool ok=!ferror(f) && n==size;
    ok=(fclose(f)==0) && ok; buf[n]=0;
    cJSON *root=ok?cJSON_ParseWithOpts(buf,nullptr,true):nullptr; free(buf);
    if(!cJSON_IsObject(root)) { cJSON_Delete(root); return nullptr; }
    auto schema=cJSON_GetObjectItemCaseSensitive(root,"config_schema");
    auto gen=cJSON_GetObjectItemCaseSensitive(root,"_generation");
    auto sum=cJSON_DetachItemFromObjectCaseSensitive(root,"_checksum");
    if(!schema && !gen && !sum) { generation=0; return root; }
    char actual[65];
    ok=cJSON_IsNumber(schema) && schema->valueint==2 && cJSON_IsNumber(gen) &&
       std::isfinite(gen->valuedouble) && gen->valuedouble>=1 && gen->valuedouble<9007199254740991.0 &&
       floor(gen->valuedouble)==gen->valuedouble && cJSON_IsString(sum) && strlen(sum->valuestring)==64 &&
       checksum(root,actual) && strcmp(actual,sum->valuestring)==0;
    if(sum) cJSON_AddItemToObject(root,"_checksum",sum);
    if(!ok) { cJSON_Delete(root); return nullptr; }
    generation=static_cast<uint64_t>(gen->valuedouble); return root;
}
static void validate_config(k85_config_t &cfg) {
    auto range=[](int &v,int min,int max,int fallback) { if(v<min||v>max) v=fallback; };
    range(cfg.rotation,0,3,1); range(cfg.navigation_profile,0,1,0);
    range(cfg.imu_sensitivity,0,2,1); range(cfg.imu_deadzone,0,2,1);
    range(cfg.brightness_active,0,100,80); range(cfg.bootstyle_idx,0,2,0);
    range(cfg.device_name_idx,0,9,0); range(cfg.boot_loader_style,0,3,0); range(cfg.sound_volume,0,100,20);
    range(cfg.cursor_mode,0,3,0); range(cfg.kbd_nav_mode,0,1,0);
    range(cfg.menu_ui_style,0,2,0); range(cfg.bios_ui_style,0,2,0);
    range(cfg.battery_mode_idx,0,2,1); range(cfg.font_idx,0,4,0);
    range(cfg.active_profile_idx,0,K85_MAX_PROFILES-1,0);
    range(cfg.wifi_networks_count,0,K85_MAX_WIFI_NETWORKS,0);
    range(cfg.pw_entries_count,0,K85_MAX_PW_ENTRIES,0); range(cfg.totp_entries_count,0,K85_MAX_TOTP_ENTRIES,0);
    for(int i=0;i<K85_MAX_PW_ENTRIES;i++) range(cfg.pw_entries[i].ciphertext_len,0,80,0);
    if(!std::isfinite(cfg.imu_neutral_x)||fabsf(cfg.imu_neutral_x)>2) cfg.imu_neutral_x=0;
    if(!std::isfinite(cfg.imu_neutral_y)||fabsf(cfg.imu_neutral_y)>2) cfg.imu_neutral_y=0;
}
bool k85_config_load(void) {
    ConfigGuard lock; if(!lock.ok) return false;
    const char *paths[]={CONFIG_FILE_PATH,CONFIG_FILE_PATH ".tmp",CONFIG_FILE_PATH ".bak"};
    cJSON *best=nullptr; uint64_t highest=0; int chosen=-1;
    for(int i=0;i<3;i++) {
        uint64_t generation=0; auto candidate=read_record(paths[i],generation);
        if(candidate && (!best || generation>highest)) {
            cJSON_Delete(best); best=candidate; highest=generation; chosen=i;
        } else cJSON_Delete(candidate);
    }
    k85_config_defaults(&g_config); s_has_digest=false;
    cJSON_Delete(s_retained); s_retained=nullptr; s_generation=0; s_main_is_latest=false;
    if(!best) { ESP_LOGW(TAG,"No valid config; files retained for recovery"); return false; }
    cfg_from_json(best,&g_config); validate_config(g_config);
    s_retained=best; s_generation=highest; s_main_is_latest=chosen==0;
    memory_digest(&g_config,s_digest); s_has_digest=true;
    return true;
}
static bool config_save_locked() {
    // 정상 설정의 변경이 없으면 재암호화/flash 쓰기를 생략한다.
    validate_config(g_config);
    unsigned char current[32]; memory_digest(&g_config,current);
    if(s_has_digest && memcmp(current,s_digest,32)==0) return true;
    auto snapshot=static_cast<k85_config_t*>(malloc(sizeof(k85_config_t)));
    if(!snapshot) return false; memcpy(snapshot,&g_config,sizeof(*snapshot)); validate_config(*snapshot);
    cJSON *root=cfg_to_json(snapshot); memory_digest(snapshot,current); memset(snapshot,0,sizeof(*snapshot)); free(snapshot);
    if(!root) return false;
    if(s_retained) deep_merge(root,s_retained);
    cJSON_DeleteItemFromObjectCaseSensitive(root,"_checksum");
    cJSON_DeleteItemFromObjectCaseSensitive(root,"_generation");
    cJSON_DeleteItemFromObjectCaseSensitive(root,"config_schema");
    if(s_generation>=9007199254740990ULL) { cJSON_Delete(root); return false; }
    cJSON_AddNumberToObject(root,"config_schema",2);
    cJSON_AddNumberToObject(root,"_generation",double(s_generation+1));
    char sum[65]; if(!checksum(root,sum)) { cJSON_Delete(root); return false; }
    cJSON_AddStringToObject(root,"_checksum",sum);
    char *txt=cJSON_PrintUnformatted(root); if(!txt) { cJSON_Delete(root); return false; }
    auto &io=k85_config_io(); FILE *f=fopen(CONFIG_FILE_PATH ".tmp","wb"); bool ok=false;
    if(f) {
        size_t len=strlen(txt); ok=io.write(txt,1,len,f)==len;
        if(io.flush(f)!=0) ok=false;
        if(io.sync(fileno(f))!=0) ok=false;
        if(io.close(f)!=0) ok=false;
    }
    cJSON_free(txt);
    uint64_t generation=0; auto checked=ok?read_record(CONFIG_FILE_PATH ".tmp",generation):nullptr;
    ok=checked && generation==s_generation+1; cJSON_Delete(checked);
    // 오류가 난 임시본은 복구 후보로 선택되지 않도록 무효화한다. 정상본/백업은 건드리지 않는다.
    if(!ok) { remove(CONFIG_FILE_PATH ".tmp"); cJSON_Delete(root); return false; }
    if(s_main_is_latest && io.rename(CONFIG_FILE_PATH,CONFIG_FILE_PATH ".bak")!=0) {
        remove(CONFIG_FILE_PATH ".tmp"); cJSON_Delete(root); return false;
    }
    if(s_main_is_latest) s_main_is_latest=false;
    if(io.rename(CONFIG_FILE_PATH ".tmp",CONFIG_FILE_PATH)!=0) {
        remove(CONFIG_FILE_PATH ".tmp"); cJSON_Delete(root); return false;
    }
    cJSON_Delete(s_retained); s_retained=root; ++s_generation; s_main_is_latest=true;
    memcpy(s_digest,current,32); s_has_digest=true; return true;
}
bool k85_config_save(void) {
    ConfigGuard lock; return lock.ok && config_save_locked();
}
bool k85_config_restore_file(const char *path) {
    ConfigGuard lock; if(!lock.ok) return false;
    uint64_t ignored=0; cJSON *restored=read_record(path,ignored); if(!restored) return false;
    auto old=static_cast<k85_config_t*>(malloc(sizeof(g_config))); if(!old) { cJSON_Delete(restored); return false; }
    memcpy(old,&g_config,sizeof(g_config)); cJSON *old_root=s_retained; bool old_digest=s_has_digest;
    k85_config_defaults(&g_config); cfg_from_json(restored,&g_config); validate_config(g_config);
    s_retained=restored; s_has_digest=false;
    bool ok=config_save_locked();
    if(ok) cJSON_Delete(old_root);
    else { cJSON_Delete(s_retained); s_retained=old_root; s_has_digest=old_digest; memcpy(&g_config,old,sizeof(g_config)); }
    memset(old,0,sizeof(g_config)); free(old); return ok;
}
// 용어: 세대=generation, checksum=우발 손상 검사값, 임시본/백업본=중단 후 정상값 복구 후보.

// ============================ high scores ============================
int k85_get_high_score(const char *game) {
    if (!strcmp(game, "2048"))    return g_config.high_scores.game_2048;
    if (!strcmp(game, "tetris"))  return g_config.high_scores.tetris;
    if (!strcmp(game, "snake"))   return g_config.high_scores.snake;
    if (!strcmp(game, "memory"))  return g_config.high_scores.memory;
    if (!strcmp(game, "reaction")) return g_config.high_scores.reaction;
    if (!strcmp(game, "flappy"))  return g_config.high_scores.flappy;
    if (!strcmp(game, "balance")) return g_config.high_scores.balance;
    if (!strcmp(game, "pong"))    return g_config.high_scores.pong;
    return 0;
}

static int *high_score_ptr(const char *game) {
    if (!strcmp(game, "2048"))    return &g_config.high_scores.game_2048;
    if (!strcmp(game, "tetris"))  return &g_config.high_scores.tetris;
    if (!strcmp(game, "snake"))   return &g_config.high_scores.snake;
    if (!strcmp(game, "memory"))  return &g_config.high_scores.memory;
    if (!strcmp(game, "reaction")) return &g_config.high_scores.reaction;
    if (!strcmp(game, "flappy"))  return &g_config.high_scores.flappy;
    if (!strcmp(game, "balance")) return &g_config.high_scores.balance;
    if (!strcmp(game, "pong"))    return &g_config.high_scores.pong;
    return NULL;
}

bool k85_set_high_score_if_better(const char *game, int value) {
    int *p = high_score_ptr(game);
    if (!p) return false;
    if (value > *p) {
        *p = value;
        k85_config_save();
        return true;
    }
    return false;
}

bool k85_set_low_score_if_better(const char *game, int value) {
    int *p = high_score_ptr(game);
    if (!p) return false;
    if (*p == 0 || value < *p) {
        *p = value;
        k85_config_save();
        return true;
    }
    return false;
}

// ============================ sound ============================
int k85_get_sound_volume(void) {
    return g_config.sound_volume;
}

void k85_set_sound_volume(int v) {
    g_config.sound_volume = v;
    k85_config_save();
}














