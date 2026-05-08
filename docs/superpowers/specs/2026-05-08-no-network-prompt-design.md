# WiFi 连接失败无网络提示设计

## 概述

当 ESP32-S3 双眼 LCD 板（AIToy-s3-dualeye-lcd-0.71）WiFi 连接超时失败时，在双圆形 LCD 屏上显示全屏"无网络"图标，并播放语音提示。支持自动重试和后台重连机制。

## 触发条件

仅在 `WifiBoard::StartNetwork()` 中 WiFi 连接 60 秒超时后触发。不包括运行中断网场景。

## 完整流程

```
StartNetwork() 调用
    │
    ▼
WiFi 连接尝试（60s 超时）
    │
    ├── 成功 → 正常返回，继续启动流程
    │
    └── 失败
         │
         ▼
    ┌─────────────────────────────────┐
    │ 1. 双屏显示全屏 WiFi-Off 图标    │
    │ 2. 播放 "无网络" OGG 语音        │
    └─────────────────────────────────┘
         │
         ▼
    重试 WiFi 连接（最多 3 次，间隔 30s）
    期间保持无网络图标显示
         │
         ├── 某次重试成功 → 隐藏图标 → 播放 "网络已连接" 语音 → 返回
         │
         └── 3 次都失败
              │
              ▼
         ┌──────────────────────────────────┐
         │ 恢复眼睛动画                       │
         │ 进入后台重连循环（每 30s 尝试一次） │
         └──────────────────────────────────┘
              │
              └── 某次重连成功
                   │
                   ▼
              播放 "网络已连接" 语音
              StartNetwork() 返回
              正常继续启动流程（OTA、协议初始化等）
```

## 阻塞模型

`StartNetwork()` 保持阻塞式设计，与现有架构一致。在 3 次重试失败后虽然恢复了眼睛动画，但函数仍在内部循环等待 WiFi 连接成功。从用户视角看设备眼睛正常运行，实际 `StartNetwork()` 尚未返回。WiFi 连上后函数返回，`Application::Start()` 中后续流程（OTA 检查、协议初始化）自动继续。

## 修改文件清单

### 1. `main/boards/AIToy-s3-dualeye-lcd-0.71/eye_display.h`

新增两个公开方法：

- `void ShowFullScreenIcon(const char* icon_text)` — 隐藏所有眼球 LVGL 对象（巩膜、虹膜、瞳孔、高光、眼皮、眉毛、装饰），在屏幕中心绘制一个大号 WiFi-Off 图标。GIF 模式下隐藏 gif_obj_。
- `void HideFullScreenIcon()` — 删除图标对象，恢复所有眼球对象的可见性。

新增私有成员：

- `lv_obj_t* fullscreen_icon_ = nullptr` — 全屏图标 LVGL 对象指针

### 2. `main/boards/AIToy-s3-dualeye-lcd-0.71/eye_display.cc`

实现 `ShowFullScreenIcon()` 和 `HideFullScreenIcon()`：

**ShowFullScreenIcon 逻辑：**
1. 获取当前 display 的 active screen
2. 隐藏所有眼球子对象（sclera_, iris_, pupil_, highlight_, eyelid_top_, eyelid_bottom_, eyebrow_, decor_, decor2_）或 gif_obj_，使用 `lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN)`
3. 创建一个 lv_label 对象，设置 FontAwesome 字体，显示 WiFi-Off 图标字符
4. 设置字体大小足够大（使用项目中可用的最大 FontAwesome 字体），白色图标居中显示在黑色背景上
5. 保存到 `fullscreen_icon_` 成员

**HideFullScreenIcon 逻辑：**
1. 删除 `fullscreen_icon_` 对象（`lv_obj_delete()`）
2. 将 `fullscreen_icon_` 置 nullptr
3. 恢复所有眼球子对象可见性（`lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN)`）

### 3. `main/boards/common/wifi_board.h`

无需修改公开接口。在 `StartNetwork()` 内部增加逻辑即可。

### 4. `main/boards/common/wifi_board.cc`

修改 `StartNetwork()` 方法，将原来的超时→进入配网模式逻辑替换为：

```
原逻辑：
  WiFi 超时 → wifi_station.Stop() → 进入配网

新逻辑：
  WiFi 超时 → 显示无网络图标 + 语音 → 重试 3 次 → 恢复眼睛 → 后台重连循环
```

具体修改点在 `if (!wifi_station.WaitForConnected(60 * 1000))` 分支内：

1. 通过 `DisplayManager::GetAllDisplays()` 获取所有 EyeDisplay 实例
2. 对每个 display 调用 `ShowFullScreenIcon(FONT_AWESOME_WIFI_OFF)`
3. 调用 `Application::GetInstance().PlaySound(Lang::Sounds::OGG_NO_NETWORK)`
4. 循环重试 3 次：每次 `wifi_station.WaitForConnected(30 * 1000)`
   - 成功则 `HideFullScreenIcon()` + 播放已连接语音 + return
5. 3 次失败后：`HideFullScreenIcon()` 恢复眼睛
6. 进入 `while(true)` 循环：每 30 秒 `wifi_station.WaitForConnected(30 * 1000)`
   - 成功则播放已连接语音 + break

### 5. `main/assets/lang_config.h`

新增语音资源引用：

- `OGG_NO_NETWORK` — "无网络"语音提示（用户提供 `no_network.ogg`）
- `OGG_WIFI_CONNECTED` — "网络已连接"语音提示（用户提供 `wifi_connected.ogg`）

新增字符串常量：

- `NO_NETWORK` — "无网络连接"
- `WIFI_CONNECTED` — "网络已连接"

### 6. 音频文件

用户需提供并放置到对应语言的音频资源目录：

- `no_network.ogg` — 无网络提示语音
- `wifi_connected.ogg` — 网络已连接提示语音

## 不变的部分

- BOOT 键长按 5 秒重置 WiFi 并重启 — 不受影响
- BOOT 键短按在 Starting 状态下重置 WiFi — 不受影响
- BLE 配网流程 — 不受影响
- 已有 SSID 列表为空时直接进入配网模式 — 不受影响（在重试逻辑之前判断）
- `force_ap` 强制配网模式 — 不受影响

## EyeAnimator 协调

3 次重试期间需暂停 `EyeAnimator` 动画任务，避免它与全屏图标产生冲突。方法：在 `EyeAnimator` 中新增一个 `pause_/resume_` 标志，当 `pause_` 为 true 时动画循环跳过渲染。

- `EyeAnimator::Pause()` — 设置 pause_ = true
- `EyeAnimator::Resume()` — 设置 pause_ = false

## LVGL 线程安全

所有 LVGL 操作必须在 `lvgl_port_lock()` / `lvgl_port_unlock()` 之间执行。`ShowFullScreenIcon()` 和 `HideFullScreenIcon()` 内部加锁。

## 字体选择

全屏图标使用项目中已有的 FontAwesome 字体。需要确认可用的最大字号。如果现有字体尺寸不够大，可以使用较小字号配合居中显示，仍然能达到辨识效果。圆形屏 240x240，图标尺寸建议 80-120px。

## 错误处理

- WiFi station 的 Start/Stop/WaitForConnected 均可重复调用，无需特殊保护
- 如果音频播放失败（OGG 文件缺失），不影响主流程，仅跳过语音
- 如果 LVGL lock 超时，跳过本次图标显示/隐藏操作
