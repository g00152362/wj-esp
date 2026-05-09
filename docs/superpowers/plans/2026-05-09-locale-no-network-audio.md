# Locale No-Network Audio Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add locale-specific playback for the no-network prompt while keeping the existing shared audio fallback.

**Architecture:** Extend `Application` with a file-based sound playback helper, add a Wi-Fi board helper that prefers locale-specific clips located under `main/assets/locales/<locale>/`, and update the timeout branch in `WifiBoard::StartNetwork()` to use the helper with fallback logging.

**Tech Stack:** ESP-IDF, C++, POSIX file I/O, existing audio service.

---

### Task 1: Add Application file-play helper

**Files:**
- Modify: `main/application.h`
- Modify: `main/application.cc`
- Modify: `main/audio/audio_service.h`
- Modify: `main/audio/audio_service.cc`

- [ ] **Step 1: Declare helper in header**

```cpp
// main/application.h
class Application {
public:
    bool PlaySoundFromFile(const char* path);
    // existing declarations...
};
```

- [ ] **Step 2: Implement helper**

```cpp
// main/application.cc
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

bool Application::PlaySoundFromFile(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        ESP_LOGW(TAG, "Failed to open sound file: %s", path);
        return false;
    }

    struct stat st {};
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        ESP_LOGW(TAG, "Invalid sound file: %s", path);
        close(fd);
        return false;
    }

    std::vector<uint8_t> buffer(static_cast<size_t>(st.st_size));
    ssize_t read_bytes = read(fd, buffer.data(), buffer.size());
    close(fd);
    if (read_bytes != st.st_size) {
        ESP_LOGW(TAG, "Failed to read sound file: %s", path);
        return false;
    }

    audio_service_.PlaySoundFromBuffer(buffer.data(), buffer.size());
    return true;
}
```

- [ ] **Step 3: Update audio service to accept raw buffers**

```cpp
// main/audio/audio_service.h
class AudioService {
public:
    void PlaySound(const std::string_view& sound);
    void PlaySoundFromBuffer(const uint8_t* data, size_t size);
};
```

```cpp
// main/audio/audio_service.cc
void AudioService::PlaySoundFromBuffer(const uint8_t* data, size_t size) {
    if (!codec_->output_enabled()) {
        esp_timer_stop(audio_power_timer_);
        esp_timer_start_periodic(audio_power_timer_, AUDIO_POWER_CHECK_INTERVAL_MS * 1000);
        codec_->EnableOutput(true);
    }

    size_t offset = 0;
    while (offset < size) {
        size_t chunk = std::min<size_t>(AUDIO_SERVICE_DECODE_CHUNK_SIZE, size - offset);
        decoder_->Feed(data + offset, chunk);
        offset += chunk;
    }
}
```

- [ ] **Step 4: Adjust existing PlaySound to reuse buffer API**

```cpp
void AudioService::PlaySound(const std::string_view& ogg) {
    PlaySoundFromBuffer(reinterpret_cast<const uint8_t*>(ogg.data()), ogg.size());
}
```

- [ ] **Step 5: Commit helper additions**

```bash
git add main/application.{h,cc} main/audio/audio_service.{h,cc}
git commit -m "feat: add file-based sound playback helper"
```

### Task 2: Locale path resolution helper

**Files:**
- Modify: `main/boards/common/wifi_board.h`
- Modify: `main/boards/common/wifi_board.cc`

- [ ] **Step 1: Add helper declaration**

```cpp
// main/boards/common/wifi_board.h
class WifiBoard : public Board {
protected:
    bool TryPlayLocalizedNoNetworkSound();
};
```

- [ ] **Step 2: Implement helper**

```cpp
// main/boards/common/wifi_board.cc
#include <sys/stat.h>

namespace {
    bool LocaleSoundExists(const char* path) {
        struct stat st {};
        return stat(path, &st) == 0 && st.st_size > 0;
    }
}

bool WifiBoard::TryPlayLocalizedNoNetworkSound() {
    static bool warned_missing = false;
    const char* locale = Lang::CODE;
    char path[256];
    snprintf(path, sizeof(path), "/spiffs/main/assets/locales/%s/no_network.ogg", locale);

    auto& application = Application::GetInstance();
    if (LocaleSoundExists(path)) {
        if (application.PlaySoundFromFile(path)) {
            return true;
        }
    } else if (!warned_missing) {
        ESP_LOGW(TAG, "Missing locale no_network.ogg for %s", locale);
        warned_missing = true;
    }

    application.PlaySound(Lang::Sounds::OGG_NO_NETWORK);
    return false;
}
```

- [ ] **Step 3: Use helper in StartNetwork**

```cpp
if (!wifi_station.WaitForConnected(60 * 1000)) {
    ShowNoNetworkPrompt();
    auto display = Board::GetInstance().GetDisplay();
    auto& application = Application::GetInstance();
    display->ShowNotification(Lang::Strings::NO_NETWORK, 30000);
    TryPlayLocalizedNoNetworkSound();
```

- [ ] **Step 4: Commit WifiBoard changes**

```bash
git add main/boards/common/wifi_board.{h,cc}
git commit -m "feat: prefer locale no-network sound"
```

### Task 3: Manual verification & cleanup

**Files:**
- N/A (manual)

- [ ] **Step 1: Flash or run firmware without locale file**

Observe logs for warning `Missing locale no_network.ogg` only once and ensure fallback audio plays.

- [ ] **Step 2: Add test locale file**

Copy `main/assets/common/no_network.ogg` to `main/assets/locales/en-US/no_network.ogg` and rerun; confirm helper reports success (log message optional).

- [ ] **Step 3: Push changes**

```bash
git push origin main
```

---

## Self-Review

- Spec coverage: locale detection, file existence check, helper API, fallback logging, and WifiBoard integration all addressed.
- Placeholder scan: no TBD/empty directives.
- Consistency: helper names and paths aligned with spec.
