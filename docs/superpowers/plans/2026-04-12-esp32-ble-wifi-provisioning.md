# ESP32 BLE WiFi Provisioning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `AIToy-s3-dualeye-lcd-0.71` enter BLE WiFi provisioning instead of WiFi AP provisioning while keeping WiFi AP as the default `WifiBoard` behavior for boards that do not pass a provisioning mode.

**Architecture:** Keep `WifiBoard::StartNetwork()` as the only startup decision point, split the existing AP-specific provisioning code behind a dispatcher, and move the AIToy board's duplicated BLE provisioning logic into `WifiBoard`. This repo does not have a unit-test harness for board integration code, so use compile-first TDD: introduce the new call sites first to force a failing build, then add the minimum implementation to make the build pass, followed by target and compatibility builds plus on-device verification.

**Tech Stack:** ESP-IDF, C++, FreeRTOS, NimBLE (`BleDataService`), `SsidManager`, `WifiStation`, `WifiConfigurationAp`, `idf.py`

---

## File Structure

- Modify: `main/boards/common/wifi_board.h`
  Responsibility: declare the provisioning mode enum, constructor parameter, provisioning dispatcher, and BLE helper hooks while keeping AP provisioning declarations intact.
- Modify: `main/boards/common/wifi_board.cc`
  Responsibility: preserve the existing WiFi AP provisioning path, route startup/fallback through a new dispatcher, and own the BLE provisioning lifecycle plus BLE JSON payload handling.
- Modify: `main/boards/AIToy-s3-dualeye-lcd-0.71/esp32-s3-dualeye-lcd-0.71.cc`
  Responsibility: opt the board into BLE provisioning mode and remove the duplicate board-local BLE provisioning implementation so there is one authoritative flow.
- Verify only: `main/boards/common/ble_data_service.h`
- Verify only: `main/boards/common/ble_data_service.cpp`
  Responsibility: existing BLE GATT transport stays unchanged unless build/runtime verification exposes a blocker.

### Task 1: Add a Selectable Provisioning Mode to `WifiBoard`

**Files:**
- Modify: `main/boards/AIToy-s3-dualeye-lcd-0.71/esp32-s3-dualeye-lcd-0.71.cc:309-344`
- Modify: `main/boards/common/wifi_board.h:1-24`
- Modify: `main/boards/common/wifi_board.cc:22-29`
- Test: `sdkconfig`

- [ ] **Step 1: Write the failing build trigger in the AIToy board**

Change the AIToy board constructor to opt into BLE mode before `WifiBoard` knows about that mode:

```cpp
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
```

- [ ] **Step 2: Run the target build to verify it fails**

Run:

```powershell
idf.py build
```

Expected: FAIL with a compiler error containing either `ProvisioningMode` is not declared or `WifiBoard` has no matching constructor taking one argument.

- [ ] **Step 3: Add the provisioning mode enum and constructor declaration to `WifiBoard`**

Replace the current header body with this declaration shape:

```cpp
#ifndef WIFI_BOARD_H
#define WIFI_BOARD_H

#include "board.h"

enum class ProvisioningMode {
    WifiAp,
    Ble,
};

class WifiBoard : public Board {
protected:
    bool wifi_config_mode_ = false;
    ProvisioningMode provisioning_mode_ = ProvisioningMode::WifiAp;
    void EnterProvisioningMode();
    void EnterWifiApProvisioningMode();
    virtual std::string GetBoardJson() override;

public:
    explicit WifiBoard(ProvisioningMode provisioning_mode = ProvisioningMode::WifiAp);
    virtual std::string GetBoardType() override;
    virtual void StartNetwork() override;
    virtual NetworkInterface* GetNetwork() override;
    virtual const char* GetNetworkStateIcon() override;
    virtual void SetPowerSaveMode(bool enabled) override;
    virtual void ResetWifiConfiguration();
    virtual AudioCodec* GetAudioCodec() override { return nullptr; }
    virtual std::string GetDeviceStatusJson() override;
};

#endif // WIFI_BOARD_H
```

- [ ] **Step 4: Implement the new constructor signature in `wifi_board.cc`**

Replace the constructor definition with:

```cpp
WifiBoard::WifiBoard(ProvisioningMode provisioning_mode)
    : provisioning_mode_(provisioning_mode) {
    Settings settings("wifi", true);
    wifi_config_mode_ = settings.GetInt("force_ap") == 1;
    if (wifi_config_mode_) {
        ESP_LOGI(TAG, "force_ap is set to 1, reset to 0");
        settings.SetInt("force_ap", 0);
    }
}
```

- [ ] **Step 5: Run the target build to verify it passes**

Run:

```powershell
idf.py build
```

Expected: PASS. The build should finish successfully for the existing `CONFIG_BOARD_TYPE_ESP32S3_DUALEYE_LCD_0_71=y` configuration.

- [ ] **Step 6: Commit the constructor-mode scaffolding**

Run:

```powershell
git add main/boards/common/wifi_board.h main/boards/common/wifi_board.cc main/boards/AIToy-s3-dualeye-lcd-0.71/esp32-s3-dualeye-lcd-0.71.cc
git commit -m "refactor: add selectable WifiBoard provisioning mode"
```

Expected: a commit containing only the enum, constructor, and the AIToy board's base-constructor call.

### Task 2: Route Startup Through a Provisioning Dispatcher While Keeping AP Mode

**Files:**
- Modify: `main/boards/common/wifi_board.h:6-19`
- Modify: `main/boards/common/wifi_board.cc:35-113`
- Test: `sdkconfig`

- [ ] **Step 1: Write the failing build trigger by changing the call sites before the dispatcher exists**

Update the declaration and startup call sites so `StartNetwork()` routes through a new dispatcher even though it is not implemented yet:

```cpp
class WifiBoard : public Board {
protected:
    bool wifi_config_mode_ = false;
    ProvisioningMode provisioning_mode_ = ProvisioningMode::WifiAp;
    void EnterProvisioningMode();
    void EnterWifiApProvisioningMode();
    virtual std::string GetBoardJson() override;

public:
    explicit WifiBoard(ProvisioningMode provisioning_mode = ProvisioningMode::WifiAp);
    virtual std::string GetBoardType() override;
    virtual void StartNetwork() override;
    virtual NetworkInterface* GetNetwork() override;
    virtual const char* GetNetworkStateIcon() override;
    virtual void SetPowerSaveMode(bool enabled) override;
    virtual void ResetWifiConfiguration();
    virtual AudioCodec* GetAudioCodec() override { return nullptr; }
    virtual std::string GetDeviceStatusJson() override;
};
```

```cpp
void WifiBoard::StartNetwork() {
    if (wifi_config_mode_) {
        EnterProvisioningMode();
        return;
    }

    auto& ssid_manager = SsidManager::GetInstance();
    auto ssid_list = ssid_manager.GetSsidList();
    if (ssid_list.empty()) {
        wifi_config_mode_ = true;
        EnterProvisioningMode();
        return;
    }

    auto& wifi_station = WifiStation::GetInstance();
    wifi_station.OnScanBegin([this]() {
        auto display = Board::GetInstance().GetDisplay();
        display->ShowNotification(Lang::Strings::SCANNING_WIFI, 30000);
    });
    wifi_station.OnConnect([this](const std::string& ssid) {
        auto display = Board::GetInstance().GetDisplay();
        std::string notification = Lang::Strings::CONNECT_TO;
        notification += ssid;
        notification += ".";
        notification += ".";
        notification += ".";
        display->ShowNotification(notification.c_str(), 30000);
    });
    wifi_station.OnConnected([this](const std::string& ssid) {
        auto display = Board::GetInstance().GetDisplay();
        std::string notification = Lang::Strings::CONNECTED_TO;
        notification += ssid;
        display->ShowNotification(notification.c_str(), 30000);
    });
    wifi_station.Start();

    if (!wifi_station.WaitForConnected(60 * 1000)) {
        wifi_station.Stop();
        wifi_config_mode_ = true;
        EnterProvisioningMode();
        return;
    }
}
```

- [ ] **Step 2: Run the target build to verify it fails**

Run:

```powershell
idf.py build
```

Expected: FAIL with either `no declaration matches 'void WifiBoard::EnterWifiConfigMode()'` or an undefined-reference error for `WifiBoard::EnterProvisioningMode()`.

- [ ] **Step 3: Implement the dispatcher and rename the old AP entry point**

Replace the old AP-only method with this dispatcher plus the renamed AP implementation:

```cpp
void WifiBoard::EnterProvisioningMode() {
    wifi_config_mode_ = true;
    if (provisioning_mode_ == ProvisioningMode::Ble) {
#if CONFIG_USE_BLE_DATA_SERVICE
        EnterBleProvisioningMode();
#else
        auto& application = Application::GetInstance();
        application.SetDeviceState(kDeviceStateWifiConfiguring);
        ESP_LOGE(TAG, "BLE provisioning selected but CONFIG_USE_BLE_DATA_SERVICE is disabled");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(10000));
        }
#endif
        return;
    }

    // Legacy AP provisioning path is kept for compatibility with existing boards.
    EnterWifiApProvisioningMode();
}

void WifiBoard::EnterWifiApProvisioningMode() {
    auto& application = Application::GetInstance();
    application.SetDeviceState(kDeviceStateWifiConfiguring);

    auto& wifi_ap = WifiConfigurationAp::GetInstance();
    wifi_ap.SetLanguage(Lang::CODE);
    wifi_ap.SetSsidPrefix("Xiaozhi");
    wifi_ap.Start();

    std::string hint = Lang::Strings::CONNECT_TO_HOTSPOT;
    hint += wifi_ap.GetSsid();
    hint += Lang::Strings::ACCESS_VIA_BROWSER;
    hint += wifi_ap.GetWebServerUrl();
    hint += "\n\n";

    application.Alert(Lang::Strings::WIFI_CONFIG_MODE, hint.c_str(), "", Lang::Sounds::OGG_WIFICONFIG);

#if CONFIG_USE_ACOUSTIC_WIFI_PROVISIONING
    auto display = Board::GetInstance().GetDisplay();
    auto codec = Board::GetInstance().GetAudioCodec();
    int channel = 1;
    if (codec) {
        channel = codec->input_channels();
    }
    ESP_LOGI(TAG, "Start receiving WiFi credentials from audio, input channels: %d", channel);
    audio_wifi_config::ReceiveWifiCredentialsFromAudio(&application, &wifi_ap, display, channel);
#endif

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
```

Keep `ResetWifiConfiguration()` behavior intact, but update its comment to match the new semantics:

```cpp
void WifiBoard::ResetWifiConfiguration() {
    // Set a flag and reboot the device to force provisioning on the next boot.
    {
        Settings settings("wifi", true);
        settings.SetInt("force_ap", 1);
    }
    GetDisplay()->ShowNotification(Lang::Strings::ENTERING_WIFI_CONFIG_MODE);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}
```

- [ ] **Step 4: Run the target build to verify the dispatcher passes**

Run:

```powershell
idf.py build
```

Expected: PASS. The target board should still compile even though BLE mode is not implemented yet because the dispatcher compiles behind the `CONFIG_USE_BLE_DATA_SERVICE` guard.

- [ ] **Step 5: Commit the dispatcher split**

Run:

```powershell
git add main/boards/common/wifi_board.h main/boards/common/wifi_board.cc
git commit -m "refactor: route WifiBoard startup through provisioning dispatcher"
```

Expected: a commit that preserves AP mode but centralizes the provisioning decision.

### Task 3: Move BLE Provisioning Ownership into `WifiBoard`

**Files:**
- Modify: `main/boards/common/wifi_board.h:1-24`
- Modify: `main/boards/common/wifi_board.cc:1-170`
- Modify: `main/boards/AIToy-s3-dualeye-lcd-0.71/esp32-s3-dualeye-lcd-0.71.cc:12-19`
- Modify: `main/boards/AIToy-s3-dualeye-lcd-0.71/esp32-s3-dualeye-lcd-0.71.cc:189-343`
- Test: `sdkconfig`

- [ ] **Step 1: Write the failing build trigger by declaring the BLE entry points without implementations**

Extend `wifi_board.h` with the BLE helper declarations and state field:

```cpp
#ifndef WIFI_BOARD_H
#define WIFI_BOARD_H

#include "board.h"

enum class ProvisioningMode {
    WifiAp,
    Ble,
};

class WifiBoard : public Board {
protected:
    bool wifi_config_mode_ = false;
    ProvisioningMode provisioning_mode_ = ProvisioningMode::WifiAp;
    void EnterProvisioningMode();
    void EnterWifiApProvisioningMode();
#if CONFIG_USE_BLE_DATA_SERVICE
    bool ble_provisioning_active_ = false;
    void EnterBleProvisioningMode();
    bool StartBleProvisioning();
    void StopBleProvisioning();
    void HandleBleProvisioningData(const uint8_t* data, size_t len);
#endif
    virtual std::string GetBoardJson() override;

public:
    explicit WifiBoard(ProvisioningMode provisioning_mode = ProvisioningMode::WifiAp);
    virtual std::string GetBoardType() override;
    virtual void StartNetwork() override;
    virtual NetworkInterface* GetNetwork() override;
    virtual const char* GetNetworkStateIcon() override;
    virtual void SetPowerSaveMode(bool enabled) override;
    virtual void ResetWifiConfiguration();
    virtual AudioCodec* GetAudioCodec() override { return nullptr; }
    virtual std::string GetDeviceStatusJson() override;
};
```

Keep the dispatcher branch from Task 2 calling `EnterBleProvisioningMode()` even though the implementation does not exist yet.

- [ ] **Step 2: Run the target build to verify it fails**

Run:

```powershell
idf.py build
```

Expected: FAIL with undefined references for one or more of `EnterBleProvisioningMode`, `StartBleProvisioning`, `StopBleProvisioning`, or `HandleBleProvisioningData`.

- [ ] **Step 3: Implement BLE provisioning in `wifi_board.cc`**

Add the required BLE includes near the top of the file:

```cpp
#include "display.h"
#include "application.h"
#include "system_info.h"
#include "font_awesome_symbols.h"
#include "settings.h"
#include "assets/lang_config.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_network.h>
#include <esp_log.h>

#include <wifi_station.h>
#include <wifi_configuration_ap.h>
#include <ssid_manager.h>
#include "afsk_demod.h"

#if CONFIG_USE_BLE_DATA_SERVICE
#include "ble_data_service.h"
#include <cJSON.h>
#include <cstring>
#endif
```

Add the BLE helper implementation under the existing AP code:

```cpp
#if CONFIG_USE_BLE_DATA_SERVICE
namespace {
constexpr size_t kMaxBleSsidLength = 32;
constexpr size_t kMaxBlePasswordLength = 64;
}

bool WifiBoard::StartBleProvisioning() {
    if (ble_provisioning_active_) {
        return true;
    }

    auto& ble = BleDataService::GetInstance();
    ble.SetOnReceive([this](const uint8_t* data, size_t len) {
        HandleBleProvisioningData(data, len);
    });
    ble.SetOnConnect([](uint16_t conn_handle) {
        ESP_LOGI(TAG, "BLE client connected, handle=%d", conn_handle);
    });
    ble.SetOnDisconnect([](uint16_t conn_handle, int reason) {
        ESP_LOGI(TAG, "BLE client disconnected, handle=%d, reason=%d", conn_handle, reason);
    });

    esp_err_t ret = ble.Init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start BLE provisioning: %s", esp_err_to_name(ret));
        return false;
    }

    ble_provisioning_active_ = true;
    return true;
}

void WifiBoard::StopBleProvisioning() {
    if (!ble_provisioning_active_) {
        return;
    }

    BleDataService::GetInstance().Deinit();
    ble_provisioning_active_ = false;
}

void WifiBoard::HandleBleProvisioningData(const uint8_t* data, size_t len) {
    std::string json_str(reinterpret_cast<const char*>(data), len);
    cJSON* root = cJSON_Parse(json_str.c_str());
    if (!root) {
        ESP_LOGW(TAG, "BLE provisioning payload is not valid JSON");
        return;
    }

    cJSON* type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type)) {
        ESP_LOGW(TAG, "BLE provisioning payload is missing the type field");
        cJSON_Delete(root);
        return;
    }

    auto& ble = BleDataService::GetInstance();
    if (strcmp(type->valuestring, "read") == 0) {
        cJSON* resp = cJSON_CreateObject();
        std::string mac = SystemInfo::GetMacAddress();
        cJSON_AddStringToObject(resp, "macAddress", mac.c_str());
        char* resp_str = cJSON_PrintUnformatted(resp);
        ble.Notify(reinterpret_cast<const uint8_t*>(resp_str), strlen(resp_str));
        cJSON_free(resp_str);
        cJSON_Delete(resp);
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type->valuestring, "write") == 0) {
        cJSON* ssid = cJSON_GetObjectItem(root, "ssid");
        cJSON* pwd = cJSON_GetObjectItem(root, "pwd");
        if (!cJSON_IsString(ssid) || !cJSON_IsString(pwd)) {
            ESP_LOGW(TAG, "BLE write is missing ssid or pwd");
            cJSON_Delete(root);
            return;
        }

        size_t ssid_len = strlen(ssid->valuestring);
        size_t pwd_len = strlen(pwd->valuestring);
        if (ssid_len == 0 || ssid_len > kMaxBleSsidLength || pwd_len > kMaxBlePasswordLength) {
            ESP_LOGW(TAG, "BLE write contains an invalid ssid or password length");
            cJSON_Delete(root);
            return;
        }

        SsidManager::GetInstance().AddSsid(ssid->valuestring, pwd->valuestring);

        cJSON* resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "status", "ok");
        char* resp_str = cJSON_PrintUnformatted(resp);
        ble.Notify(reinterpret_cast<const uint8_t*>(resp_str), strlen(resp_str));
        cJSON_free(resp_str);
        cJSON_Delete(resp);
        cJSON_Delete(root);

        ESP_LOGI(TAG, "BLE provisioning saved WiFi credentials, rebooting in 1s");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
        return;
    }

    ESP_LOGW(TAG, "BLE provisioning received unsupported type: %s", type->valuestring);
    cJSON_Delete(root);
}

void WifiBoard::EnterBleProvisioningMode() {
    auto& application = Application::GetInstance();
    application.SetDeviceState(kDeviceStateWifiConfiguring);
    application.Alert(Lang::Strings::WIFI_CONFIG_MODE, "Use BLE to configure Wi-Fi", "", Lang::Sounds::OGG_WIFICONFIG);

    StopBleProvisioning();
    if (!StartBleProvisioning()) {
        auto display = Board::GetInstance().GetDisplay();
        display->ShowNotification("BLE provisioning failed", 30000);
    }

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
#endif
```

- [ ] **Step 4: Remove the duplicate board-local BLE provisioning code from the AIToy board**

Delete the BLE-only include block at `main/boards/AIToy-s3-dualeye-lcd-0.71/esp32-s3-dualeye-lcd-0.71.cc:12-19` and delete the entire `#ifdef CONFIG_USE_BLE_DATA_SERVICE` block at `:189-307`.

The top of the file should look like this after cleanup:

```cpp
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
```

And the constructor block should stay on the centralized base-class path:

```cpp
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
```

- [ ] **Step 5: Run the target build to verify BLE ownership now compiles**

Run:

```powershell
idf.py build
```

Expected: PASS. The target board should compile with BLE provisioning now owned by `WifiBoard`.

- [ ] **Step 6: Commit the centralized BLE provisioning flow**

Run:

```powershell
git add main/boards/common/wifi_board.h main/boards/common/wifi_board.cc main/boards/AIToy-s3-dualeye-lcd-0.71/esp32-s3-dualeye-lcd-0.71.cc
git commit -m "feat: move BLE WiFi provisioning into WifiBoard"
```

Expected: a commit that removes the duplicate AIToy BLE lifecycle and centralizes provisioning behavior.

### Task 4: Verify Default AP Compatibility and Run Hardware Scenarios

**Files:**
- Verify: `sdkconfig`
- Verify: `main/boards/common/wifi_board.cc`
- Verify: `main/boards/esp32-s3-touch-lcd-1.85c/esp32-s3-touch-lcd-1.85c.cc`

- [ ] **Step 1: Rebuild the AIToy target as the baseline verification**

Run:

```powershell
idf.py build
```

Expected: PASS. The baseline board still builds cleanly after the centralized BLE changes.

- [ ] **Step 2: Run a compatibility build for an unchanged AP-based `WifiBoard`**

Run these commands from the repo root:

```powershell
Copy-Item sdkconfig sdkconfig.aitoy.bak
idf.py set-target esp32s3
@'
CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_1_85C=y
'@ | Add-Content sdkconfig
idf.py -DBOARD_NAME=compat-ap-check build
Move-Item -Force sdkconfig.aitoy.bak sdkconfig
```

Expected: PASS. The unchanged `esp32-s3-touch-lcd-1.85c` board should build without being modified, proving that the default `WifiBoard` constructor still selects WiFi AP provisioning.

- [ ] **Step 3: Rebuild the AIToy target after restoring `sdkconfig`**

Run:

```powershell
idf.py build
```

Expected: PASS. Restoring the original `sdkconfig` should put the workspace back on the AIToy target with no manual cleanup required.

- [ ] **Step 4: Run the no-credentials BLE provisioning hardware check**

Flash and monitor the device with no saved SSIDs, then verify provisioning mode:

```powershell
idf.py flash monitor
```

Expected hardware behavior:

- The device enters `kDeviceStateWifiConfiguring`.
- The provisioning LED keeps its existing blink pattern.
- Monitor output includes a provisioning-state transition and BLE startup logs such as `BLE Data Service initialized` and `Advertising started`.

- [ ] **Step 5: Run the valid BLE write hardware check**

Create or enable a hotspot named `codex-test-ap` with password `codex-test-1234`, then use the existing mobile app or BLE test tool to send these payloads in order while `idf.py monitor` is running:

```json
{"type":"read"}
{"type":"write","ssid":"codex-test-ap","pwd":"codex-test-1234"}
```

Expected hardware behavior:

- The `read` payload returns a JSON response containing `macAddress`.
- The `write` payload returns `{"status":"ok"}`.
- Monitor output includes a log equivalent to `BLE provisioning saved WiFi credentials, rebooting in 1s`.
- After reboot, the device connects to the configured AP and continues into the normal chat startup flow.

- [ ] **Step 6: Run the invalid BLE write hardware check**

Send an invalid provisioning payload while `idf.py monitor` is still attached:

```json
{"type":"write","ssid":"","pwd":"123"}
```

Expected hardware behavior:

- The device logs an invalid-length warning.
- The device does not reboot.
- No WiFi credentials are overwritten in NVS.

- [ ] **Step 7: Finish with a clean workspace check**

Run:

```powershell
git status --short
```

Expected: only the implementation files and any intentionally updated docs are modified. There should be no accidental `sdkconfig` drift after restoring the backup in Step 2.
