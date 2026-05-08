#include "eye_animator.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_random.h>
#include <algorithm>
#include <cmath>

static const char* TAG = "EyeAnimator";

static int random_range(int min_val, int max_val) {
    if (min_val >= max_val) return min_val;
    return min_val + (esp_random() % (max_val - min_val + 1));
}

static float lerpf(float cur, float target, float factor) {
    return cur + (target - cur) * factor;
}

EyeAnimator& EyeAnimator::GetInstance() {
    static EyeAnimator instance;
    return instance;
}

EyeAnimator::EyeAnimator() {
    mutex_ = xSemaphoreCreateMutex();
}

void EyeAnimator::Init(EyeDisplay* left, EyeDisplay* right) {
    left_ = left;
    right_ = right;
}

void EyeAnimator::Start() {
    xTaskCreate(TaskEntry, "eye_anim", 5120, this, 2, &task_);
}

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

void EyeAnimator::SetEmotion(const std::string& emotion) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    target_emotion_ = emotion;
    xSemaphoreGive(mutex_);
}

void EyeAnimator::SetState(EyeAnimState state) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    target_state_ = state;
    xSemaphoreGive(mutex_);
}

void EyeAnimator::TaskEntry(void* arg) {
    static_cast<EyeAnimator*>(arg)->Run();
}

EyeAnimator::EmotionParams EyeAnimator::GetEmotionParams(const std::string& emotion) {
    EmotionParams p;
    p.iris_color = lv_color_hex(0x4488CC);

    if (emotion == "happy" || emotion == "funny") {
        p.eyelid_bottom = 0.40f;
        p.pupil_scale = 1.20f;
        p.iris_color = lv_color_hex(0x55BBEE);
        p.sclera_h_scale = 0.85f;
        p.eyebrow_visible = true;
        p.eyebrow_angle = 8.0f;
        p.eyebrow_y = -5.0f;
        p.decoration = EyeDecor::SPARKLE;
    } else if (emotion == "laughing") {
        p.eyelid_bottom = 0.55f;
        p.eyelid_top = 0.10f;
        p.pupil_scale = 1.25f;
        p.iris_color = lv_color_hex(0x55CCFF);
        p.sclera_h_scale = 0.70f;
        p.eyebrow_visible = true;
        p.eyebrow_angle = 14.0f;
        p.eyebrow_y = -2.0f;
        p.decoration = EyeDecor::SPARKLE;
    } else if (emotion == "sad") {
        p.eyelid_top = 0.28f;
        p.gaze_y = 0.40f;
        p.pupil_scale = 0.70f;
        p.iris_color = lv_color_hex(0x3355AA);
        p.sclera_h_scale = 0.88f;
        p.blink_min_ms = 2000;
        p.blink_max_ms = 4000;
        p.eyebrow_visible = true;
        p.eyebrow_angle = -18.0f;
        p.eyebrow_y = 5.0f;
        p.decoration = EyeDecor::TEAR;
    } else if (emotion == "crying") {
        p.eyelid_top = 0.35f;
        p.gaze_y = 0.45f;
        p.pupil_scale = 0.65f;
        p.iris_color = lv_color_hex(0x334488);
        p.sclera_h_scale = 0.80f;
        p.blink_min_ms = 1500;
        p.blink_max_ms = 3000;
        p.eyebrow_visible = true;
        p.eyebrow_angle = -22.0f;
        p.eyebrow_y = 8.0f;
        p.decoration = EyeDecor::TEAR;
    } else if (emotion == "angry") {
        p.eyelid_top = 0.38f;
        p.pupil_scale = 0.55f;
        p.iris_color = lv_color_hex(0xDD3333);
        p.sclera_h_scale = 0.78f;
        p.blink_min_ms = 5000;
        p.blink_max_ms = 8000;
        p.eyebrow_visible = true;
        p.eyebrow_angle = 22.0f;
        p.eyebrow_y = 8.0f;
        p.decoration = EyeDecor::ANGER;
    } else if (emotion == "surprised" || emotion == "shocked") {
        float s = (emotion == "shocked") ? 1.25f : 1.20f;
        p.pupil_scale = (emotion == "shocked") ? 1.70f : 1.55f;
        p.sclera_h_scale = s;
        p.blink_min_ms = 5000;
        p.blink_max_ms = 9000;
        p.eyebrow_visible = true;
        p.eyebrow_angle = 0.0f;
        p.eyebrow_y = -12.0f;
    } else if (emotion == "thinking") {
        p.eyelid_top = 0.08f;
        p.eyelid_bottom = 0.05f;
        p.gaze_x = 0.45f;
        p.gaze_y = -0.40f;
        p.sclera_h_scale = 0.92f;
        p.eyebrow_visible = true;
        p.eyebrow_angle = -8.0f;
        p.eyebrow_y = -3.0f;
    } else if (emotion == "sleepy") {
        p.eyelid_top = 0.55f;
        p.eyelid_bottom = 0.15f;
        p.gaze_y = 0.20f;
        p.pupil_scale = 0.80f;
        p.sclera_h_scale = 0.60f;
        p.blink_min_ms = 1200;
        p.blink_max_ms = 2500;
        p.decoration = EyeDecor::ZZZ;
    } else if (emotion == "loving" || emotion == "kissy") {
        p.eyelid_bottom = 0.25f;
        p.pupil_scale = 1.35f;
        p.iris_color = lv_color_hex(0xFF6699);
        p.sclera_h_scale = 0.90f;
        p.eyebrow_visible = true;
        p.eyebrow_angle = 6.0f;
        p.eyebrow_y = -4.0f;
        p.decoration = EyeDecor::HEART;
    } else if (emotion == "confused") {
        p.eyelid_top = 0.15f;
        p.gaze_x = -0.35f;
        p.gaze_y = -0.20f;
        p.sclera_h_scale = 0.95f;
        p.eyebrow_visible = true;
        p.eyebrow_angle = -12.0f;
        p.eyebrow_y = 2.0f;
        p.decoration = EyeDecor::SWEAT;
    } else if (emotion == "winking") {
        p.wink_left = true;
        p.eyelid_top = 0.95f;
        p.eyelid_bottom = 0.20f;
        p.sclera_h_scale = 0.88f;
        p.eyebrow_visible = true;
        p.eyebrow_angle = 10.0f;
        p.eyebrow_y = -5.0f;
        p.decoration = EyeDecor::SPARKLE;
    } else if (emotion == "embarrassed") {
        p.eyelid_top = 0.18f;
        p.eyelid_bottom = 0.22f;
        p.gaze_x = 0.40f;
        p.gaze_y = 0.20f;
        p.pupil_scale = 0.80f;
        p.iris_color = lv_color_hex(0xDD7799);
        p.sclera_h_scale = 0.85f;
        p.eyebrow_visible = true;
        p.eyebrow_angle = -10.0f;
        p.eyebrow_y = 3.0f;
        p.decoration = EyeDecor::SWEAT;
    } else if (emotion == "cool" || emotion == "confident") {
        p.eyelid_top = 0.25f;
        p.pupil_scale = 0.85f;
        p.sclera_h_scale = 0.80f;
        p.blink_min_ms = 4000;
        p.blink_max_ms = 7000;
    } else if (emotion == "silly") {
        p.pupil_scale = 1.25f;
        p.gaze_x = 0.20f;
        p.gaze_y = 0.15f;
        p.eyelid_bottom = 0.15f;
        p.sclera_h_scale = 0.95f;
        p.eyebrow_visible = true;
        p.eyebrow_angle = 5.0f;
        p.eyebrow_y = -2.0f;
        p.decoration = EyeDecor::SPARKLE;
    } else if (emotion == "delicious") {
        p.eyelid_bottom = 0.38f;
        p.pupil_scale = 1.20f;
        p.iris_color = lv_color_hex(0x55BBEE);
        p.sclera_h_scale = 0.82f;
        p.eyebrow_visible = true;
        p.eyebrow_angle = 10.0f;
        p.eyebrow_y = -3.0f;
    } else if (emotion == "relaxed") {
        p.eyelid_top = 0.20f;
        p.eyelid_bottom = 0.08f;
        p.sclera_h_scale = 0.88f;
        p.blink_min_ms = 2000;
        p.blink_max_ms = 3500;
    } else if (emotion == "fearful") {
        p.pupil_scale = 1.50f;
        p.sclera_h_scale = 1.15f;
        p.gaze_x = -0.25f;
        p.iris_color = lv_color_hex(0x6699CC);
        p.blink_min_ms = 1800;
        p.blink_max_ms = 3000;
        p.eyebrow_visible = true;
        p.eyebrow_angle = -15.0f;
        p.eyebrow_y = -8.0f;
        p.decoration = EyeDecor::SWEAT;
    } else if (emotion == "disgusted") {
        p.eyelid_top = 0.20f;
        p.eyelid_bottom = 0.30f;
        p.pupil_scale = 0.65f;
        p.gaze_x = 0.30f;
        p.iris_color = lv_color_hex(0x889944);
        p.sclera_h_scale = 0.75f;
        p.eyebrow_visible = true;
        p.eyebrow_angle = -15.0f;
        p.eyebrow_y = 6.0f;
    }

    return p;
}

// Decoration layout: returns position/size/color for decor1 and decor2
struct DecorLayout {
    int x1, y1, w1, h1;
    int x2, y2, w2, h2;
    lv_color_t c1, c2;
    bool show1, show2;
};

static DecorLayout GetDecorLayout(EyeDecor type) {
    DecorLayout d = {};
    d.show1 = false;
    d.show2 = false;
    d.c1 = lv_color_white();
    d.c2 = lv_color_white();

    switch (type) {
    case EyeDecor::HEART:
        d.show1 = true; d.show2 = true;
        d.x1 = 58; d.y1 = -58; d.w1 = 22; d.h1 = 22;
        d.x2 = 66; d.y2 = -50; d.w2 = 16; d.h2 = 16;
        d.c1 = lv_color_hex(0xFF3366);
        d.c2 = lv_color_hex(0xFF5588);
        break;
    case EyeDecor::TEAR:
        d.show1 = true; d.show2 = true;
        d.x1 = -8; d.y1 = 82; d.w1 = 10; d.h1 = 20;
        d.x2 = -8; d.y2 = 97; d.w2 = 6; d.h2 = 6;
        d.c1 = lv_color_hex(0x5599FF);
        d.c2 = lv_color_hex(0x77BBFF);
        break;
    case EyeDecor::ANGER:
        d.show1 = true; d.show2 = true;
        d.x1 = 62; d.y1 = -62; d.w1 = 16; d.h1 = 16;
        d.x2 = 72; d.y2 = -54; d.w2 = 12; d.h2 = 12;
        d.c1 = lv_color_hex(0xFF2222);
        d.c2 = lv_color_hex(0xFF4444);
        break;
    case EyeDecor::SWEAT:
        d.show1 = true; d.show2 = false;
        d.x1 = 68; d.y1 = -48; d.w1 = 14; d.h1 = 18;
        d.c1 = lv_color_hex(0x66AAFF);
        break;
    case EyeDecor::ZZZ:
        d.show1 = true; d.show2 = true;
        d.x1 = 55; d.y1 = -58; d.w1 = 16; d.h1 = 16;
        d.x2 = 68; d.y2 = -72; d.w2 = 12; d.h2 = 12;
        d.c1 = lv_color_hex(0xCCDDFF);
        d.c2 = lv_color_hex(0xAABBEE);
        break;
    case EyeDecor::SPARKLE:
        d.show1 = true; d.show2 = true;
        d.x1 = 65; d.y1 = -55; d.w1 = 10; d.h1 = 10;
        d.x2 = 52; d.y2 = -70; d.w2 = 8; d.h2 = 8;
        d.c1 = lv_color_hex(0xFFDD44);
        d.c2 = lv_color_hex(0xFFEE88);
        break;
    default:
        break;
    }
    return d;
}

void EyeAnimator::ApplyToDisplay(EyeDisplay* eye, const EmotionParams& ep,
                                  float eyelid_top, float eyelid_bottom,
                                  float gaze_x, float gaze_y, float pupil_scale,
                                  float eyebrow_angle, float eyebrow_y,
                                  float sclera_h, float decor_bob) {
    bool is_right = (eye->GetSide() == EyeSide::RIGHT);
    float flip = is_right ? -1.0f : 1.0f;

    if (is_right) {
        gaze_x = -gaze_x;
        gaze_y = -gaze_y;
        std::swap(eyelid_top, eyelid_bottom);
    }

    int top_y = EYELID_TOP_OPEN_Y + (int)((EYELID_TOP_CLOSED_Y - EYELID_TOP_OPEN_Y) * eyelid_top);
    int bottom_y = EYELID_BOTTOM_OPEN_Y + (int)((EYELID_BOTTOM_CLOSED_Y - EYELID_BOTTOM_OPEN_Y) * eyelid_bottom);

    eye->SetEyelidTopY(top_y);
    eye->SetEyelidBottomY(bottom_y);
    eye->SetIrisOffset((int)(gaze_x * MAX_GAZE_OFFSET), (int)(gaze_y * MAX_GAZE_OFFSET));
    eye->SetPupilDiameter((int)(BASE_PUPIL_SIZE * pupil_scale));
    eye->SetIrisColor(ep.iris_color);

    int sclera_h_px = (int)(sclera_h * BASE_SCLERA_H);
    sclera_h_px = std::clamp(sclera_h_px, 90, 220);
    eye->SetScleraSize(180, sclera_h_px);

    int eb_angle_01 = (int)(eyebrow_angle * flip * 10.0f);
    int eb_y = EYEBROW_BASE_Y + (int)(eyebrow_y);
    if (is_right) eb_y = -eb_y;
    eye->SetEyebrow(eb_angle_01, eb_y, ep.eyebrow_visible);

    DecorLayout dl = GetDecorLayout(ep.decoration);
    int bob = (int)(decor_bob);
    if (dl.show1) {
        int dx = (int)(dl.x1 * flip);
        int dy = (int)(dl.y1 * flip) + bob;
        eye->SetDecor(dx, dy, dl.w1, dl.h1, dl.c1, true);
    } else {
        eye->SetDecor(0, 0, 0, 0, lv_color_black(), false);
    }
    if (dl.show2) {
        int dx = (int)(dl.x2 * flip);
        int dy = (int)(dl.y2 * flip) + bob;
        eye->SetDecor2(dx, dy, dl.w2, dl.h2, dl.c2, true);
    } else {
        eye->SetDecor2(0, 0, 0, 0, lv_color_black(), false);
    }
}

void EyeAnimator::Run() {
    if (!left_ || !right_) {
        ESP_LOGE(TAG, "Eye displays not initialized");
        vTaskDelete(nullptr);
        return;
    }

    int64_t now = esp_timer_get_time() / 1000;
    last_blink_time_ = now;
    last_saccade_time_ = now;
    next_blink_interval_ = random_range(2000, 4000);
    next_saccade_interval_ = random_range(1500, 3000);

    while (true) {
        now = esp_timer_get_time() / 1000;

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

        // Transition blink: trigger a quick blink when emotion changes
        bool emotion_changed = false;
        if (emotion != current_emotion_) {
            current_emotion_ = emotion;
            ESP_LOGI(TAG, "Emotion changed: %s", emotion.c_str());
            emotion_changed = true;
            if (!is_blinking_) {
                is_blinking_ = true;
                last_blink_time_ = now;
            }
        }
        if (state != current_state_) {
            current_state_ = state;
            ESP_LOGI(TAG, "State changed: %d", (int)state);
            if (state == EyeAnimState::LISTENING && !is_blinking_) {
                is_blinking_ = true;
                last_blink_time_ = now;
            }
        }

        EmotionParams ep = GetEmotionParams(current_emotion_);

        // State overlay
        float state_eyelid_adjust = 0;
        switch (current_state_) {
            case EyeAnimState::IDLE:
                state_eyelid_adjust = 0.12f;
                ep.blink_min_ms = std::min(ep.blink_min_ms, 2000);
                ep.blink_max_ms = std::min(ep.blink_max_ms, 4000);
                break;
            case EyeAnimState::LISTENING:
                state_eyelid_adjust = -0.05f;
                ep.pupil_scale *= 1.05f;
                break;
            case EyeAnimState::SPEAKING:
                break;
            case EyeAnimState::CONNECTING:
                break;
        }

        float target_top = std::clamp(ep.eyelid_top + state_eyelid_adjust, 0.0f, 1.0f);
        float target_bottom = ep.eyelid_bottom;
        float target_gaze_x = ep.gaze_x;
        float target_gaze_y = ep.gaze_y;
        float target_pupil = ep.pupil_scale;
        float target_brow_angle = ep.eyebrow_angle;
        float target_brow_y = ep.eyebrow_y;
        float target_sclera_h = ep.sclera_h_scale;

        // Connecting: eyes sweep left-right
        if (current_state_ == EyeAnimState::CONNECTING) {
            float phase = fmodf((float)now / 600.0f, 6.2832f);
            target_gaze_x = sinf(phase) * 0.50f;
            target_gaze_y = sinf(phase * 0.5f) * 0.15f;
        }

        // Speaking: pupil pulse + subtle gaze movement
        if (current_state_ == EyeAnimState::SPEAKING) {
            float t = (float)(now % 1200) / 1200.0f;
            target_pupil *= (1.0f + sinf(t * 6.2832f) * 0.08f);
            float st = (float)(now % 3000) / 3000.0f;
            target_gaze_x += sinf(st * 6.2832f) * 0.08f;
        }

        // Micro-saccade
        if ((now - last_saccade_time_) >= next_saccade_interval_) {
            saccade_dx_ = (float)random_range(-8, 8) / (float)MAX_GAZE_OFFSET;
            saccade_dy_ = (float)random_range(-5, 5) / (float)MAX_GAZE_OFFSET;
            last_saccade_time_ = now;
            next_saccade_interval_ = random_range(1200, 3000);
        }
        target_gaze_x += saccade_dx_;
        target_gaze_y += saccade_dy_;

        // Faster lerp during emotion transitions
        float base_lerp = emotion_changed ? 0.35f : 0.15f;
        float slow_lerp = emotion_changed ? 0.25f : 0.10f;

        cur_eyelid_top_ = lerpf(cur_eyelid_top_, target_top, base_lerp);
        cur_eyelid_bottom_ = lerpf(cur_eyelid_bottom_, target_bottom, base_lerp);
        cur_gaze_x_ = lerpf(cur_gaze_x_, target_gaze_x, base_lerp);
        cur_gaze_y_ = lerpf(cur_gaze_y_, target_gaze_y, base_lerp);
        cur_pupil_scale_ = lerpf(cur_pupil_scale_, target_pupil, base_lerp);
        cur_eyebrow_angle_ = lerpf(cur_eyebrow_angle_, target_brow_angle, slow_lerp);
        cur_eyebrow_y_ = lerpf(cur_eyebrow_y_, target_brow_y, slow_lerp);
        cur_sclera_h_ = lerpf(cur_sclera_h_, target_sclera_h, slow_lerp);

        // Blink
        float blink_override = 0.0f;
        if (is_blinking_) {
            int elapsed = (int)(now - last_blink_time_);
            if (elapsed < 70) {
                blink_override = (float)elapsed / 70.0f;
            } else if (elapsed < 110) {
                blink_override = 1.0f;
            } else if (elapsed < 180) {
                blink_override = 1.0f - (float)(elapsed - 110) / 70.0f;
            } else {
                is_blinking_ = false;
                next_blink_interval_ = random_range(ep.blink_min_ms, ep.blink_max_ms);
            }
        }

        if (!is_blinking_ && (now - last_blink_time_) >= next_blink_interval_) {
            is_blinking_ = true;
            last_blink_time_ = now;
        }

        // Compose final eyelid values
        float final_top_left = std::max(cur_eyelid_top_, blink_override);
        float final_top_right = std::max(cur_eyelid_top_, blink_override);
        float final_bottom = cur_eyelid_bottom_;

        // Wink: only close left eye, keep right open
        if (ep.wink_left && !is_blinking_) {
            final_top_right = 0.0f;
        }

        // During blink, momentarily close sclera more for drama
        float blink_sclera = cur_sclera_h_;
        if (blink_override > 0.5f) {
            blink_sclera *= (1.0f - (blink_override - 0.5f) * 0.3f);
        }

        // Decoration bobbing animation
        float decor_bob = sinf((float)(now % 2000) / 2000.0f * 6.2832f) * 4.0f;

        // Apply to both displays
        if (lvgl_port_lock(50)) {
            ApplyToDisplay(left_, ep, final_top_left, final_bottom,
                          cur_gaze_x_, cur_gaze_y_, cur_pupil_scale_,
                          cur_eyebrow_angle_, cur_eyebrow_y_,
                          blink_sclera, decor_bob);
            ApplyToDisplay(right_, ep, final_top_right, final_bottom,
                          cur_gaze_x_, cur_gaze_y_, cur_pupil_scale_,
                          cur_eyebrow_angle_, cur_eyebrow_y_,
                          blink_sclera, decor_bob);
            lvgl_port_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
