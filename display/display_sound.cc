/**
 * @file display_sound.cc
 * @brief 音乐预览页面相关功能（从 display.cc 拆分）
 */

#include "display.h"
#include "board.h"
#include "app/application.h"
#include "protocol.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "Display"

void Display::SoundPagePrev(void)
{
    DisplayLockGuard lock(this);
    if (current_page_ != PAGE_SOUND) {
        return;
    }
    sound_selected_index_ = (sound_selected_index_ - 1 + kSoundPreviewCount) % kSoundPreviewCount;
    sound_idle_ticks_ = 0;
    Application::GetInstance().GetAudioService().ResetDecoder();
    UpdateSoundPageUi();
    StartSoundPreviewPlayback(sound_selected_index_);
}

void Display::SoundPageNext(void)
{
    DisplayLockGuard lock(this);
    if (current_page_ != PAGE_SOUND) {
        return;
    }
    sound_selected_index_ = (sound_selected_index_ + 1) % kSoundPreviewCount;
    sound_idle_ticks_ = 0;
    Application::GetInstance().GetAudioService().ResetDecoder();
    UpdateSoundPageUi();
    StartSoundPreviewPlayback(sound_selected_index_);
}

void Display::SoundPagePlay(void)
{
    DisplayLockGuard lock(this);
    if (current_page_ != PAGE_SOUND) {
        return;
    }

    auto& audio_service = Application::GetInstance().GetAudioService();

    sound_idle_ticks_ = 0;
    sound_requested_index_ = -1;
    sound_stop_requested_ = true;
    audio_service.ResetDecoder();

    if (sound_preview_playing_) {
        sound_autoplay_ = false;
        sound_preview_playing_ = false;
        UpdateSoundPageUi();
        return;
    }

    sound_preview_playing_ = true;
    sound_stop_requested_ = false;
    UpdateSoundPageUi();
    StartSoundPreviewPlayback(sound_selected_index_);
}

void Display::SoundPageToggleAuto(void)
{
    DisplayLockGuard lock(this);
    if (current_page_ != PAGE_SOUND) {
        return;
    }

    sound_autoplay_ = !sound_autoplay_;
    sound_idle_ticks_ = 0;
    sound_stop_requested_ = !sound_autoplay_;
    Application::GetInstance().GetAudioService().ResetDecoder();
    if (sound_autoplay_) {
        sound_preview_playing_ = true;
        sound_stop_requested_ = false;
        StartSoundPreviewPlayback(sound_selected_index_);
    } else {
        sound_preview_playing_ = false;
    }
    UpdateSoundPageUi();
}

void Display::UpdateSoundPageUi()
{
    if (sound_name_label_ != nullptr) {
        lv_label_set_text(sound_name_label_, kSoundPreviews[sound_selected_index_].name);
    }

    if (sound_desc_label_ != nullptr) {
        lv_label_set_text(sound_desc_label_, kSoundPreviews[sound_selected_index_].hint);
    }

    if (sound_indicator_label_ != nullptr) {
        char indicator[16];
        snprintf(indicator, sizeof(indicator), "%d/%d", sound_selected_index_ + 1, kSoundPreviewCount);
        lv_label_set_text(sound_indicator_label_, indicator);
    }

    if (sound_mode_label_ != nullptr) {
        lv_label_set_text(sound_mode_label_, sound_autoplay_ ? "自动" : "单次");
    }

    if (sound_state_label_ != nullptr) {
        lv_label_set_text(sound_state_label_, sound_preview_playing_ ? "播放" : "暂停");
    }

    if (sound_position_track_ != nullptr && sound_position_fill_ != nullptr) {
        const int inner_width = std::max(0, static_cast<int>(lv_obj_get_width(sound_position_track_)) - 2);
        const int fill_width = std::max(1, inner_width * (sound_selected_index_ + 1) / kSoundPreviewCount);
        const int fill_height = std::max(1, static_cast<int>(lv_obj_get_height(sound_position_track_)) - 2);
        lv_obj_set_size(sound_position_fill_, fill_width, fill_height);
    }
}

void Display::EnsureSoundTimer()
{
    if (sound_timer_ != nullptr) {
        return;
    }

    esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) {
            static_cast<Display*>(arg)->AdvanceSoundTick();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "sound_page",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &sound_timer_));
}

void Display::EnsureSoundPreviewTask()
{
    if (sound_preview_task_handle_ != nullptr) {
        return;
    }

    xTaskCreate([](void* arg) {
        static_cast<Display*>(arg)->SoundPreviewTask();
        vTaskDelete(nullptr);
    }, "sound_preview", 4096 * 2, this, 2, &sound_preview_task_handle_);  // 8KB 栈，防止解析大 P3 文件时溢出
}

void Display::StopSoundTimer()
{
    if (sound_timer_ != nullptr) {
        esp_timer_stop(sound_timer_);
    }
}

void Display::StartSoundPreviewPlayback(int index)
{
    auto& audio_service = Application::GetInstance().GetAudioService();
    sound_requested_index_ = -1;
    sound_stop_requested_ = true;
    audio_service.ResetDecoder();

    // 确保音频输出已启用（空闲超时或停止播放可能关闭了输出）
    auto codec = Board::GetInstance().GetAudioCodec();
    if (codec != nullptr && !codec->output_enabled()) {
        codec->EnableOutput(true);
    }

    sound_preview_playing_ = true;
    sound_stop_requested_ = false;
    sound_requested_index_ = index;
    EnsureSoundPreviewTask();
    if (sound_preview_task_handle_ != nullptr) {
        xTaskNotifyGive(sound_preview_task_handle_);
    }
}

void Display::StopSoundPreviewPlayback()
{
    sound_autoplay_ = false;
    sound_preview_playing_ = false;
    sound_requested_index_ = -1;
    sound_stop_requested_ = true;
    auto codec = Board::GetInstance().GetAudioCodec();
    if (codec != nullptr) {
        codec->EnableOutput(false);
    }
    Application::GetInstance().GetAudioService().StopPlayback();
}

void Display::StartAlarmPlayback()
{
    sound_autoplay_ = false;
    sound_idle_ticks_ = 0;
    StartSoundPreviewPlayback(alarm_music_index_);
}

void Display::AdvanceSoundTick()
{
    DisplayLockGuard lock(this);
    if (current_page_ != PAGE_SOUND) {
        return;
    }

    const bool audio_idle = Application::GetInstance().GetAudioService().IsIdle();
    if (sound_autoplay_) {
        if (audio_idle) {
            sound_preview_playing_ = false;
            sound_idle_ticks_++;
            if (sound_idle_ticks_ >= 2) {
                sound_idle_ticks_ = 0;
                sound_selected_index_ = (sound_selected_index_ + 1) % kSoundPreviewCount;
                UpdateSoundPageUi();
                StartSoundPreviewPlayback(sound_selected_index_);
                return;
            }
        } else {
            sound_preview_playing_ = true;
            sound_idle_ticks_ = 0;
        }
    } else if (audio_idle) {
        sound_preview_playing_ = false;
    }

    UpdateSoundPageUi();
}

void Display::SoundPreviewTask()
{
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        while (true) {
            int index = sound_requested_index_;
            if (index < 0 || index >= kSoundPreviewCount) {
                break;
            }
            sound_requested_index_ = -1;
            sound_stop_requested_ = false;

            const auto& sound = kSoundPreviews[index].sound;
            const char* data = sound.data();
            size_t size = sound.size();
            auto& audio_service = Application::GetInstance().GetAudioService();

            for (const char* p = data; p < data + size; ) {
                if (sound_stop_requested_ || sound_requested_index_ != -1) {
                    break;
                }

                auto p3 = reinterpret_cast<const BinaryProtocol3*>(p);
                p += sizeof(BinaryProtocol3);

                auto payload_size = ntohs(p3->payload_size);
                // 防御性检查：payload 大小异常则跳过
                if (payload_size == 0 || payload_size > 4096) {
                    ESP_LOGW(TAG, "Invalid P3 payload size: %d", payload_size);
                    break;
                }
                auto packet = std::make_unique<AudioStreamPacket>();
                packet->sample_rate = 16000;
                packet->frame_duration = 60;
                packet->payload.resize(payload_size);
                memcpy(packet->payload.data(), p3->payload, payload_size);
                p += payload_size;

                if (!audio_service.PushPacketToDecodeQueue(std::move(packet), false)) {
                    // 队列满时稍等后回退指针重试
                    vTaskDelay(pdMS_TO_TICKS(10));
                    if (sound_stop_requested_ || sound_requested_index_ != -1) {
                        break;
                    }
                    p -= sizeof(BinaryProtocol3) + payload_size;
                }
            }

            if (sound_stop_requested_) {
                sound_preview_playing_ = false;
                sound_stop_requested_ = false;
            }
        }
    }
}
