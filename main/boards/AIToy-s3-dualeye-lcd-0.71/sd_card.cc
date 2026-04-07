#include "sd_card.h"
#include "config.h"
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <driver/sdmmc_host.h>
#include <esp_log.h>
#include <dirent.h>
#include <cstring>
#include <cstdio>
#include <map>
#include <string>
#include <lvgl.h>

static const char* TAG = "SdCard";

static bool sd_mounted_ = false;
static std::map<std::string, std::string> gif_map_;
static std::string default_gif_;

static const struct {
    const char* emotion;
    const char* group;
} emotion_groups[] = {
    {"laughing", "happy"}, {"funny", "happy"},
    {"crying", "sad"},
    {"shocked", "surprised"},
    {"confused", "thinking"},
    {"kissy", "loving"},
    {"relaxed", "neutral"},
    {"confident", "cool"},
    {nullptr, nullptr}
};

bool SdCardInit() {
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    sdmmc_card_t* card = nullptr;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = (gpio_num_t)SD_CLK_PIN;
    slot_config.cmd = (gpio_num_t)SD_CMD_PIN;
    slot_config.d0 = (gpio_num_t)SD_D0_PIN;
    slot_config.d1 = GPIO_NUM_NC;
    slot_config.d2 = GPIO_NUM_NC;
    slot_config.d3 = GPIO_NUM_NC;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD card not available: %s", esp_err_to_name(ret));
        return false;
    }
    sd_mounted_ = true;
    sdmmc_card_print_info(stdout, card);

    DIR* dir = opendir("/sdcard");
    if (!dir) {
        ESP_LOGW(TAG, "Cannot open /sdcard directory");
        return false;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type != DT_REG) continue;
        char* ext = strrchr(entry->d_name, '.');
        if (!ext || strcasecmp(ext, ".gif") != 0) continue;

        std::string name(entry->d_name, ext - entry->d_name);
        for (auto& c : name) c = tolower(c);

        std::string lv_path = std::string("S:") + entry->d_name;
        gif_map_[name] = lv_path;
        ESP_LOGI(TAG, "GIF found: %s -> %s", name.c_str(), lv_path.c_str());
    }
    closedir(dir);

    if (gif_map_.count("neutral"))
        default_gif_ = gif_map_["neutral"];
    else if (!gif_map_.empty())
        default_gif_ = gif_map_.begin()->second;

    ESP_LOGI(TAG, "Found %d GIF files, default: %s",
             (int)gif_map_.size(), default_gif_.c_str());
    return !gif_map_.empty();
}

bool SdCardHasGifs() {
    return sd_mounted_ && !gif_map_.empty();
}

const char* SdCardGetGifPath(const char* emotion) {
    if (!emotion || gif_map_.empty()) return default_gif_.c_str();

    std::string key(emotion);
    for (auto& c : key) c = tolower(c);

    auto it = gif_map_.find(key);
    if (it != gif_map_.end()) return it->second.c_str();

    for (int i = 0; emotion_groups[i].emotion; i++) {
        if (key == emotion_groups[i].emotion) {
            it = gif_map_.find(emotion_groups[i].group);
            if (it != gif_map_.end()) return it->second.c_str();
        }
    }

    return default_gif_.c_str();
}

// --- LVGL stdio-based filesystem driver for SD card ---
static void* sdfs_open(lv_fs_drv_t* drv, const char* path, lv_fs_mode_t mode) {
    (void)drv;
    const char* flags = "rb";
    if (mode == LV_FS_MODE_WR) flags = "wb";
    else if (mode == (LV_FS_MODE_WR | LV_FS_MODE_RD)) flags = "rb+";

    char full[300];
    snprintf(full, sizeof(full), "/sdcard/%s", path);
    return fopen(full, flags);
}

static lv_fs_res_t sdfs_close(lv_fs_drv_t* drv, void* fp) {
    (void)drv;
    fclose((FILE*)fp);
    return LV_FS_RES_OK;
}

static lv_fs_res_t sdfs_read(lv_fs_drv_t* drv, void* fp, void* buf,
                             uint32_t btr, uint32_t* br) {
    (void)drv;
    *br = fread(buf, 1, btr, (FILE*)fp);
    return LV_FS_RES_OK;
}

static lv_fs_res_t sdfs_write(lv_fs_drv_t* drv, void* fp, const void* buf,
                              uint32_t btw, uint32_t* bw) {
    (void)drv;
    *bw = fwrite(buf, 1, btw, (FILE*)fp);
    return LV_FS_RES_OK;
}

static lv_fs_res_t sdfs_seek(lv_fs_drv_t* drv, void* fp, uint32_t pos,
                             lv_fs_whence_t whence) {
    (void)drv;
    int w = SEEK_SET;
    if (whence == LV_FS_SEEK_CUR) w = SEEK_CUR;
    else if (whence == LV_FS_SEEK_END) w = SEEK_END;
    fseek((FILE*)fp, (long)pos, w);
    return LV_FS_RES_OK;
}

static lv_fs_res_t sdfs_tell(lv_fs_drv_t* drv, void* fp, uint32_t* pos_p) {
    (void)drv;
    *pos_p = (uint32_t)ftell((FILE*)fp);
    return LV_FS_RES_OK;
}

void SdCardRegisterLvglFs() {
    static lv_fs_drv_t drv;
    lv_fs_drv_init(&drv);
    drv.letter = 'S';
    drv.cache_size = 2 * 1024;
    drv.open_cb = sdfs_open;
    drv.close_cb = sdfs_close;
    drv.read_cb = sdfs_read;
    drv.write_cb = sdfs_write;
    drv.seek_cb = sdfs_seek;
    drv.tell_cb = sdfs_tell;
    lv_fs_drv_register(&drv);
    ESP_LOGI(TAG, "LVGL FS driver 'S' registered for SD card");
}
