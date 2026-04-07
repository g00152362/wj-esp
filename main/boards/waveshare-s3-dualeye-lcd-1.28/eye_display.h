#pragma once

#include "display.h"
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lvgl_port.h>

enum class EyeSide { LEFT, RIGHT };

enum class EyeDecor {
    NONE = 0,
    HEART,
    TEAR,
    ANGER,
    SWEAT,
    ZZZ,
    SPARKLE,
};

class EyeDisplay : public Display {
public:
    EyeDisplay(esp_lcd_panel_io_handle_t panel_io,
               esp_lcd_panel_handle_t panel,
               int width, int height,
               int offset_x, int offset_y,
               bool mirror_x, bool mirror_y, bool swap_xy,
               EyeSide side,
               bool gif_mode = false);

    void SetEmotion(const char* emotion) override;
    void SetStatus(const char* status) override;
    void SetChatMessage(const char* role, const char* content) override {}
    void SetIcon(const char* icon) override {}
    void ShowNotification(const char* message, int duration_ms = 3000) override {}
    void ShowNotification(const std::string& notification, int duration_ms = 3000) override {}
    void UpdateStatusBar(bool update_all = false) override {}
    void SetTheme(const std::string& theme_name) override {}

    void SetEyelidTopY(int y);
    void SetEyelidBottomY(int y);
    void SetIrisOffset(int dx, int dy);
    void SetPupilDiameter(int d);
    void SetIrisColor(lv_color_t color);
    void SetScleraSize(int w, int h);
    void SetEyebrow(int angle_01deg, int y_offset, bool visible);
    void SetDecor(int x, int y, int w, int h, lv_color_t color, bool visible);
    void SetDecor2(int x, int y, int w, int h, lv_color_t color, bool visible);

    lv_obj_t* GetEyelidTop() { return eyelid_top_; }
    lv_obj_t* GetEyelidBottom() { return eyelid_bottom_; }
    EyeSide GetSide() const { return side_; }
    bool IsGifMode() const { return gif_mode_; }
    lv_obj_t* GetGifObj() { return gif_obj_; }
    void SetPairDisplay(EyeDisplay* pair) { pair_ = pair; }

protected:
    bool Lock(int timeout_ms = 0) override;
    void Unlock() override;

private:
    EyeSide side_;
    bool gif_mode_;
    esp_lcd_panel_io_handle_t panel_io_;
    esp_lcd_panel_handle_t panel_;

    lv_obj_t* sclera_ = nullptr;
    lv_obj_t* iris_ = nullptr;
    lv_obj_t* pupil_ = nullptr;
    lv_obj_t* highlight_ = nullptr;
    lv_obj_t* eyelid_top_ = nullptr;
    lv_obj_t* eyelid_bottom_ = nullptr;
    lv_obj_t* eyebrow_ = nullptr;
    lv_obj_t* decor_ = nullptr;
    lv_obj_t* decor2_ = nullptr;

    lv_obj_t* gif_obj_ = nullptr;
    EyeDisplay* pair_ = nullptr;

    void CreateEyeObjects();
    void CreateGifObjects();
public:
    void LoadInitialGif();
};
