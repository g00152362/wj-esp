#include "wifi_board.h"
#include "audio/codecs/box_audio_codec.h"
#include "eye_display.h"
#include "eye_animator.h"
#include "sd_card.h"
#include "display_manager.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"

#include <esp_log.h>
#include "i2c_device.h"
#include <driver/i2c.h>
#include <driver/ledc.h>
#include <wifi_station.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_timer.h>
#include "esp_io_expander_tca95xx_16bit.h"
#include <driver/spi_common.h>

#include <esp_lcd_gc9d01.h>
#include <esp_lvgl_port.h>

#define TAG "AIToy_audio_board"

class CustomBoard : public WifiBoard
{
private:
    Button boot_button_;
    i2c_master_bus_handle_t i2c_bus_;
    esp_io_expander_handle_t io_expander = NULL;
    EyeDisplay *left_eye_;
    EyeDisplay *right_eye_;
    DisplayManager display_manager_;
    bool gif_mode_ = false;

    void InitializeI2c()
    {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = I2C_SDA_IO,
            .scl_io_num = I2C_SCL_IO,
            .clk_source = I2C_CLK_SRC_DEFAULT,
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    void InitializeSpi()
    {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = DISPLAY_MISO_PIN;
        buscfg.sclk_io_num = DISPLAY_SCLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeSdCard()
    {
        if (SdCardInit() && SdCardHasGifs())
        {
            gif_mode_ = true;
            ESP_LOGI(TAG, "SD card with GIF files detected, using GIF eye mode");
        }
        else
        {
            gif_mode_ = false;
            ESP_LOGI(TAG, "No SD card or GIF files, using LVGL eye mode");
        }
    }

    void InitializeLcdDisplay()
    {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = DISPLAY_SPI_SCLK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &panel_io));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RESET_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        // panel_config.rgb_ele_order = LCD_RGB_ENDIAN_BGR;
        panel_config.flags.reset_active_high = 0; // 这里必须是 0！！！
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9d01(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        vTaskDelay(pdMS_TO_TICKS(120)); // 必须等！
        esp_lcd_panel_init(panel);

        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        left_eye_ = new EyeDisplay(panel_io, panel,
                                   DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                   DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                   EyeSide::LEFT, gif_mode_);
    }

    void InitializeLcdDisplay_2()
    {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY2_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = DISPLAY_SPI_SCLK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &panel_io));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY2_RESET_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        panel_config.flags.reset_active_high = 0; // 这里必须是 0！！！
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9d01(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        vTaskDelay(pdMS_TO_TICKS(120)); // 必须等！
        esp_lcd_panel_init(panel);

        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY2_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY2_MIRROR_X, DISPLAY2_MIRROR_Y);

        right_eye_ = new EyeDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                    DISPLAY2_MIRROR_X, DISPLAY2_MIRROR_Y, DISPLAY2_SWAP_XY,
                                    EyeSide::RIGHT, gif_mode_);
    }

    void InitializeButtons()
    {
        boot_button_.OnClick([this]()
                             {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState(); });
    }
    void InitializeMotors()
    {

        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << MOTOR_CONTROL1,
            .mode = GPIO_MODE_OUTPUT, // 输出
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&cfg);
        gpio_set_level(MOTOR_CONTROL1, 1); // 拉高
        // gpio_config_t cfg1 = {
        //     .pin_bit_mask = 1ULL << MOTOR_CONTROL2,
        //     .mode = GPIO_MODE_OUTPUT, // 输出
        //     .pull_up_en = GPIO_PULLUP_DISABLE,
        //     .pull_down_en = GPIO_PULLDOWN_DISABLE,
        //     .intr_type = GPIO_INTR_DISABLE,
        // };
        // gpio_config(&cfg1);
        // gpio_set_level(MOTOR_CONTROL2, 1); // 拉高
    }
public:
    CustomBoard() : WifiBoard(ProvisioningMode::Ble), boot_button_(BOOT_BUTTON_GPIO)
    {
        InitializeI2c();
        InitializeSpi();
        InitializeSdCard();
        InitializeLcdDisplay();
        InitializeLcdDisplay_2();
        InitializeMotors();
        InitializeButtons();
        GetBacklight()->RestoreBrightness();
        GetBacklight2()->RestoreBrightness();
        if (gif_mode_)
        {
            left_eye_->SetPairDisplay(right_eye_);
            if (lvgl_port_lock(0))
            {
                left_eye_->LoadInitialGif();
                right_eye_->LoadInitialGif();
                lvgl_port_unlock();
            }
        }
        else
        {
            EyeAnimator::GetInstance().Init(left_eye_, right_eye_);
            EyeAnimator::GetInstance().Start();
        }

        DisplayManager::AddDisplay(left_eye_, true);
        DisplayManager::AddDisplay(right_eye_, false);
    }

    virtual AudioCodec *GetAudioCodec() override
    {
        static BoxAudioCodec audio_codec(i2c_bus_, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                                         AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
                                         AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR, AUDIO_CODEC_ES7210_ADDR, AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }

    virtual Display *GetDisplay() override
    {
        return &display_manager_;
    }

    virtual Backlight *GetBacklight() override
    {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    Backlight *GetBacklight2()
    {
        static PwmBacklight backlight(DISPLAY2_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
};

DECLARE_BOARD(CustomBoard);
