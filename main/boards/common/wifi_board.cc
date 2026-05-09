#include "wifi_board.h"

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

static const char *TAG = "WifiBoard";

WifiBoard::WifiBoard(ProvisioningMode provisioning_mode)
    : provisioning_mode_(provisioning_mode) {
    Settings settings("wifi", true);
    wifi_config_mode_ = settings.GetInt("force_ap") == 1;
    if (wifi_config_mode_) {
        ESP_LOGI(TAG, "force_ap is set to 1, reset to 0");
        settings.SetInt("force_ap", 0);
    }
}

std::string WifiBoard::GetBoardType() {
    return "wifi";
}

void WifiBoard::EnterProvisioningMode() {
    wifi_config_mode_ = true;
    switch (provisioning_mode_) {
    case ProvisioningMode::Ble:
#if CONFIG_USE_BLE_DATA_SERVICE
        EnterBleProvisioningMode();
#else
        {
            auto& application = Application::GetInstance();
            application.SetDeviceState(kDeviceStateWifiConfiguring);
            ESP_LOGE(TAG, "BLE provisioning selected but CONFIG_USE_BLE_DATA_SERVICE is disabled");
            while (true) {
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
        }
#endif
        return;
    case ProvisioningMode::WifiAp:
    default:
        break;
    }

    EnterWifiApProvisioningMode();
}

void WifiBoard::EnterWifiApProvisioningMode() {
    auto& application = Application::GetInstance();
    application.SetDeviceState(kDeviceStateWifiConfiguring);

    auto& wifi_ap = WifiConfigurationAp::GetInstance();
    wifi_ap.SetLanguage(Lang::CODE);
    wifi_ap.SetSsidPrefix("Xiaozhi");
    wifi_ap.Start();

    // 显示 WiFi 配置 AP 的 SSID 和 Web 服务器 URL
    std::string hint = Lang::Strings::CONNECT_TO_HOTSPOT;
    hint += wifi_ap.GetSsid();
    hint += Lang::Strings::ACCESS_VIA_BROWSER;
    hint += wifi_ap.GetWebServerUrl();
    hint += "\n\n";
    
    // 播报配置 WiFi 的提示
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
    
    // Wait forever until reset after configuration
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

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
        if (resp_str != nullptr) {
            ble.Notify(reinterpret_cast<const uint8_t*>(resp_str), strlen(resp_str));
            cJSON_free(resp_str);
        }
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
        if (resp_str != nullptr) {
            ble.Notify(reinterpret_cast<const uint8_t*>(resp_str), strlen(resp_str));
            cJSON_free(resp_str);
        }
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
    application.Alert(Lang::Strings::WIFI_CONFIG_MODE, "Use BLE to configure Wi-Fi", "",
                      Lang::Sounds::OGG_WIFICONFIG);

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

void WifiBoard::StartNetwork() {
    // User can press BOOT button while starting to enter WiFi configuration mode
    if (wifi_config_mode_) {
        EnterProvisioningMode();
        return;
    }

    // If no WiFi SSID is configured, enter WiFi configuration mode
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
        notification += "...";
        display->ShowNotification(notification.c_str(), 30000);
    });
    wifi_station.OnConnected([this](const std::string& ssid) {
        auto display = Board::GetInstance().GetDisplay();
        std::string notification = Lang::Strings::CONNECTED_TO;
        notification += ssid;
        display->ShowNotification(notification.c_str(), 30000);
    });
    wifi_station.Start();

    // Try to connect to WiFi, if failed, launch the WiFi configuration AP
    if (!wifi_station.WaitForConnected(60 * 1000)) {
        wifi_station.Stop();
        wifi_config_mode_ = true;
        EnterProvisioningMode();
        return;
    }
}

NetworkInterface* WifiBoard::GetNetwork() {
    static EspNetwork network;
    return &network;
}

const char* WifiBoard::GetNetworkStateIcon() {
    if (wifi_config_mode_) {
        return FONT_AWESOME_WIFI;
    }
    auto& wifi_station = WifiStation::GetInstance();
    if (!wifi_station.IsConnected()) {
        return FONT_AWESOME_WIFI_OFF;
    }
    int8_t rssi = wifi_station.GetRssi();
    if (rssi >= -60) {
        return FONT_AWESOME_WIFI;
    } else if (rssi >= -70) {
        return FONT_AWESOME_WIFI_FAIR;
    } else {
        return FONT_AWESOME_WIFI_WEAK;
    }
}

std::string WifiBoard::GetBoardJson() {
    // Set the board type for OTA
    auto& wifi_station = WifiStation::GetInstance();
    std::string board_json = R"({)";
    board_json += R"("type":")" + std::string(BOARD_TYPE) + R"(",)";
    board_json += R"("name":")" + std::string(BOARD_NAME) + R"(",)";
    if (!wifi_config_mode_) {
        board_json += R"("ssid":")" + wifi_station.GetSsid() + R"(",)";
        board_json += R"("rssi":)" + std::to_string(wifi_station.GetRssi()) + R"(,)";
        board_json += R"("channel":)" + std::to_string(wifi_station.GetChannel()) + R"(,)";
        board_json += R"("ip":")" + wifi_station.GetIpAddress() + R"(",)";
    }
    board_json += R"("mac":")" + SystemInfo::GetMacAddress() + R"(")";
    board_json += R"(})";
    return board_json;
}

void WifiBoard::SetPowerSaveMode(bool enabled) {
    auto& wifi_station = WifiStation::GetInstance();
    wifi_station.SetPowerSaveMode(enabled);
}

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

std::string WifiBoard::GetDeviceStatusJson() {
    /*
     * 返回设备状态JSON
     * 
     * 返回的JSON结构如下：
     * {
     *     "audio_speaker": {
     *         "volume": 70
     *     },
     *     "screen": {
     *         "brightness": 100,
     *         "theme": "light"
     *     },
     *     "battery": {
     *         "level": 50,
     *         "charging": true
     *     },
     *     "network": {
     *         "type": "wifi",
     *         "ssid": "Xiaozhi",
     *         "rssi": -60
     *     },
     *     "chip": {
     *         "temperature": 25
     *     }
     * }
     */
    auto& board = Board::GetInstance();
    auto root = cJSON_CreateObject();

    // Audio speaker
    auto audio_speaker = cJSON_CreateObject();
    auto audio_codec = board.GetAudioCodec();
    if (audio_codec) {
        cJSON_AddNumberToObject(audio_speaker, "volume", audio_codec->output_volume());
    }
    cJSON_AddItemToObject(root, "audio_speaker", audio_speaker);

    // Screen brightness
    auto backlight = board.GetBacklight();
    auto screen = cJSON_CreateObject();
    if (backlight) {
        cJSON_AddNumberToObject(screen, "brightness", backlight->brightness());
    }
    auto display = board.GetDisplay();
    if (display && display->height() > 64) { // For LCD display only
        cJSON_AddStringToObject(screen, "theme", display->GetTheme().c_str());
    }
    cJSON_AddItemToObject(root, "screen", screen);

    // Battery
    int battery_level = 0;
    bool charging = false;
    bool discharging = false;
    if (board.GetBatteryLevel(battery_level, charging, discharging)) {
        cJSON* battery = cJSON_CreateObject();
        cJSON_AddNumberToObject(battery, "level", battery_level);
        cJSON_AddBoolToObject(battery, "charging", charging);
        cJSON_AddItemToObject(root, "battery", battery);
    }

    // Network
    auto network = cJSON_CreateObject();
    auto& wifi_station = WifiStation::GetInstance();
    cJSON_AddStringToObject(network, "type", "wifi");
    cJSON_AddStringToObject(network, "ssid", wifi_station.GetSsid().c_str());
    int rssi = wifi_station.GetRssi();
    if (rssi >= -60) {
        cJSON_AddStringToObject(network, "signal", "strong");
    } else if (rssi >= -70) {
        cJSON_AddStringToObject(network, "signal", "medium");
    } else {
        cJSON_AddStringToObject(network, "signal", "weak");
    }
    cJSON_AddItemToObject(root, "network", network);

    // Chip
    float esp32temp = 0.0f;
    if (board.GetTemperature(esp32temp)) {
        auto chip = cJSON_CreateObject();
        cJSON_AddNumberToObject(chip, "temperature", esp32temp);
        cJSON_AddItemToObject(root, "chip", chip);
    }

    auto json_str = cJSON_PrintUnformatted(root);
    std::string json(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);
    return json;
}
