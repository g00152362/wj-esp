#include "eye_display.h"
#include "eye_animator.h"
#include "sd_card.h"
#include <libs/gif/lv_gif.h>
#include <esp_log.h>
#include <vector>
#include <cstring>

static const char* TAG = "EyeDisplay";

EyeDisplay::EyeDisplay(esp_lcd_panel_io_handle_t panel_io,
                       esp_lcd_panel_handle_t panel,
                       int width, int height,
                       int offset_x, int offset_y,
                       bool mirror_x, bool mirror_y, bool swap_xy,
                       EyeSide side,
                       bool gif_mode)
    : side_(side), gif_mode_(gif_mode), panel_io_(panel_io), panel_(panel) {
    width_ = width;
    height_ = height;

    std::vector<uint16_t> buffer(width_, 0x0000);
    for (int y = 0; y < height_; y++) {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "Turning display on");
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

    static bool lvgl_inited = false;
    if (!lvgl_inited) {
        ESP_LOGI(TAG, "Initialize LVGL library");
        lv_init();

        ESP_LOGI(TAG, "Initialize LVGL port");
        lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
        port_cfg.task_priority = 1;
        port_cfg.timer_period_ms = gif_mode ? 20 : 50;
        lvgl_port_init(&port_cfg);
        lvgl_inited = true;
    }

    ESP_LOGI(TAG, "Adding LCD display (%s eye, %s mode)",
             side == EyeSide::LEFT ? "left" : "right",
             gif_mode ? "GIF" : "LVGL");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * (gif_mode_ ? 80 : 20)),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = false,
        .rotation = {
            .swap_xy = swap_xy,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = 1,
            .buff_spiram = 0,
            .sw_rotate = 0,
            .swap_bytes = 1,
            .full_refresh = 0,
            .direct_mode = 0,
        },
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (!display_) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }

    lv_display_set_default(display_);
    vTaskDelay(pdMS_TO_TICKS(10));

    if (gif_mode_) {
        static bool fs_registered = false;
        if (!fs_registered) {
            SdCardRegisterLvglFs();
            fs_registered = true;
        }
    }

    if (lvgl_port_lock(5000)) {
        if (gif_mode_) {
            CreateGifObjects();
        } else {
            CreateEyeObjects();
        }
        lvgl_port_unlock();
    }
}

void EyeDisplay::CreateGifObjects() {
    lv_obj_t* screen = lv_display_get_screen_active(display_);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    gif_obj_ = lv_gif_create(screen);
    lv_obj_set_size(gif_obj_, 240, 240);
    lv_obj_align(gif_obj_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(gif_obj_, LV_OPA_TRANSP, 0);
}

void EyeDisplay::LoadInitialGif() {
    if (!gif_mode_ || !gif_obj_) return;
    const char* path = SdCardGetGifPath("neutral");
    if (path && path[0]) {
        lv_gif_set_src(gif_obj_, path);
        ESP_LOGI(TAG, "GIF initial source: %s", path);
    }
}

void EyeDisplay::CreateEyeObjects() {
    lv_obj_t* screen = lv_display_get_screen_active(display_);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    sclera_ = lv_obj_create(screen);
    lv_obj_remove_style_all(sclera_);
    lv_obj_set_size(sclera_, 180, 180);
    lv_obj_align(sclera_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(sclera_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(sclera_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(sclera_, LV_OPA_COVER, 0);
    lv_obj_set_style_clip_corner(sclera_, true, 0);
    lv_obj_remove_flag(sclera_, LV_OBJ_FLAG_SCROLLABLE);

    iris_ = lv_obj_create(sclera_);
    lv_obj_remove_style_all(iris_);
    lv_obj_set_size(iris_, 100, 100);
    lv_obj_align(iris_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(iris_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(iris_, lv_color_hex(0x4488CC), 0);
    lv_obj_set_style_bg_opa(iris_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(iris_, LV_OBJ_FLAG_SCROLLABLE);

    pupil_ = lv_obj_create(iris_);
    lv_obj_remove_style_all(pupil_);
    lv_obj_set_size(pupil_, 44, 44);
    lv_obj_align(pupil_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(pupil_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(pupil_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(pupil_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(pupil_, LV_OBJ_FLAG_SCROLLABLE);

    highlight_ = lv_obj_create(iris_);
    lv_obj_remove_style_all(highlight_);
    lv_obj_set_size(highlight_, 14, 14);
    lv_obj_set_style_radius(highlight_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(highlight_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(highlight_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(highlight_, LV_OBJ_FLAG_SCROLLABLE);
    if (side_ == EyeSide::LEFT) {
        lv_obj_align(highlight_, LV_ALIGN_CENTER, -15, -15);
    } else {
        lv_obj_align(highlight_, LV_ALIGN_CENTER, -15, 15);
    }

    eyelid_top_ = lv_obj_create(screen);
    lv_obj_remove_style_all(eyelid_top_);
    lv_obj_set_size(eyelid_top_, 280, 160);
    lv_obj_set_pos(eyelid_top_, -20, -170);
    lv_obj_set_style_radius(eyelid_top_, 70, 0);
    lv_obj_set_style_bg_color(eyelid_top_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(eyelid_top_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(eyelid_top_, LV_OBJ_FLAG_SCROLLABLE);

    eyelid_bottom_ = lv_obj_create(screen);
    lv_obj_remove_style_all(eyelid_bottom_);
    lv_obj_set_size(eyelid_bottom_, 280, 160);
    lv_obj_set_pos(eyelid_bottom_, -20, 250);
    lv_obj_set_style_radius(eyelid_bottom_, 70, 0);
    lv_obj_set_style_bg_color(eyelid_bottom_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(eyelid_bottom_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(eyelid_bottom_, LV_OBJ_FLAG_SCROLLABLE);

    eyebrow_ = lv_obj_create(screen);
    lv_obj_remove_style_all(eyebrow_);
    lv_obj_set_size(eyebrow_, 90, 14);
    lv_obj_align(eyebrow_, LV_ALIGN_CENTER, 0, -75);
    lv_obj_set_style_radius(eyebrow_, 7, 0);
    lv_obj_set_style_bg_color(eyebrow_, lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_opa(eyebrow_, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_pivot_x(eyebrow_, 45, 0);
    lv_obj_set_style_transform_pivot_y(eyebrow_, 7, 0);
    lv_obj_remove_flag(eyebrow_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(eyebrow_, LV_OBJ_FLAG_HIDDEN);

    decor_ = lv_obj_create(screen);
    lv_obj_remove_style_all(decor_);
    lv_obj_set_size(decor_, 22, 22);
    lv_obj_set_style_radius(decor_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(decor_, lv_color_hex(0xFF4466), 0);
    lv_obj_set_style_bg_opa(decor_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(decor_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(decor_, LV_OBJ_FLAG_HIDDEN);

    decor2_ = lv_obj_create(screen);
    lv_obj_remove_style_all(decor2_);
    lv_obj_set_size(decor2_, 16, 16);
    lv_obj_set_style_radius(decor2_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(decor2_, lv_color_hex(0xFF4466), 0);
    lv_obj_set_style_bg_opa(decor2_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(decor2_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(decor2_, LV_OBJ_FLAG_HIDDEN);
}

void EyeDisplay::SetEmotion(const char* emotion) {
    if (gif_mode_) {
        if (side_ != EyeSide::LEFT) return;
        if (!gif_obj_ || !emotion) return;
        const char* path = SdCardGetGifPath(emotion);
        if (path && path[0]) {
            if (lvgl_port_lock(200)) {
                lv_gif_set_src(gif_obj_, path);
                lv_gif_restart(gif_obj_);
                if (pair_ && pair_->GetGifObj()) {
                    lv_gif_set_src(pair_->GetGifObj(), path);
                    lv_gif_restart(pair_->GetGifObj());
                }
                lvgl_port_unlock();
            }
            ESP_LOGI(TAG, "GIF emotion: %s -> %s", emotion, path);
        }
    } else {
        EyeAnimator::GetInstance().SetEmotion(emotion);
    }
}

void EyeDisplay::SetStatus(const char* status) {
    if (side_ != EyeSide::LEFT) return;
    if (gif_mode_) return;
    auto& animator = EyeAnimator::GetInstance();
    if (strstr(status, "Stand") || strstr(status, "待命")) {
        animator.SetState(EyeAnimState::IDLE);
    } else if (strstr(status, "Listen") || strstr(status, "聆听")) {
        animator.SetState(EyeAnimState::LISTENING);
    } else if (strstr(status, "Speak") || strstr(status, "说话")) {
        animator.SetState(EyeAnimState::SPEAKING);
    } else if (strstr(status, "Connect") || strstr(status, "连接")) {
        animator.SetState(EyeAnimState::CONNECTING);
    }
}

void EyeDisplay::SetEyelidTopY(int y) {
    if (eyelid_top_) lv_obj_set_y(eyelid_top_, y);
}

void EyeDisplay::SetEyelidBottomY(int y) {
    if (eyelid_bottom_) lv_obj_set_y(eyelid_bottom_, y);
}

void EyeDisplay::SetIrisOffset(int dx, int dy) {
    if (iris_) lv_obj_align(iris_, LV_ALIGN_CENTER, dx, dy);
}

void EyeDisplay::SetPupilDiameter(int d) {
    if (pupil_) {
        lv_obj_set_size(pupil_, d, d);
        lv_obj_align(pupil_, LV_ALIGN_CENTER, 0, 0);
    }
}

void EyeDisplay::SetIrisColor(lv_color_t color) {
    if (iris_) lv_obj_set_style_bg_color(iris_, color, 0);
}

void EyeDisplay::SetScleraSize(int w, int h) {
    if (sclera_) {
        lv_obj_set_size(sclera_, w, h);
        lv_obj_align(sclera_, LV_ALIGN_CENTER, 0, 0);
    }
}

void EyeDisplay::SetEyebrow(int angle_01deg, int y_offset, bool visible) {
    if (!eyebrow_) return;
    if (visible) {
        lv_obj_remove_flag(eyebrow_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_transform_rotation(eyebrow_, angle_01deg, 0);
        lv_obj_align(eyebrow_, LV_ALIGN_CENTER, 0, y_offset);
    } else {
        lv_obj_add_flag(eyebrow_, LV_OBJ_FLAG_HIDDEN);
    }
}

void EyeDisplay::SetDecor(int x, int y, int w, int h, lv_color_t color, bool visible) {
    if (!decor_) return;
    if (visible) {
        lv_obj_remove_flag(decor_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(decor_, w, h);
        lv_obj_align(decor_, LV_ALIGN_CENTER, x, y);
        lv_obj_set_style_bg_color(decor_, color, 0);
    } else {
        lv_obj_add_flag(decor_, LV_OBJ_FLAG_HIDDEN);
    }
}

void EyeDisplay::SetDecor2(int x, int y, int w, int h, lv_color_t color, bool visible) {
    if (!decor2_) return;
    if (visible) {
        lv_obj_remove_flag(decor2_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(decor2_, w, h);
        lv_obj_align(decor2_, LV_ALIGN_CENTER, x, y);
        lv_obj_set_style_bg_color(decor2_, color, 0);
    } else {
        lv_obj_add_flag(decor2_, LV_OBJ_FLAG_HIDDEN);
    }
}

bool EyeDisplay::Lock(int timeout_ms) {
    return lvgl_port_lock(timeout_ms);
}

void EyeDisplay::Unlock() {
    lvgl_port_unlock();
}
