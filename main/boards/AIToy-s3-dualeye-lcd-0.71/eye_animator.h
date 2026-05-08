#pragma once

#include "eye_display.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <string>

enum class EyeAnimState {
    IDLE,
    LISTENING,
    SPEAKING,
    CONNECTING,
};

class EyeAnimator {
public:
    static EyeAnimator& GetInstance();

    void Init(EyeDisplay* left, EyeDisplay* right);
    void Start();
    void Pause();
    void Resume();
    void SetEmotion(const std::string& emotion);
    void SetState(EyeAnimState state);

private:
    EyeAnimator();
    ~EyeAnimator() = default;

    EyeDisplay* left_ = nullptr;
    EyeDisplay* right_ = nullptr;

    SemaphoreHandle_t mutex_;
    TaskHandle_t task_ = nullptr;

    std::string current_emotion_ = "neutral";
    std::string target_emotion_ = "neutral";
    EyeAnimState current_state_ = EyeAnimState::IDLE;
    EyeAnimState target_state_ = EyeAnimState::IDLE;
    bool paused_ = false;

    struct EmotionParams {
        float eyelid_top = 0;
        float eyelid_bottom = 0;
        float pupil_scale = 1.0f;
        float gaze_x = 0, gaze_y = 0;
        lv_color_t iris_color;
        int blink_min_ms = 2500;
        int blink_max_ms = 5500;
        bool wink_left = false;
        float eyebrow_angle = 0;
        float eyebrow_y = 0;
        bool eyebrow_visible = false;
        float sclera_h_scale = 1.0f;
        EyeDecor decoration = EyeDecor::NONE;
    };

    float cur_eyelid_top_ = 0;
    float cur_eyelid_bottom_ = 0;
    float cur_gaze_x_ = 0, cur_gaze_y_ = 0;
    float cur_pupil_scale_ = 1.0f;
    float cur_eyebrow_angle_ = 0;
    float cur_eyebrow_y_ = 0;
    float cur_sclera_h_ = 1.0f;

    int64_t last_blink_time_ = 0;
    int next_blink_interval_ = 3000;
    int64_t last_saccade_time_ = 0;
    int next_saccade_interval_ = 2000;
    float saccade_dx_ = 0, saccade_dy_ = 0;
    bool is_blinking_ = false;

    static void TaskEntry(void* arg);
    void Run();
    EmotionParams GetEmotionParams(const std::string& emotion);
    void ApplyToDisplay(EyeDisplay* eye, const EmotionParams& ep,
                        float eyelid_top, float eyelid_bottom,
                        float gaze_x, float gaze_y, float pupil_scale,
                        float eyebrow_angle, float eyebrow_y,
                        float sclera_h, float decor_bob);

    static constexpr int EYELID_TOP_OPEN_Y = -170;
    static constexpr int EYELID_TOP_CLOSED_Y = 30;
    static constexpr int EYELID_BOTTOM_OPEN_Y = 250;
    static constexpr int EYELID_BOTTOM_CLOSED_Y = 60;
    static constexpr int MAX_GAZE_OFFSET = 35;
    static constexpr int BASE_PUPIL_SIZE = 44;
    static constexpr int BASE_SCLERA_H = 180;
    static constexpr int EYEBROW_BASE_Y = -75;
};
