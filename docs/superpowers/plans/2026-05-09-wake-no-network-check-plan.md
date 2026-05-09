# Wake-Time Network Check Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Insert a wake-time network availability gate that acknowledges the wake word, checks Wi-Fi/IP status, and plays/visualizes a locale-aware "network unavailable" prompt when needed.

**Architecture:** Extend the wake-word handler in `Application` with a synchronous network guard that leverages a new helper under `main/protocols`. The guard coordinates audio playback (locale assets) and delegates GIF display updates to the eye display modules. Asset lookup falls back to shared defaults when locale-specific resources are missing.

**Tech Stack:** ESP-IDF (C++), LVGL display layer, existing audio service, Wi-Fi station utilities, project asset pipeline.

---

### Task 1: Introduce NetworkGuard helper

**Files:**
- Create: `main/protocols/network_guard.h`
- Create: `main/protocols/network_guard.cc`
- Modify: `main/protocols/CMakeLists.txt`
- Test: _manual logging via device boot log_

- [ ] **Step 1: Define interface header**

```cpp
// main/protocols/network_guard.h
#pragma once

#include <chrono>
#include <string_view>

namespace NetworkGuard {

enum class NetworkCheckResult {
    kOk,
    kWifiDisconnected,
    kNoIp,
    kTimeout,
    kError,
};

NetworkCheckResult VerifyForInteraction(std::chrono::milliseconds timeout);
const char* ToString(NetworkCheckResult result);

}  // namespace NetworkGuard
```

- [ ] **Step 2: Implement helper logic**

```cpp
// main/protocols/network_guard.cc
#include "network_guard.h"

#include "wifi_station.h"
#include "esp_netif.h"
#include "esp_log.h"

namespace {
constexpr const char* kTag = "NetworkGuard";
}

namespace NetworkGuard {

NetworkCheckResult VerifyForInteraction(std::chrono::milliseconds timeout) {
    auto& wifi = WifiStation::GetInstance();
    if (!wifi.IsConnected()) {
        ESP_LOGW(kTag, "Wake check: Wi-Fi not connected");
        return NetworkCheckResult::kWifiDisconnected;
    }

    esp_netif_ip_info_t info;
    auto deadline = esp_timer_get_time() + timeout.count() * 1000;
    do {
        if (esp_netif_get_ip_info(wifi.GetNetif(), &info) == ESP_OK && info.ip.addr != 0) {
            return NetworkCheckResult::kOk;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    } while (esp_timer_get_time() < deadline);

    ESP_LOGW(kTag, "Wake check: IP unavailable");
    return NetworkCheckResult::kNoIp;
}

const char* ToString(NetworkCheckResult result) {
    switch (result) {
        case NetworkCheckResult::kOk:
            return "ok";
        case NetworkCheckResult::kWifiDisconnected:
            return "wifi_disconnected";
        case NetworkCheckResult::kNoIp:
            return "no_ip";
        case NetworkCheckResult::kTimeout:
            return "timeout";
        default:
            return "error";
    }
}

}  // namespace NetworkGuard
```

- [ ] **Step 3: Register component**

```cmake
# main/protocols/CMakeLists.txt
idf_component_register(
    SRCS "protocol.cc" "mqtt_protocol.cc" "websocket_protocol.cc" "network_guard.cc"
    INCLUDE_DIRS "."
    REQUIRES wifi_board display audio
)
```

- [ ] **Step 4: Format & self-check**

Run: `idf.py clang-format network_guard.{h,cc}` (or project formatting script)
Expected: no diff or consistent formatting

### Task 2: Wire guard into Application wake path

**Files:**
- Modify: `main/application.h`
- Modify: `main/application.cc`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: Declare helper trigger**

```cpp
// main/application.h
class Application {
    // ...
  private:
    bool HandleWakeNetworkCheck();
};
```

- [ ] **Step 2: Implement wake handling flow**

```cpp
// main/application.cc (inside Application class)
bool Application::HandleWakeNetworkCheck() {
    PlaySound(Lang::Sounds::OGG_WAKE_ACK);
    audio_service_.WaitForSoundComplete();

    using namespace std::chrono_literals;
    auto result = NetworkGuard::VerifyForInteraction(1500ms);
    if (result == NetworkGuard::NetworkCheckResult::kOk) {
        return true;
    }

    ESP_LOGW(TAG, "Wake network failed: %s", NetworkGuard::ToString(result));
    auto locale_sound = AssetManager::ResolveLocaleSound("no_network_wake");
    PlaySound(locale_sound);
    DisplayManager::GetInstance().ShowWakeNoNetworkGif();
    audio_service_.WaitForSoundComplete();
    DisplayManager::GetInstance().HideWakeNoNetworkGif();
    return false;
}
```

- [ ] **Step 3: Invoke from `WakeWordInvoke`**

```cpp
void Application::WakeWordInvoke(const std::string& wake_word) {
    if (device_state_ == kDeviceStateIdle) {
        if (!HandleWakeNetworkCheck()) {
            return;
        }
        ToggleChatState();
        Schedule([this, wake_word]() {
            if (protocol_) {
                protocol_->SendWakeWordDetected(wake_word);
            }
        });
```

- [ ] **Step 4: Add dependency**

```cmake
# main/CMakeLists.txt
set(SRCS
    application.cc
    ...
    protocols/network_guard.cc
)
```

- [ ] **Step 5: Build sanity**

Run: `idf.py reconfigure`
Expected: configuration succeeds

### Task 3: Extend audio service for blocking wait

**Files:**
- Modify: `main/audio/audio_service.h`
- Modify: `main/audio/audio_service.cc`

- [ ] **Step 1: Add wait API**

```cpp
// audio_service.h
void WaitForSoundComplete();
```

- [ ] **Step 2: Implement wait**

```cpp
// audio_service.cc
void AudioService::WaitForSoundComplete() {
    while (IsSoundPlaying()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

- [ ] **Step 3: Ensure thread safety**

Check `IsSoundPlaying()` uses internal mutex 

### Task 4: Display GIF helpers

**Files:**
- Modify: `main/display/eye_display.h`
- Modify: `main/display/eye_display.cc`
- Modify: `main/display/display.h`
- Modify: `main/display/display.cc`

- [ ] **Step 1: Declare interface**

```cpp
// display.h
virtual void ShowWakeNoNetworkGif(const AssetRef& asset) = 0;
virtual void HideWakeNoNetworkGif() = 0;
```

- [ ] **Step 2: Implement for eye display**

```cpp
void EyeDisplay::ShowWakeNoNetworkGif(const AssetRef& asset) {
    lvgl_port_lock();
    // pause animator, create LVGIF object from asset
    lvgl_port_unlock();
}

void EyeDisplay::HideWakeNoNetworkGif() {
    lvgl_port_lock();
    // delete object, resume animator
    lvgl_port_unlock();
}
```

- [ ] **Step 3: Default implementations**

Other display types no-op.

### Task 5: Asset resolution helpers

**Files:**
- Modify: `main/assets/lang_config.h`
- Modify: `main/assets/lang_config.cc`
- Modify: `main/assets/locales/en-US/language.json`
- Modify: `main/assets/locales/zh-CN/language.json`

- [ ] **Step 1: Register new sound keys**

```json
// en-US language.json
"wake_ack_audio": "wake_ack.ogg",
"wake_no_network_audio": "no_network_wake.ogg"
```

- [ ] **Step 2: Add to zh-CN**

```json
// zh-CN language.json
"wake_ack_audio": "wake_ack.ogg",
"wake_no_network_audio": "no_network_wake.ogg"
```

- [ ] **Step 3: Update lang_config**

```cpp
static const std::string_view OGG_WAKE_ACK = Lang::LookupLocaleSound("wake_ack_audio");
static const std::string_view OGG_WAKE_NO_NETWORK = Lang::LookupLocaleSound("wake_no_network_audio");
```

### Task 6: Integrate AssetManager display control

**Files:**
- Modify: `main/assets/asset_manager.h`
- Modify: `main/assets/asset_manager.cc`

- [ ] **Step 1: Provide `ResolveLocaleSound`**

```cpp
std::string_view AssetManager::ResolveLocaleSound(std::string_view key);
```

- [ ] **Step 2: Provide GIF resolver**

```cpp
AssetRef AssetManager::ResolveLocaleGif(std::string_view key);
```

### Task 7: Testing & verification

- [ ] Build: `idf.py build`
- [ ] Flash & monitor: `idf.py -p <port> flash monitor`
  - Expect wake words with network good proceed normally
  - With Wi-Fi disabled, expect “在的” + “网络不通了” + GIF
- [ ] Document log outcomes

---
