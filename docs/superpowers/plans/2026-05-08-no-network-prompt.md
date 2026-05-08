# No-Network Prompt Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the AIToy dual-eye "no network" prompt and retry flow from `docs/superpowers/specs/2026-05-08-no-network-prompt-design.md` without regressing existing Wi-Fi provisioning behavior.

**Architecture:** Keep `WifiBoard::StartNetwork()` as the single startup gate, but replace its "initial connect timed out" branch with a retry loop that stays inside `StartNetwork()` until Wi-Fi eventually connects. Expose the timeout UI as overridable `WifiBoard` hooks, then implement the actual dual-eye Wi-Fi-off presentation inside the AIToy board where `EyeDisplay` and `EyeAnimator` already exist. Store the two new spoken prompt clips under `main/assets/common/` so the existing asset pipeline exposes `OGG_NO_NETWORK` and `OGG_WIFI_CONNECTED` for every language build without hand-editing the generated `main/assets/lang_config.h`.

**Tech Stack:** ESP-IDF, C++, FreeRTOS, LVGL 9, `esp-wifi-connect`, generated language assets from `scripts/gen_lang.py`

**Execution Notes:** Run this plan from a dedicated git worktree. The current checkout is on `main` and already contains unrelated in-progress edits in `main/boards/common/wifi_board.*` and `main/boards/AIToy-s3-dualeye-lcd-0.71/*`; do not overwrite or revert them while implementing this plan.

---

## File Structure

- Modify: `main/assets/locales/zh-CN/language.json`
  Responsibility: add the new text prompts for the current Chinese build.
- Modify: `main/assets/locales/en-US/language.json`
  Responsibility: keep the base language complete so generated string constants exist for every locale.
- Create: `main/assets/common/no_network.ogg`
- Create: `main/assets/common/wifi_connected.ogg`
  Responsibility: provide build-global spoken prompt assets that `scripts/gen_lang.py` will expose via `Lang::Sounds`.
- Verify only: `main/assets/lang_config.h`
  Responsibility: generated output; never edit it by hand.
- Modify: `main/boards/common/wifi_board.h`
  Responsibility: declare overridable timeout prompt hooks that keep the common retry logic board-agnostic.
- Modify: `main/boards/common/wifi_board.cc`
  Responsibility: replace the current "timeout -> provisioning" path with the retry flow, keep `force_ap` and empty-SSID provisioning unchanged, and use `WifiStation`'s existing reconnect loop instead of repeated `Start()`/`Stop()`.
- Modify: `main/boards/AIToy-s3-dualeye-lcd-0.71/eye_animator.h`
- Modify: `main/boards/AIToy-s3-dualeye-lcd-0.71/eye_animator.cc`
  Responsibility: allow the dual-eye animation task to pause while the full-screen Wi-Fi-off overlay is active.
- Modify: `main/boards/AIToy-s3-dualeye-lcd-0.71/eye_display.h`
- Modify: `main/boards/AIToy-s3-dualeye-lcd-0.71/eye_display.cc`
  Responsibility: add an LVGL-safe full-screen icon overlay API for each eye display.
- Modify: `main/boards/AIToy-s3-dualeye-lcd-0.71/esp32-s3-dualeye-lcd-0.71.cc`
  Responsibility: override the new `WifiBoard` hooks and drive both eyes plus `EyeAnimator` during the no-network prompt.

### Task 1: Add the No-Network Strings and Prompt Audio Assets

**Files:**
- Create: `main/assets/common/no_network.ogg`
- Create: `main/assets/common/wifi_connected.ogg`
- Modify: `main/assets/locales/zh-CN/language.json`
- Modify: `main/assets/locales/en-US/language.json`
- Verify: `main/assets/lang_config.h`

- [ ] **Step 1: Save the supplied spoken prompt clips into the repo**

Place the user-provided files at these exact paths:

```text
main/assets/common/no_network.ogg
main/assets/common/wifi_connected.ogg
```

Run:

```powershell
Get-Item main/assets/common/no_network.ogg, main/assets/common/wifi_connected.ogg | Select-Object Name,Length
```

Expected: both files exist and `Length` is greater than 0.

- [ ] **Step 2: Add the new Chinese text prompts**

Update `main/assets/locales/zh-CN/language.json` so the Wi-Fi section contains:

```json
        "CONNECT_TO_HOTSPOT":"手机连接热点 ",
        "ACCESS_VIA_BROWSER":"，浏览器访问 ",
        "WIFI_CONFIG_MODE":"配网模式",
        "ENTERING_WIFI_CONFIG_MODE":"进入配网模式...",
        "SCANNING_WIFI":"扫描 Wi-Fi...",
        "NO_NETWORK":"无网络",
        "WIFI_CONNECTED":"网络已连接",
```

- [ ] **Step 3: Add the matching English base strings**

Update `main/assets/locales/en-US/language.json` so the Wi-Fi section contains:

```json
        "CONNECT_TO_HOTSPOT": "Hotspot: ",
        "ACCESS_VIA_BROWSER": " Config URL: ",
        "WIFI_CONFIG_MODE": "Wi-Fi Configuration Mode",
        "ENTERING_WIFI_CONFIG_MODE": "Entering Wi-Fi configuration mode...",
        "SCANNING_WIFI": "Scanning Wi-Fi...",
        "NO_NETWORK": "No network",
        "WIFI_CONNECTED": "Wi-Fi connected",
```

- [ ] **Step 4: Regenerate the language header**

Run:

```powershell
python scripts/gen_lang.py --language zh-CN --output main/assets/lang_config.h
```

Expected: the script prints `Successfully generated language config file: main/assets/lang_config.h`.

- [ ] **Step 5: Verify the generated identifiers exist**

Run:

```powershell
rg -n "NO_NETWORK|WIFI_CONNECTED|OGG_NO_NETWORK|OGG_WIFI_CONNECTED" main/assets/lang_config.h
```

Expected: one `Strings::NO_NETWORK`, one `Strings::WIFI_CONNECTED`, one `Sounds::OGG_NO_NETWORK`, and one `Sounds::OGG_WIFI_CONNECTED` entry are present.

- [ ] **Step 6: Commit the asset-pipeline changes**

Run:

```powershell
git add main/assets/common/no_network.ogg main/assets/common/wifi_connected.ogg main/assets/locales/zh-CN/language.json main/assets/locales/en-US/language.json main/assets/lang_config.h
git commit -m "feat: add no-network prompt assets"
```

Expected: the commit contains the two new `.ogg` files plus the JSON/header updates.

### Task 2: Replace the Initial Wi-Fi Timeout Fallback With a Retry Flow

**Files:**
- Modify: `main/boards/common/wifi_board.h`
- Modify: `main/boards/common/wifi_board.cc`
- Verify: `main/assets/lang_config.h`

- [ ] **Step 1: Write the failing build trigger by switching `StartNetwork()` to the new prompt flow before the hook methods exist**

Replace only the existing timeout branch in `main/boards/common/wifi_board.cc`:

```cpp
    if (!wifi_station.WaitForConnected(60 * 1000)) {
        auto& application = Application::GetInstance();
        auto display = Board::GetInstance().GetDisplay();

        ESP_LOGW(TAG, "Initial Wi-Fi connect timed out, showing no-network prompt");
        ShowNoNetworkPrompt();
        display->ShowNotification(Lang::Strings::NO_NETWORK, 30000);
        application.PlaySound(Lang::Sounds::OGG_NO_NETWORK);

        for (int attempt = 0; attempt < 3; ++attempt) {
            ESP_LOGI(TAG, "Foreground Wi-Fi retry %d/3", attempt + 1);
            if (wifi_station.WaitForConnected(30 * 1000)) {
                HideNoNetworkPrompt();
                display->ShowNotification(Lang::Strings::WIFI_CONNECTED, 3000);
                application.PlaySound(Lang::Sounds::OGG_WIFI_CONNECTED);
                return;
            }
        }

        HideNoNetworkPrompt();
        ESP_LOGW(TAG, "Foreground retries exhausted, keep waiting in background");

        // WifiStation keeps its own scan/reconnect loop alive after Start().
        while (!wifi_station.WaitForConnected(30 * 1000)) {
        }

        display->ShowNotification(Lang::Strings::WIFI_CONNECTED, 3000);
        application.PlaySound(Lang::Sounds::OGG_WIFI_CONNECTED);
        return;
    }
```

- [ ] **Step 2: Run the target build to verify it fails for the expected reason**

Run:

```powershell
idf.py build
```

Expected: FAIL with compiler errors that mention `ShowNoNetworkPrompt` and `HideNoNetworkPrompt` are not declared members of `WifiBoard`.

- [ ] **Step 3: Declare the new board hook methods in `wifi_board.h`**

Update the protected section of `main/boards/common/wifi_board.h` to include:

```cpp
protected:
    bool wifi_config_mode_ = false;
    ProvisioningMode provisioning_mode_ = ProvisioningMode::WifiAp;
    void EnterProvisioningMode();
    void EnterWifiApProvisioningMode();
    virtual void ShowNoNetworkPrompt();
    virtual void HideNoNetworkPrompt();
    virtual std::string GetBoardJson() override;
```

- [ ] **Step 4: Implement the no-op hooks and keep the retry loop inside `StartNetwork()`**

Add these default hook implementations to `main/boards/common/wifi_board.cc`:

```cpp
void WifiBoard::ShowNoNetworkPrompt() {
}

void WifiBoard::HideNoNetworkPrompt() {
}
```

Keep the rest of `StartNetwork()` unchanged except for the timeout branch from Step 1. In particular:

```cpp
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
```

Do not reintroduce any of the removed timeout fallback lines:

```cpp
        wifi_station.Stop();
        wifi_config_mode_ = true;
        EnterProvisioningMode();
```

- [ ] **Step 5: Run the target build to verify the retry flow compiles**

Run:

```powershell
idf.py build
```

Expected: PASS for the current AIToy target.

- [ ] **Step 6: Commit the `WifiBoard` retry-flow change**

Run:

```powershell
git add main/boards/common/wifi_board.h main/boards/common/wifi_board.cc
git commit -m "feat: add no-network retry flow to WifiBoard"
```

Expected: the commit changes only the common Wi-Fi startup logic and the new no-op hook declarations.

### Task 3: Add Pause and Resume Control to `EyeAnimator`

**Files:**
- Modify: `main/boards/AIToy-s3-dualeye-lcd-0.71/eye_animator.h`
- Modify: `main/boards/AIToy-s3-dualeye-lcd-0.71/eye_animator.cc`

- [ ] **Step 1: Write the failing build trigger by teaching the render loop to read a pause flag before the flag exists**

In `main/boards/AIToy-s3-dualeye-lcd-0.71/eye_animator.cc`, change the state read inside `EyeAnimator::Run()` to:

```cpp
        std::string emotion;
        EyeAnimState state;
        bool paused = false;
        {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            emotion = target_emotion_;
            state = target_state_;
            paused = paused_;
            xSemaphoreGive(mutex_);
        }

        if (paused) {
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }
```

- [ ] **Step 2: Run the target build to verify it fails**

Run:

```powershell
idf.py build
```

Expected: FAIL with a compiler error that `paused_` is not a member of `EyeAnimator`.

- [ ] **Step 3: Add the public pause/resume API and backing state**

Update `main/boards/AIToy-s3-dualeye-lcd-0.71/eye_animator.h`:

```cpp
public:
    static EyeAnimator& GetInstance();

    void Init(EyeDisplay* left, EyeDisplay* right);
    void Start();
    void Pause();
    void Resume();
    void SetEmotion(const std::string& emotion);
    void SetState(EyeAnimState state);
```

Add the backing flag in the private section:

```cpp
    bool paused_ = false;
```

- [ ] **Step 4: Implement the new methods in `eye_animator.cc`**

Insert:

```cpp
void EyeAnimator::Pause() {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    paused_ = true;
    xSemaphoreGive(mutex_);
}

void EyeAnimator::Resume() {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    paused_ = false;
    xSemaphoreGive(mutex_);
}
```

Keep the Step 1 `Run()` change exactly as written so the task simply skips rendering while paused.

- [ ] **Step 5: Run the target build to verify the pause/resume API compiles**

Run:

```powershell
idf.py build
```

Expected: PASS.

- [ ] **Step 6: Commit the animator pause/resume support**

Run:

```powershell
git add main/boards/AIToy-s3-dualeye-lcd-0.71/eye_animator.h main/boards/AIToy-s3-dualeye-lcd-0.71/eye_animator.cc
git commit -m "feat: add pause control to AIToy eye animator"
```

Expected: the commit contains only the new animator API and the `Run()` pause gate.

### Task 4: Add a Full-Screen Wi-Fi-Off Overlay API to `EyeDisplay`

**Files:**
- Modify: `main/boards/AIToy-s3-dualeye-lcd-0.71/eye_display.h`
- Modify: `main/boards/AIToy-s3-dualeye-lcd-0.71/eye_display.cc`

- [ ] **Step 1: Write the failing build trigger by adding the overlay method bodies before declaring their interface**

Insert these two methods into `main/boards/AIToy-s3-dualeye-lcd-0.71/eye_display.cc` immediately above `bool EyeDisplay::Lock(int timeout_ms)`:

```cpp
void EyeDisplay::ShowFullScreenIcon(const char* icon_text) {
    if (!icon_text) return;
    if (!lvgl_port_lock(200)) return;

    auto* screen = lv_display_get_screen_active(display_);
    SetObjectHidden(sclera_, true);
    SetObjectHidden(iris_, true);
    SetObjectHidden(pupil_, true);
    SetObjectHidden(highlight_, true);
    SetObjectHidden(eyelid_top_, true);
    SetObjectHidden(eyelid_bottom_, true);
    SetObjectHidden(eyebrow_, true);
    SetObjectHidden(decor_, true);
    SetObjectHidden(decor2_, true);
    SetObjectHidden(gif_obj_, true);

    if (!fullscreen_icon_) {
        fullscreen_icon_ = lv_label_create(screen);
        lv_obj_set_style_bg_opa(fullscreen_icon_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(fullscreen_icon_, lv_color_white(), 0);
        lv_obj_set_style_text_font(fullscreen_icon_, &font_awesome_30_4, 0);
    }

    lv_label_set_text(fullscreen_icon_, icon_text);
    lv_obj_update_layout(fullscreen_icon_);
    lv_obj_set_style_transform_zoom(fullscreen_icon_, 768, 0);
    lv_obj_set_style_transform_pivot_x(fullscreen_icon_, lv_obj_get_width(fullscreen_icon_) / 2, 0);
    lv_obj_set_style_transform_pivot_y(fullscreen_icon_, lv_obj_get_height(fullscreen_icon_) / 2, 0);
    lv_obj_center(fullscreen_icon_);
    lv_obj_move_foreground(fullscreen_icon_);
    lv_obj_remove_flag(fullscreen_icon_, LV_OBJ_FLAG_HIDDEN);

    lvgl_port_unlock();
}

void EyeDisplay::HideFullScreenIcon() {
    if (!lvgl_port_lock(200)) return;

    if (fullscreen_icon_) {
        lv_obj_delete(fullscreen_icon_);
        fullscreen_icon_ = nullptr;
    }

    SetObjectHidden(sclera_, false);
    SetObjectHidden(iris_, false);
    SetObjectHidden(pupil_, false);
    SetObjectHidden(highlight_, false);
    SetObjectHidden(eyelid_top_, false);
    SetObjectHidden(eyelid_bottom_, false);
    SetObjectHidden(eyebrow_, false);
    SetObjectHidden(decor_, false);
    SetObjectHidden(decor2_, false);
    SetObjectHidden(gif_obj_, false);

    lvgl_port_unlock();
}
```

- [ ] **Step 2: Run the target build to verify it fails**

Run:

```powershell
idf.py build
```

Expected: FAIL with compiler errors that mention missing declarations for `ShowFullScreenIcon`, `HideFullScreenIcon`, `SetObjectHidden`, `fullscreen_icon_`, or `font_awesome_30_4`.

- [ ] **Step 3: Declare the overlay API and backing state in `eye_display.h`**

Update `main/boards/AIToy-s3-dualeye-lcd-0.71/eye_display.h`:

```cpp
public:
    EyeDisplay(esp_lcd_panel_io_handle_t panel_io,
               esp_lcd_panel_handle_t panel,
               int width, int height,
               int offset_x, int offset_y,
               bool mirror_x, bool mirror_y, bool swap_xy,
               EyeSide side,
               bool gif_mode = false);

    void ShowFullScreenIcon(const char* icon_text);
    void HideFullScreenIcon();

    void SetEmotion(const char* emotion) override;
```

Add the new private members and helper:

```cpp
    lv_obj_t* gif_obj_ = nullptr;
    lv_obj_t* fullscreen_icon_ = nullptr;
    EyeDisplay* pair_ = nullptr;

    void CreateEyeObjects();
    void CreateGifObjects();
    void SetObjectHidden(lv_obj_t* obj, bool hidden);
```

- [ ] **Step 4: Add the FontAwesome declaration and helper implementation**

Near the top of `main/boards/AIToy-s3-dualeye-lcd-0.71/eye_display.cc`, after the existing includes, add:

```cpp
LV_FONT_DECLARE(font_awesome_30_4);
```

Then add the helper implementation immediately above `bool EyeDisplay::Lock(int timeout_ms)`:

```cpp
void EyeDisplay::SetObjectHidden(lv_obj_t* obj, bool hidden) {
    if (!obj) return;

    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}
```

Do not simplify away the internal `lvgl_port_lock()` / `lvgl_port_unlock()` calls in the overlay methods; they are the LVGL thread-safety boundary for this task.

- [ ] **Step 5: Run the target build to verify the overlay API compiles**

Run:

```powershell
idf.py build
```

Expected: PASS.

- [ ] **Step 6: Commit the full-screen overlay implementation**

Run:

```powershell
git add main/boards/AIToy-s3-dualeye-lcd-0.71/eye_display.h main/boards/AIToy-s3-dualeye-lcd-0.71/eye_display.cc
git commit -m "feat: add full-screen no-network overlay to AIToy eyes"
```

Expected: the commit contains only the eye display overlay API plus its LVGL-safe implementation.

### Task 5: Override the No-Network Prompt Hooks in the AIToy Board

**Files:**
- Modify: `main/boards/AIToy-s3-dualeye-lcd-0.71/esp32-s3-dualeye-lcd-0.71.cc`

- [ ] **Step 1: Write the failing build trigger by overriding the hooks before adding the FontAwesome include**

Add these overrides inside `class CustomBoard`, after the existing BLE helper block and before `public:`:

```cpp
protected:
    void ShowNoNetworkPrompt() override {
        if (!gif_mode_) {
            EyeAnimator::GetInstance().Pause();
        }

        if (left_eye_) {
            left_eye_->ShowFullScreenIcon(FONT_AWESOME_WIFI_OFF);
        }
        if (right_eye_) {
            right_eye_->ShowFullScreenIcon(FONT_AWESOME_WIFI_OFF);
        }
    }

    void HideNoNetworkPrompt() override {
        if (left_eye_) {
            left_eye_->HideFullScreenIcon();
        }
        if (right_eye_) {
            right_eye_->HideFullScreenIcon();
        }

        if (!gif_mode_) {
            EyeAnimator::GetInstance().Resume();
        }
    }
```

- [ ] **Step 2: Run the target build to verify it fails**

Run:

```powershell
idf.py build
```

Expected: FAIL with a compiler error that `FONT_AWESOME_WIFI_OFF` is not declared in this translation unit.

- [ ] **Step 3: Add the missing include and keep the overrides board-local**

At the top of `main/boards/AIToy-s3-dualeye-lcd-0.71/esp32-s3-dualeye-lcd-0.71.cc`, add:

```cpp
#include "font_awesome_symbols.h"
```

Do not change `main/boards/common/wifi_board.cc` to include `display_manager.h` or any other AIToy-only header. The whole point of this task is to keep the common Wi-Fi logic board-agnostic and do the dual-eye work here, where `left_eye_`, `right_eye_`, and `gif_mode_` already exist.

- [ ] **Step 4: Run the target build to verify the AIToy hook overrides compile**

Run:

```powershell
idf.py build
```

Expected: PASS.

- [ ] **Step 5: Commit the AIToy board integration**

Run:

```powershell
git add main/boards/AIToy-s3-dualeye-lcd-0.71/esp32-s3-dualeye-lcd-0.71.cc
git commit -m "feat: show no-network prompt on AIToy dual-eye board"
```

Expected: the commit contains only the board-local hook override and the new FontAwesome include.

### Task 6: Verify the End-to-End Startup Flow and the Protected Regressions

**Files:**
- Verify: `main/boards/common/wifi_board.cc`
- Verify: `main/boards/AIToy-s3-dualeye-lcd-0.71/esp32-s3-dualeye-lcd-0.71.cc`
- Verify: `main/boards/AIToy-s3-dualeye-lcd-0.71/eye_display.cc`
- Verify: `main/boards/AIToy-s3-dualeye-lcd-0.71/eye_animator.cc`

- [ ] **Step 1: Rebuild the current AIToy target from scratch**

Run:

```powershell
idf.py build
```

Expected: PASS with no compile or link errors.

- [ ] **Step 2: Run the initial-timeout hardware check with a saved but unavailable SSID**

Prepare a known SSID/password on the device, power that AP off, then run:

```powershell
idf.py flash monitor
```

Expected hardware behavior:

- The device stays in `kDeviceStateStarting` during the blocked `StartNetwork()` path.
- About 60 seconds after boot, both circular LCDs switch to a centered Wi-Fi-off icon.
- The spoken `no_network.ogg` clip plays once.
- The prompt remains visible for three foreground retry windows of 30 seconds each.

- [ ] **Step 3: Verify recovery during the foreground retry phase**

While the prompt from Step 2 is still visible, power the saved AP back on.

Expected hardware behavior:

- The Wi-Fi-off icon disappears from both eyes as soon as the station connects.
- If the board is using LVGL eyes, the normal eye animation resumes immediately after the icon is removed.
- The spoken `wifi_connected.ogg` clip plays once.
- Boot continues into the post-network startup path, including the OTA check and protocol initialization logs.

- [ ] **Step 4: Verify the background wait path after all three foreground retries fail**

Repeat Step 2, but keep the AP unavailable for at least 150 seconds total so the initial 60-second wait plus the three 30-second foreground retries all expire before recovery.

Expected hardware behavior:

- After the third 30-second retry window expires, the Wi-Fi-off icon is removed and the eyes resume their normal animation.
- The process still remains blocked inside `StartNetwork()` and does not enter provisioning mode.
- When the AP finally comes back, the spoken `wifi_connected.ogg` clip plays and boot continues normally.

- [ ] **Step 5: Verify the protected immediate-provisioning paths still work**

Clear the saved Wi-Fi credentials, then reboot the device.

Expected hardware behavior:

- If the SSID list is empty, the device enters provisioning mode immediately.
- The new no-network icon flow does not run in this case.
- The existing `force_ap` / provisioning logic still owns the empty-credentials path.

- [ ] **Step 6: Verify the BOOT-button regressions called out in the spec**

During the blocked `kDeviceStateStarting` period with Wi-Fi still disconnected:

- Short-press BOOT once.
- Long-press BOOT for 5 seconds.

Expected hardware behavior:

- The short-press reset-to-provisioning behavior still works.
- The long-press clear-Wi-Fi-and-restart behavior still works.
- Neither path gets stuck behind the new no-network overlay.

- [ ] **Step 7: Finish with a workspace sanity check**

Run:

```powershell
git status --short
```

Expected: the worktree is clean if the task-by-task commit steps were followed. If you intentionally postponed commits, only the files named in this plan should be modified.
