#pragma once
#include <cstddef>
using esp_err_t=int;
constexpr int ESP_OK=0,ESP_FAIL=-1;
struct esp_vfs_littlefs_conf_t {const char *base_path,*partition_label; bool format_if_mount_failed,dont_mount;};
inline int esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t *c) {return c->format_if_mount_failed?ESP_FAIL:ESP_OK;}
inline bool esp_littlefs_mounted(const char*) {return true;}
inline int esp_littlefs_info(const char*,size_t *t,size_t *u) {*t=10000;*u=0;return ESP_OK;}
inline const char *esp_err_to_name(int) {return "test";}
