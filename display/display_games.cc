/**
 * @file display_games.cc
 * @brief 娱乐功能页面：躲避游戏、反应测试、记忆测试、呼吸引导（从 display.cc 拆分）
 */

#include "display.h"
#include "board.h"
#include "app/application.h"
#include "font_awesome_symbols.h"
#include "settings/settings.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <esp_random.h>

#define TAG "Display"

// ============================================================
// 娱乐功能页面导航
// ============================================================

void Display::FunPagePrev(void)
{
    DisplayLockGuard lock(this);
    if (current_page_ != PAGE_FUN) {
        return;
    }
    fun_selected_index_ = (fun_selected_index_ - 1 + kVisibleFunItemCount) % kVisibleFunItemCount;
    UpdateFunPageUi();
}

void Display::FunPageNext(void)
{
    DisplayLockGuard lock(this);
    if (current_page_ != PAGE_FUN) {
        return;
    }
    fun_selected_index_ = (fun_selected_index_ + 1) % kVisibleFunItemCount;
    UpdateFunPageUi();
}

void Display::FunPageEnter(void)
{
    if (current_page_ != PAGE_FUN) {
        return;
    }
    if (fun_selected_index_ == 0) {
        Switch_Game_Page();
    } else if (fun_selected_index_ == 1) {
        Switch_Reflex_Page();
    } else if (fun_selected_index_ == 2) {
        Switch_Memory_Page();
    } else {
        Switch_Breathe_Page();
    }
}

void Display::UpdateFunPageUi()
{
    if (fun_selected_index_ >= kVisibleFunItemCount) {
        fun_selected_index_ = 0;
    }

    if (fun_icon_label_ != nullptr) {
        const char* icon = FONT_AWESOME_PLAY;
        if (fun_selected_index_ == 1) {
            icon = FONT_AWESOME_BELL;
        } else if (fun_selected_index_ == 2) {
            icon = FONT_AWESOME_EDIT;
        } else if (fun_selected_index_ == 3) {
            icon = FONT_AWESOME_EMOJI_RELAXED;
        }
        lv_label_set_text(fun_icon_label_, icon);
    }

    if (fun_title_label_ != nullptr) {
        lv_label_set_text(fun_title_label_, kFunItems[fun_selected_index_].title);
    }

    if (fun_hint_label_ != nullptr) {
        lv_label_set_text(fun_hint_label_, kFunItems[fun_selected_index_].hint);
    }

    if (fun_indicator_label_ != nullptr) {
        char indicator[16];
        snprintf(indicator, sizeof(indicator), "%d/%d", fun_selected_index_ + 1, kVisibleFunItemCount);
        lv_label_set_text(fun_indicator_label_, indicator);
    }
}

// ============================================================
// 快捷设置页面
// ============================================================

void Display::QuickPageAdjust(int delta)
{
    if (current_page_ != PAGE_QUICK) {
        return;
    }

    auto& board = Board::GetInstance();
    if (quick_selected_index_ == 0) {
        auto backlight = board.GetBacklight();
        if (backlight != nullptr) {
            int brightness = ClampValue(static_cast<int>(backlight->target_brightness()) + delta, 10, 100);
            backlight->SetBrightness(static_cast<uint8_t>(brightness), true);
        }
    } else {
        auto codec = board.GetAudioCodec();
        if (codec != nullptr) {
            int volume = ClampValue(codec->output_volume() + delta, 0, 100);
            codec->SetOutputVolume(volume);
        }
    }

    DisplayLockGuard lock(this);
    UpdateQuickPageUi();
}

void Display::QuickPageToggleSelection(void)
{
    DisplayLockGuard lock(this);
    if (current_page_ != PAGE_QUICK) {
        return;
    }
    quick_selected_index_ = (quick_selected_index_ + 1) % 2;
    UpdateQuickPageUi();
}

void Display::UpdateQuickPageUi()
{
    auto backlight = Board::GetInstance().GetBacklight();
    auto codec = Board::GetInstance().GetAudioCodec();
    int brightness_percent = 0;
    int volume_percent = 0;

    if (quick_brightness_value_ != nullptr) {
        if (backlight != nullptr) {
            char brightness[12];
            brightness_percent = backlight->target_brightness();
            snprintf(brightness, sizeof(brightness), "%d%%", brightness_percent);
            lv_label_set_text(quick_brightness_value_, brightness);
        } else {
            lv_label_set_text(quick_brightness_value_, "无");
        }
    }

    if (quick_volume_value_ != nullptr) {
        if (codec != nullptr) {
            char volume[12];
            volume_percent = codec->output_volume();
            snprintf(volume, sizeof(volume), "%d%%", volume_percent);
            lv_label_set_text(quick_volume_value_, volume);
        } else {
            lv_label_set_text(quick_volume_value_, "无");
        }
    }

    SetCardSelectionState(quick_brightness_card_, quick_selected_index_ == 0);
    SetCardSelectionState(quick_volume_card_, quick_selected_index_ == 1);
    SetMeterStyle(quick_brightness_bar_track_, quick_brightness_bar_fill_, quick_selected_index_ == 0);
    SetMeterStyle(quick_volume_bar_track_, quick_volume_bar_fill_, quick_selected_index_ == 1);

    if (quick_brightness_bar_track_ != nullptr && quick_brightness_bar_fill_ != nullptr) {
        const int inner_width = std::max(0, static_cast<int>(lv_obj_get_width(quick_brightness_bar_track_)) - 2);
        const int fill_width = brightness_percent > 0 ? std::max(1, inner_width * brightness_percent / 100) : 0;
        const int fill_height = std::max(1, static_cast<int>(lv_obj_get_height(quick_brightness_bar_track_)) - 2);
        lv_obj_set_size(quick_brightness_bar_fill_, fill_width, fill_height);
    }

    if (quick_volume_bar_track_ != nullptr && quick_volume_bar_fill_ != nullptr) {
        const int inner_width = std::max(0, static_cast<int>(lv_obj_get_width(quick_volume_bar_track_)) - 2);
        const int fill_width = volume_percent > 0 ? std::max(1, inner_width * volume_percent / 100) : 0;
        const int fill_height = std::max(1, static_cast<int>(lv_obj_get_height(quick_volume_bar_track_)) - 2);
        lv_obj_set_size(quick_volume_bar_fill_, fill_width, fill_height);
    }
}

// ============================================================
// 躲避游戏
// ============================================================

void Display::GameMove(int delta)
{
    DisplayLockGuard lock(this);
    if (current_page_ != PAGE_GAME) {
        return;
    }
    game_player_lane_ = ClampValue(game_player_lane_ + delta, 0, kGameLaneCount - 1);
    UpdateGamePageUi();
}

void Display::GameToggle(void)
{
    DisplayLockGuard lock(this);
    if (current_page_ != PAGE_GAME) {
        return;
    }

    if (game_over_) {
        game_over_ = false;
        game_score_ = 0;
        game_player_lane_ = 1;
        game_obstacle_lane_ = esp_random() % kGameLaneCount;
        game_obstacle_y_ = 2;
    }

    EnsureGameTimer();
    if (game_running_) {
        game_running_ = false;
        StopGameTimer();
    } else {
        game_running_ = true;
        StopGameTimer();
        esp_timer_start_periodic(game_timer_, 180 * 1000);
        Application::GetInstance().PlaySound(Lang::Sounds::P3_SUCCESS);
    }
    UpdateGamePageUi();
}

void Display::GameReset(void)
{
    DisplayLockGuard lock(this);
    game_running_ = false;
    game_over_ = false;
    game_score_ = 0;
    game_player_lane_ = 1;
    game_obstacle_lane_ = esp_random() % kGameLaneCount;
    game_obstacle_y_ = 2;
    StopGameTimer();
    UpdateGamePageUi();
}

void Display::EnsureGameTimer()
{
    if (game_timer_ != nullptr) {
        return;
    }

    esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) {
            static_cast<Display*>(arg)->AdvanceGameTick();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "fun_game",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &game_timer_));
}

void Display::StopGameTimer()
{
    if (game_timer_ != nullptr) {
        esp_timer_stop(game_timer_);
    }
}

void Display::AdvanceGameTick()
{
    DisplayLockGuard lock(this);
    if (current_page_ != PAGE_GAME || !game_running_ || game_field_ == nullptr) {
        return;
    }

    const int player_y = lv_obj_get_height(game_field_) - 6;
    const int obstacle_step = GetGameTickStep(game_score_);
    game_obstacle_y_ += obstacle_step;

    if (game_obstacle_y_ >= player_y) {
        if (game_obstacle_lane_ == game_player_lane_) {
            game_running_ = false;
            game_over_ = true;
            StopGameTimer();
            SaveGameBestScore();
            Application::GetInstance().PlaySound(Lang::Sounds::P3_EXCLAMATION);
        } else {
            game_score_++;
            if (game_score_ > game_best_score_) {
                game_best_score_ = game_score_;
                SaveGameBestScore();
            }
            game_obstacle_lane_ = esp_random() % kGameLaneCount;
            game_obstacle_y_ = 2;
            Application::GetInstance().PlaySound(Lang::Sounds::P3_POPUP);
        }
    }

    UpdateGamePageUi();
}

void Display::SaveGameBestScore()
{
    Settings settings("fun", true);
    settings.SetInt("best_score", game_best_score_);
}

void Display::UpdateGamePageUi()
{
    if (game_field_ != nullptr) {
        const int padding = 4;
        const int actor_width = 16;
        const int lane_width = std::max(1, static_cast<int>((lv_obj_get_width(game_field_) - padding * 2) / kGameLaneCount));
        const int player_x = padding + game_player_lane_ * lane_width + std::max(0, (lane_width - actor_width) / 2);
        const int obstacle_x = padding + game_obstacle_lane_ * lane_width + std::max(0, (lane_width - actor_width) / 2);
        const int player_y = lv_obj_get_height(game_field_) - 6;
        const int obstacle_y = ClampValue(game_obstacle_y_, 2, player_y);

        if (game_player_ != nullptr) {
            lv_obj_set_pos(game_player_, player_x, player_y);
        }
        if (game_obstacle_ != nullptr) {
            lv_obj_set_pos(game_obstacle_, obstacle_x, obstacle_y);
        }
    }

    SetCardSelectionState(game_status_card_, game_running_ || game_over_);

    if (game_status_label_ != nullptr) {
        const int obstacle_step = GetGameTickStep(game_score_);
        if (game_over_) {
            lv_label_set_text(game_status_label_, "撞上");
        } else if (game_running_ && obstacle_step >= 5) {
            lv_label_set_text(game_status_label_, "加速");
        } else if (game_running_) {
            lv_label_set_text(game_status_label_, "进行");
        } else if (game_score_ == 0) {
            lv_label_set_text(game_status_label_, "就绪");
        } else {
            lv_label_set_text(game_status_label_, "暂停");
        }
    }

    if (game_score_label_ != nullptr) {
        char score[24];
        snprintf(score, sizeof(score), "%02d/%02d", game_score_, game_best_score_);
        lv_label_set_text(game_score_label_, score);
    }
}

// ============================================================
// 反应测试
// ============================================================

void Display::ReflexTap(void)
{
    if (current_page_ != PAGE_REFLEX) {
        return;
    }

    DisplayLockGuard lock(this);
    if (reflex_state_ == kReflexWaiting) {
        reflex_state_ = kReflexEarly;
        reflex_last_ms_ = -1;
        StopReflexTimer();
        UpdateReflexPageUi();
        Application::GetInstance().PlaySound(Lang::Sounds::P3_EXCLAMATION);
        return;
    }

    if (reflex_state_ == kReflexReady) {
        reflex_last_ms_ = static_cast<int>((esp_timer_get_time() - reflex_go_time_us_) / 1000);
        reflex_state_ = kReflexResult;
        if (reflex_best_ms_ == 0 || reflex_last_ms_ < reflex_best_ms_) {
            reflex_best_ms_ = reflex_last_ms_;
            SaveReflexBest();
        }
        UpdateReflexPageUi();
        Application::GetInstance().PlaySound(Lang::Sounds::P3_SUCCESS);
        return;
    }

    reflex_state_ = kReflexWaiting;
    reflex_last_ms_ = -1;
    reflex_go_time_us_ = 0;
    EnsureReflexTimer();
    StopReflexTimer();
    esp_timer_start_once(reflex_timer_, (1200 + (esp_random() % 2801)) * 1000);
    UpdateReflexPageUi();
}

void Display::EnsureReflexTimer()
{
    if (reflex_timer_ != nullptr) {
        return;
    }

    esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) {
            static_cast<Display*>(arg)->TriggerReflexReady();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "reflex_wait",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &reflex_timer_));
}

void Display::StopReflexTimer()
{
    if (reflex_timer_ != nullptr) {
        esp_timer_stop(reflex_timer_);
    }
}

void Display::TriggerReflexReady()
{
    DisplayLockGuard lock(this);
    if (current_page_ != PAGE_REFLEX || reflex_state_ != kReflexWaiting) {
        return;
    }

    reflex_state_ = kReflexReady;
    reflex_go_time_us_ = esp_timer_get_time();
    UpdateReflexPageUi();
    Application::GetInstance().PlaySound(Lang::Sounds::P3_POPUP);
}

void Display::SaveReflexBest()
{
    Settings settings("fun", true);
    settings.SetInt("reflex_best_ms", reflex_best_ms_);
}

void Display::UpdateReflexPageUi()
{
    if (reflex_status_card_ != nullptr) {
        SetCardSelectionState(reflex_status_card_,
            reflex_state_ == kReflexReady || reflex_state_ == kReflexEarly || reflex_state_ == kReflexResult);
    }

    if (reflex_status_label_ != nullptr) {
        const char* status = "准备";
        if (reflex_state_ == kReflexWaiting) {
            status = "等待";
        } else if (reflex_state_ == kReflexReady) {
            status = "开始";
        } else if (reflex_state_ == kReflexEarly) {
            status = "过早";
        } else if (reflex_state_ == kReflexResult) {
            status = "完成";
        }
        lv_label_set_text(reflex_status_label_, status);
    }

    if (reflex_value_label_ != nullptr) {
        char value[24];
        if (reflex_state_ == kReflexWaiting) {
            snprintf(value, sizeof(value), "等待");
        } else if (reflex_state_ == kReflexReady) {
            snprintf(value, sizeof(value), "现在");
        } else if (reflex_state_ == kReflexEarly) {
            snprintf(value, sizeof(value), "失误");
        } else if (reflex_last_ms_ >= 0) {
            snprintf(value, sizeof(value), "%03d", reflex_last_ms_);
        } else {
            snprintf(value, sizeof(value), "点击");
        }
        lv_label_set_text(reflex_value_label_, value);
    }

    if (reflex_hint_label_ != nullptr) {
        const char* hint = "点击准备";
        if (reflex_state_ == kReflexWaiting) {
            hint = "等待开始";
        } else if (reflex_state_ == kReflexReady) {
            hint = "立即点击";
        } else if (reflex_state_ == kReflexEarly) {
            hint = "按早了";
        } else if (reflex_state_ == kReflexResult) {
            hint = "再来一次";
        }
        lv_label_set_text(reflex_hint_label_, hint);
    }

    if (reflex_best_label_ != nullptr) {
        char best[24];
        if (reflex_best_ms_ > 0) {
            snprintf(best, sizeof(best), "最佳%03d", reflex_best_ms_);
        } else {
            snprintf(best, sizeof(best), "最佳---");
        }
        lv_label_set_text(reflex_best_label_, best);
    }
}

// ============================================================
// 记忆测试
// ============================================================

void Display::MemoryTap(int lane)
{
    if (current_page_ != PAGE_MEMORY || lane < 0 || lane >= kMemoryLaneCount) {
        return;
    }

    DisplayLockGuard lock(this);
    if (memory_state_ == kMemoryIdle || memory_state_ == kMemoryOver) {
        memory_sequence_length_ = 2;
        for (int i = 0; i < memory_sequence_length_; ++i) {
            memory_sequence_[i] = esp_random() % kMemoryLaneCount;
        }
        memory_input_flash_pending_ = false;
        Application::GetInstance().PlaySound(Lang::Sounds::P3_SUCCESS);
        StartMemoryRound();
        return;
    }

    if (memory_state_ == kMemoryShow) {
        return;
    }

    memory_active_lane_ = lane;
    memory_input_flash_pending_ = false;
    if (lane == memory_sequence_[memory_input_index_]) {
        memory_input_index_++;
        if (memory_input_index_ >= memory_sequence_length_) {
            if (memory_sequence_length_ > memory_best_) {
                memory_best_ = memory_sequence_length_;
                SaveMemoryBest();
            }
            if (memory_sequence_length_ < kMemoryMaxLength) {
                memory_sequence_[memory_sequence_length_] = esp_random() % kMemoryLaneCount;
                memory_sequence_length_++;
            }
            Application::GetInstance().PlaySound(Lang::Sounds::P3_SUCCESS);
            StartMemoryRound();
            return;
        }
        EnsureMemoryTimer();
        StopMemoryTimer();
        memory_input_flash_pending_ = true;
        ESP_ERROR_CHECK(esp_timer_start_once(memory_timer_, 140 * 1000));
        Application::GetInstance().PlaySound(Lang::Sounds::P3_POPUP);
        UpdateMemoryPageUi();
        return;
    }

    memory_state_ = kMemoryOver;
    StopMemoryTimer();
    memory_input_flash_pending_ = false;
    UpdateMemoryPageUi();
    Application::GetInstance().PlaySound(Lang::Sounds::P3_EXCLAMATION);
}

void Display::EnsureMemoryTimer()
{
    if (memory_timer_ != nullptr) {
        return;
    }

    esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) {
            static_cast<Display*>(arg)->AdvanceMemoryTick();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "memory_game",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &memory_timer_));
}

void Display::StopMemoryTimer()
{
    if (memory_timer_ != nullptr) {
        esp_timer_stop(memory_timer_);
    }
}

void Display::StartMemoryRound()
{
    memory_state_ = kMemoryShow;
    memory_show_index_ = 0;
    memory_input_index_ = 0;
    memory_flash_on_ = true;
    memory_input_flash_pending_ = false;
    memory_active_lane_ = memory_sequence_[0];
    EnsureMemoryTimer();
    StopMemoryTimer();
    esp_timer_start_periodic(memory_timer_, kMemoryTickMs * 1000);
    UpdateMemoryPageUi();
}

void Display::SaveMemoryBest()
{
    Settings settings("fun", true);
    settings.SetInt("memory_best", memory_best_);
}

void Display::AdvanceMemoryTick()
{
    DisplayLockGuard lock(this);
    if (current_page_ != PAGE_MEMORY) {
        return;
    }

    if (memory_state_ == kMemoryInput && memory_input_flash_pending_) {
        memory_input_flash_pending_ = false;
        memory_active_lane_ = -1;
        StopMemoryTimer();
        UpdateMemoryPageUi();
        return;
    }

    if (memory_state_ != kMemoryShow) {
        return;
    }

    if (memory_flash_on_) {
        memory_flash_on_ = false;
        memory_active_lane_ = -1;
    } else {
        memory_show_index_++;
        if (memory_show_index_ >= memory_sequence_length_) {
            memory_state_ = kMemoryInput;
            memory_input_index_ = 0;
            memory_active_lane_ = -1;
            StopMemoryTimer();
        } else {
            memory_flash_on_ = true;
            memory_active_lane_ = memory_sequence_[memory_show_index_];
        }
    }

    UpdateMemoryPageUi();
}

void Display::UpdateMemoryPageUi()
{
    SetCardSelectionState(memory_board_card_, memory_state_ != kMemoryIdle);
    SetCardSelectionState(memory_status_card_, memory_state_ == kMemoryShow || memory_state_ == kMemoryOver);

    for (int i = 0; i < kMemoryLaneCount; ++i) {
        const bool active = (memory_active_lane_ == i);
        SetCardSelectionState(memory_lane_blocks_[i], active);
        if (memory_lane_labels_[i] != nullptr) {
            lv_obj_set_style_text_color(memory_lane_labels_[i], active ? lv_color_white() : lv_color_black(), 0);
        }
    }

    if (memory_status_label_ != nullptr) {
        const char* status = "开始";
        if (memory_state_ == kMemoryShow) {
            status = "观察";
        } else if (memory_state_ == kMemoryInput) {
            status = "作答";
        } else if (memory_state_ == kMemoryOver) {
            status = "失误";
        }
        lv_label_set_text(memory_status_label_, status);
    }

    if (memory_score_label_ != nullptr) {
        char score[20];
        snprintf(score, sizeof(score), "%02d/%02d", memory_sequence_length_, memory_best_);
        lv_label_set_text(memory_score_label_, score);
    }

    if (memory_hint_label_ != nullptr) {
        const char* hint = "按下开始";
        if (memory_state_ == kMemoryShow) {
            hint = "记住顺序";
        } else if (memory_state_ == kMemoryInput) {
            hint = "重复 左中右";
        } else if (memory_state_ == kMemoryOver) {
            hint = "按下重试";
        }
        lv_label_set_text(memory_hint_label_, hint);
    }
}

// ============================================================
// 呼吸引导
// ============================================================

void Display::BreatheToggle(void)
{
    if (current_page_ != PAGE_BREATHE) {
        return;
    }

    DisplayLockGuard lock(this);
    EnsureBreatheTimer();
    if (breathe_running_) {
        breathe_running_ = false;
        StopBreatheTimer();
    } else {
        breathe_running_ = true;
        StopBreatheTimer();
        esp_timer_start_periodic(breathe_timer_, kBreatheTickMs * 1000);
    }
    UpdateBreathePageUi();
}

void Display::BreatheAdjust(int delta)
{
    if (current_page_ != PAGE_BREATHE) {
        return;
    }

    DisplayLockGuard lock(this);
    const int new_pace = ClampValue(breathe_pace_seconds_ + delta, 3, 6);
    if (new_pace == breathe_pace_seconds_) {
        return;
    }
    breathe_pace_seconds_ = new_pace;
    Settings settings("fun", true);
    settings.SetInt("breathe_pace", breathe_pace_seconds_);
    UpdateBreathePageUi();
}

void Display::EnsureBreatheTimer()
{
    if (breathe_timer_ != nullptr) {
        return;
    }

    esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) {
            static_cast<Display*>(arg)->AdvanceBreatheTick();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "breathe_flow",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &breathe_timer_));
}

void Display::StopBreatheTimer()
{
    if (breathe_timer_ != nullptr) {
        esp_timer_stop(breathe_timer_);
    }
}

void Display::AdvanceBreatheTick()
{
    DisplayLockGuard lock(this);
    if (current_page_ != PAGE_BREATHE || !breathe_running_) {
        return;
    }

    const int phase_ticks = std::max(1, GetBreathePhaseTicks(breathe_phase_, breathe_pace_seconds_));
    breathe_phase_tick_++;
    if (breathe_phase_tick_ >= phase_ticks) {
        breathe_phase_tick_ = 0;
        breathe_phase_ = (breathe_phase_ + 1) % 4;
        if (breathe_phase_ == 0) {
            breathe_cycles_++;
        }
    }

    UpdateBreathePageUi();
}

void Display::UpdateBreathePageUi()
{
    if (breathe_phase_card_ != nullptr) {
        SetCardSelectionState(breathe_phase_card_, breathe_running_);
    }

    if (breathe_phase_label_ != nullptr) {
        const char* phase = "准备";
        if (breathe_running_) {
            if (breathe_phase_ == 0) {
                phase = "吸气";
            } else if (breathe_phase_ == 1) {
                phase = "屏息";
            } else if (breathe_phase_ == 2) {
                phase = "呼气";
            } else {
                phase = "停顿";
            }
        }
        lv_label_set_text(breathe_phase_label_, phase);
    }

    if (breathe_cycle_label_ != nullptr) {
        char cycle[16];
        snprintf(cycle, sizeof(cycle), "轮%02d", breathe_cycles_);
        lv_label_set_text(breathe_cycle_label_, cycle);
    }

    if (breathe_status_label_ != nullptr) {
        char status[24];
        snprintf(status, sizeof(status), "节奏 %d秒", breathe_pace_seconds_);
        lv_label_set_text(breathe_status_label_, status);
    }

    if (breathe_bar_track_ != nullptr && breathe_bar_fill_ != nullptr) {
        const int phase_ticks = std::max(1, GetBreathePhaseTicks(breathe_phase_, breathe_pace_seconds_));
        const int inner_width = std::max(0, static_cast<int>(lv_obj_get_width(breathe_bar_track_)) - 2);
        const int fill_width = breathe_running_
            ? std::max(1, inner_width * (breathe_phase_tick_ + 1) / phase_ticks)
            : inner_width / 3;
        const int fill_height = std::max(1, static_cast<int>(lv_obj_get_height(breathe_bar_track_)) - 2);
        lv_obj_set_size(breathe_bar_fill_, fill_width, fill_height);
    }
}
