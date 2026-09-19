#ifndef DISPLAY_H
#define DISPLAY_H

#include <chrono>
#include <string>
#include <cstdio>
#include <cstring>
#include <algorithm>

#include <esp_log.h>
#include <esp_pm.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>

#define Switch_Count 15

LV_IMG_DECLARE(zhuye)
LV_IMG_DECLARE(duihua)
LV_IMG_DECLARE(jiance)
LV_IMG_DECLARE(spo2_image)
LV_IMG_DECLARE(heart_image)
LV_IMG_DECLARE(tianqi)
LV_IMG_DECLARE(guanyu)
LV_IMG_DECLARE(shoudiantong)
LV_IMG_DECLARE(miaobiao)
LV_IMG_DECLARE(naozhong)
LV_IMG_DECLARE(yundong)
LV_IMG_DECLARE(shezhi)
LV_IMG_DECLARE(youxi)
LV_IMG_DECLARE(yinyue)
LV_IMG_DECLARE(mpu)

// ============================================================
// 共用数据结构（需在 inline 函数之前定义）
// ============================================================

struct SoundPreview {
    const char* name;
    const char* hint;
    std::string_view sound;
};

struct FunItemInfo {
    const char* title;
    const char* hint;
};

// 声音预览数据（定义在 display.cc，依赖 Lang::Sounds）
extern const SoundPreview kSoundPreviews[];
extern const int kSoundPreviewCount;

inline constexpr FunItemInfo kFunItems[] = {
    {"躲避", "躲开方块"}, {"反应", "看到提示就按"}, {"记忆", "跟着顺序按"}, {"呼吸", "呼吸节奏引导"},
};

// ============================================================
// 共用内联辅助函数（供 display.cc 及拆分的子文件使用）
// ============================================================

inline int ClampValue(int value, int min_value, int max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

inline void SetCardSelectionState(lv_obj_t* card, bool active) {
    if (card == nullptr) return;
    lv_obj_set_style_bg_color(card, active ? lv_color_black() : lv_color_white(), 0);
    lv_obj_set_style_text_color(card, active ? lv_color_white() : lv_color_black(), 0);
    lv_obj_set_style_border_width(card, active ? 2 : 1, 0);
    lv_obj_set_style_border_color(card, lv_color_black(), 0);
}

inline void SetMeterStyle(lv_obj_t* track, lv_obj_t* fill, bool active) {
    if (track != nullptr) {
        lv_obj_set_style_border_color(track, active ? lv_color_white() : lv_color_black(), 0);
        lv_obj_set_style_bg_opa(track, LV_OPA_TRANSP, 0);
    }
    if (fill != nullptr) {
        lv_obj_set_style_bg_color(fill, active ? lv_color_white() : lv_color_black(), 0);
        lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    }
}

inline void FormatCompactSteps(char* buffer, size_t buffer_size, unsigned long steps) {
    if (steps < 1000) {
        snprintf(buffer, buffer_size, "%lu", steps);
    } else if (steps < 10000) {
        snprintf(buffer, buffer_size, "%.1fk", steps / 1000.0f);
    } else {
        snprintf(buffer, buffer_size, "%.0fk", steps / 1000.0f);
    }
}

inline constexpr int kFunItemCount = static_cast<int>(sizeof(kFunItems) / sizeof(kFunItems[0]));
inline constexpr int kVisibleFunItemCount = kFunItemCount;
inline constexpr int kQuickStep = 10;
inline constexpr int kGameLaneCount = 4;
inline constexpr int kReflexIdle = 0, kReflexWaiting = 1, kReflexReady = 2, kReflexEarly = 3, kReflexResult = 4;
inline constexpr int kMemoryIdle = 0, kMemoryShow = 1, kMemoryInput = 2, kMemoryOver = 3;
inline constexpr int kBreatheTickMs = 250, kBreatheTicksPerSecond = 1000 / kBreatheTickMs;
inline constexpr int kMemoryTickMs = 320, kMemoryLaneCount = 3, kMemoryMaxLength = 12;
inline constexpr int kSoundTickMs = 250;

inline int GetBreathePhaseTicks(int phase, int pace_seconds) {
    return (phase == 1 || phase == 3) ? kBreatheTicksPerSecond : pace_seconds * kBreatheTicksPerSecond;
}

inline int GetGameTickStep(int score) {
    return ClampValue(3 + score / 3, 3, 6);
}

// ============================================================

struct Select_Page {
    lv_obj_t* img_select = nullptr;
    lv_obj_t* label_select = nullptr;
    lv_obj_t* page = nullptr;
};

struct DisplayPage {
    const lv_img_dsc_t* img = nullptr;
    const char* text = nullptr;
};

enum OledDisplayPage {
    PAGE_BOOT = 0,
    PAGE_SWITCH,
    PAGE_MAIN,
    PAGE_CHAT,
    PAGE_DETE,
    PAGE_WEATHER,
    PAGE_FLASHLIGHT,
    PAGE_ABOUT,
    PAGE_RECONFIG,
    PAGE_MIAOBIAO,
    PAGE_NAOZHONG,
    PAGE_MPU6050,
    PAGE_SPORT,
    PAGE_FUN,
    PAGE_GAME,
    PAGE_REFLEX,
    PAGE_MEMORY,
    PAGE_BREATHE,
    PAGE_QUICK,
    PAGE_SOUND,
    PAGE_TIME_SETTING,
    PAGE_INIT,
};

enum OledDisplaySwitch {
    Switch_MAIN = 0,
    Switch_CHAT,
    Switch_DETE,
    Switch_WEATHER,
    Switch_FLASHLIGHT,
    Switch_MIAOBIAO,
    Switch_NAOZHONG,
    Switch_MPU6050,
    Switch_SPORT,
    Switch_SOUND,
    Switch_QUICK,
    Switch_FUN,
    Switch_TIME_SETTING,
    Switch_RECONFIG,
    Switch_ABOUT,
};

struct DisplayFonts {
    const lv_font_t* text_font = nullptr;
    const lv_font_t* text_large_font = nullptr;
    const lv_font_t* icon_font = nullptr;
    const lv_font_t* emoji_font = nullptr;
};

class Display {
public:
    Display();
    virtual ~Display();

    void UpdateTime(void);
    void UpdateDate(void);
    void UpdateWeather(void);
    void UpDateNetWork(void);
    void UpDateBattery(void);
    void UpDateHealth(void);

    void SetOnlineMode(bool online);

    void Switch_Main_Page(void);
    void Switch_Dialogue_Page(void);
    void Switch_Health_Check_Page(void);
    void Switch_Weather_Page(void);
    void Switch_Flashlight_Page(void);
    void Switch_About_Page(void);
    void Switch_Reconfig_Page(void);
    void Switch_Miaobiao_Page(void);
    void Switch_Naozhong_Page(void);
    void Switch_Mpu6050_Page(void);
    void Switch_Sport_Page(void);
    void Switch_Fun_Page(void);
    void Switch_Game_Page(void);
    void Switch_Reflex_Page(void);
    void Switch_Memory_Page(void);
    void Switch_Breathe_Page(void);
    void Switch_Quick_Page(void);
    void Switch_Sound_Page(void);
    void Switch_Time_Setting_Page(void);

    void UpdateMpu6050Display();
    void UpdateSportDisplay();

    void StartStopwatch();
    void PauseStopwatch();
    void ResetStopwatch();
    void UpdateStopwatch();
    void SetStopwatchButtonHighlight(int state);

    void SetAlarmTime(int hours, int minutes, int seconds);
    void StartAlarm();
    void StopAlarm();
    void CheckAlarm();
    void SetAlarmButtonHighlight(int state);
    void SetAlarmSetState(int state);
    void SetAlarmMusic(const std::string& music_path);
    const std::string& GetAlarmMusic() const { return alarm_music_path_; }
    bool IsAlarmEnabled() const { return alarm_enabled_; }
    int GetAlarmHours() const { return alarm_hours_; }
    int GetAlarmMinutes() const { return alarm_minutes_; }
    int GetAlarmSeconds() const { return alarm_seconds_; }
    void AdjustAlarmMusic(int delta);

    void Return_Switch_Page(void);
    void FunPagePrev(void);
    void FunPageNext(void);
    void FunPageEnter(void);
    void QuickPageAdjust(int delta);
    void QuickPageToggleSelection(void);
    void GameMove(int delta);
    void GameToggle(void);
    void GameReset(void);
    void ReflexTap(void);
    void MemoryTap(int lane);
    void BreatheToggle(void);
    void BreatheAdjust(int delta);
    void SoundPagePrev(void);
    void SoundPageNext(void);
    void SoundPagePlay(void);
    void SoundPageToggleAuto(void);
    void StopSoundPreviewPlayback();
    void StartAlarmPlayback();
    void UpdateAboutStats();

    void TimeSettingAdjust(int delta);
    void TimeSettingNextField(void);
    void TimeSettingApply(void);

    virtual void SetTime(const char* time_str);
    virtual void SetDate(const char* date_str);
    virtual void SetWeather(void);
    virtual void SetStatus(const char* status);
    virtual void ShowNotification(const char* notification, int duration_ms = 3000);
    virtual void ShowNotification(const std::string& notification, int duration_ms = 3000);
    virtual void SetEmotion(const char* emotion);
    virtual void SetChatMessage(const char* role, const char* content);
    virtual void SetFlashlightText(const char* text);
    virtual void SetIcon(const char* icon);
    virtual void SetPreviewImage(const lv_img_dsc_t* image);
    virtual void SetTheme(const std::string& theme_name);
    virtual std::string GetTheme() { return current_theme_name_; }
    virtual void UpdateStatusBar(bool update_all = false);
    virtual void ShowStandbyScreen(bool show);
    virtual void ShowBootScreen();
    virtual void next(void);
    virtual void prev(void);
    virtual void OnSwitchPageShown() {}  // 当切换到菜单页时调用，派生类可重写

    OledDisplayPage GetCurrentPage() const;
    OledDisplaySwitch GetCurrentIndex() const;
    inline int width() const { return width_; }
    inline int height() const { return height_; }

protected:
    const char* week_days[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    int width_ = 0;
    int height_ = 0;

    Select_Page pages[Switch_Count];
    OledDisplayPage current_page_ = PAGE_INIT;
    OledDisplayPage dialogue_return_page_ = PAGE_SWITCH;
    OledDisplayPage quick_return_page_ = PAGE_SWITCH;
    OledDisplaySwitch current_index_ = Switch_MAIN;
    OledDisplaySwitch previous_index_ = Switch_MAIN;
    esp_pm_lock_handle_t pm_lock_ = nullptr;
    lv_display_t* display_ = nullptr;

    lv_obj_t* spo2_value_ = nullptr;
    lv_obj_t* heart_value_ = nullptr;
    lv_obj_t* emotion_label_ = nullptr;
    lv_obj_t* emotion_label_image = nullptr;
    lv_obj_t* network_label_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
    lv_obj_t* notification_label_ = nullptr;
    lv_obj_t* mute_label_ = nullptr;
    lv_obj_t* battery_label_ = nullptr;
    lv_obj_t* chat_message_label_ = nullptr;
    lv_obj_t* low_battery_popup_ = nullptr;
    lv_obj_t* low_battery_label_ = nullptr;
    lv_obj_t* weather_icon_ = nullptr;
    lv_obj_t* temp_text_ = nullptr;
    lv_obj_t* city_text_ = nullptr;
    lv_obj_t* ring_icon_ = nullptr;
    lv_obj_t* naozhong_hour_label_ = nullptr;
    lv_obj_t* naozhong_minute_label_ = nullptr;
    lv_obj_t* naozhong_second_label_ = nullptr;
    int alarm_set_state_ = 0;
    int alarm_music_index_ = 0;
    lv_obj_t* temperature_label_ = nullptr;

    lv_obj_t* weather_container_ = nullptr;

    lv_obj_t* main_screen_ = nullptr;
    lv_obj_t* dialogue_screen_ = nullptr;
    lv_obj_t* health_screen_ = nullptr;
    lv_obj_t* weather_screen_ = nullptr;
    lv_obj_t* about_screen_ = nullptr;
    lv_obj_t* about_stats_label_ = nullptr;
    lv_obj_t* reconfig_screen_ = nullptr;
    lv_obj_t* flashlight_screen_ = nullptr;
    lv_obj_t* flashlight_text_ = nullptr;
    lv_obj_t* miaobiao_screen_ = nullptr;
    lv_obj_t* miaobiao_time_label_ = nullptr;
    lv_obj_t* miaobiao_container_ = nullptr;
    lv_obj_t* miaobiao_start_btn_ = nullptr;
    lv_obj_t* miaobiao_stop_btn_ = nullptr;
    lv_obj_t* miaobiao_reset_btn_ = nullptr;
    lv_obj_t* naozhong_screen_ = nullptr;
    lv_obj_t* naozhong_time_label_ = nullptr;
    lv_obj_t* naozhong_start_btn_ = nullptr;
    lv_obj_t* naozhong_stop_btn_ = nullptr;
    lv_obj_t* naozhong_set_btn_ = nullptr;
    lv_obj_t* naozhong_music_card_ = nullptr;
    lv_obj_t* naozhong_music_label_ = nullptr;

    lv_obj_t* mpu6050_screen_ = nullptr;
    lv_obj_t* mpu6050_pitch_label_ = nullptr;
    lv_obj_t* mpu6050_roll_label_ = nullptr;
    lv_obj_t* mpu6050_yaw_label_ = nullptr;
    esp_timer_handle_t mpu6050_update_timer_ = nullptr;

    lv_obj_t* sport_screen_ = nullptr;
    lv_obj_t* sport_steps_label_ = nullptr;
    lv_obj_t* sport_calories_label_ = nullptr;
    lv_obj_t* sport_distance_label_ = nullptr;
    esp_timer_handle_t sport_update_timer_ = nullptr;

    lv_obj_t* fun_screen_ = nullptr;
    lv_obj_t* fun_card_ = nullptr;
    lv_obj_t* fun_icon_label_ = nullptr;
    lv_obj_t* fun_title_label_ = nullptr;
    lv_obj_t* fun_hint_label_ = nullptr;
    lv_obj_t* fun_indicator_label_ = nullptr;
    int fun_selected_index_ = 0;

    lv_obj_t* quick_screen_ = nullptr;
    lv_obj_t* quick_brightness_card_ = nullptr;
    lv_obj_t* quick_volume_card_ = nullptr;
    lv_obj_t* quick_brightness_value_ = nullptr;
    lv_obj_t* quick_volume_value_ = nullptr;
    lv_obj_t* quick_brightness_bar_track_ = nullptr;
    lv_obj_t* quick_brightness_bar_fill_ = nullptr;
    lv_obj_t* quick_volume_bar_track_ = nullptr;
    lv_obj_t* quick_volume_bar_fill_ = nullptr;
    int quick_selected_index_ = 0;

    lv_obj_t* game_screen_ = nullptr;
    lv_obj_t* game_field_ = nullptr;
    lv_obj_t* game_player_ = nullptr;
    lv_obj_t* game_obstacle_ = nullptr;
    lv_obj_t* game_status_card_ = nullptr;
    lv_obj_t* game_score_label_ = nullptr;
    lv_obj_t* game_status_label_ = nullptr;
    esp_timer_handle_t game_timer_ = nullptr;
    bool game_running_ = false;
    bool game_over_ = false;
    int game_player_lane_ = 1;
    int game_obstacle_lane_ = 0;
    int game_obstacle_y_ = 2;
    int game_score_ = 0;
    int game_best_score_ = 0;

    lv_obj_t* reflex_screen_ = nullptr;
    lv_obj_t* reflex_status_card_ = nullptr;
    lv_obj_t* reflex_status_label_ = nullptr;
    lv_obj_t* reflex_value_label_ = nullptr;
    lv_obj_t* reflex_hint_label_ = nullptr;
    lv_obj_t* reflex_best_label_ = nullptr;
    esp_timer_handle_t reflex_timer_ = nullptr;
    int reflex_state_ = 0;
    int reflex_last_ms_ = -1;
    int reflex_best_ms_ = 0;
    int64_t reflex_go_time_us_ = 0;

    lv_obj_t* memory_screen_ = nullptr;
    lv_obj_t* memory_board_card_ = nullptr;
    lv_obj_t* memory_lane_blocks_[3] = {nullptr, nullptr, nullptr};
    lv_obj_t* memory_lane_labels_[3] = {nullptr, nullptr, nullptr};
    lv_obj_t* memory_status_card_ = nullptr;
    lv_obj_t* memory_status_label_ = nullptr;
    lv_obj_t* memory_score_label_ = nullptr;
    lv_obj_t* memory_hint_label_ = nullptr;
    esp_timer_handle_t memory_timer_ = nullptr;
    int memory_state_ = 0;
    int memory_sequence_[12] = {0};
    int memory_sequence_length_ = 0;
    int memory_show_index_ = 0;
    int memory_input_index_ = 0;
    int memory_best_ = 0;
    int memory_active_lane_ = -1;
    bool memory_flash_on_ = false;
    bool memory_input_flash_pending_ = false;

    lv_obj_t* breathe_screen_ = nullptr;
    lv_obj_t* breathe_phase_card_ = nullptr;
    lv_obj_t* breathe_phase_label_ = nullptr;
    lv_obj_t* breathe_cycle_label_ = nullptr;
    lv_obj_t* breathe_status_label_ = nullptr;
    lv_obj_t* breathe_bar_track_ = nullptr;
    lv_obj_t* breathe_bar_fill_ = nullptr;
    esp_timer_handle_t breathe_timer_ = nullptr;
    bool breathe_running_ = false;
    int breathe_phase_ = 0;
    int breathe_phase_tick_ = 0;
    int breathe_cycles_ = 0;
    int breathe_pace_seconds_ = 4;

    lv_obj_t* sound_screen_ = nullptr;
    lv_obj_t* sound_name_label_ = nullptr;
    lv_obj_t* sound_desc_label_ = nullptr;
    lv_obj_t* sound_indicator_label_ = nullptr;
    lv_obj_t* sound_mode_label_ = nullptr;
    lv_obj_t* sound_state_label_ = nullptr;
    lv_obj_t* sound_position_track_ = nullptr;
    lv_obj_t* sound_position_fill_ = nullptr;
    esp_timer_handle_t sound_timer_ = nullptr;
    TaskHandle_t sound_preview_task_handle_ = nullptr;
    int sound_selected_index_ = 0;
    int sound_requested_index_ = -1;
    bool sound_stop_requested_ = false;
    bool sound_autoplay_ = false;
    bool sound_preview_playing_ = false;
    int sound_idle_ticks_ = 0;

    lv_obj_t* time_setting_screen_ = nullptr;
    lv_obj_t* time_setting_year_label_ = nullptr;
    lv_obj_t* time_setting_month_label_ = nullptr;
    lv_obj_t* time_setting_day_label_ = nullptr;
    lv_obj_t* time_setting_hour_label_ = nullptr;
    lv_obj_t* time_setting_minute_label_ = nullptr;
    lv_obj_t* time_setting_second_label_ = nullptr;
    lv_obj_t* time_setting_weekday_label_ = nullptr;
    int time_setting_field_ = 0;  // 0=年 1=月 2=日 3=时 4=分 5=秒
    int time_setting_year_ = 2026;
    int time_setting_month_ = 1;
    int time_setting_day_ = 1;
    int time_setting_hour_ = 12;
    int time_setting_minute_ = 0;
    int time_setting_second_ = 0;

    esp_timer_handle_t stopwatch_timer_ = nullptr;
    bool stopwatch_running_ = false;
    uint64_t stopwatch_time_ = 0;

    bool alarm_enabled_ = false;
    int alarm_hours_ = 0;
    int alarm_minutes_ = 0;
    int alarm_seconds_ = 0;
    esp_timer_handle_t alarm_check_timer_ = nullptr;
    std::string alarm_music_path_;
    bool alarm_triggered_today_ = false;
    esp_timer_handle_t alarm_timer_ = nullptr;
    bool alarm_running_ = false;
    uint64_t alarm_time_ = 0;

    lv_obj_t* container_ = nullptr;
    lv_obj_t* init_ = nullptr;
    lv_obj_t* side_bar_ = nullptr;

    lv_obj_t* home_ = nullptr;
    lv_obj_t* dialogue_ = nullptr;
    lv_obj_t* page_time_label_ = nullptr;
    lv_obj_t* time_label_ = nullptr;
    lv_obj_t* time_ = nullptr;
    lv_obj_t* main_state_label_ = nullptr;
    lv_obj_t* main_weather_label_ = nullptr;
    lv_obj_t* main_temperature_label_ = nullptr;
    lv_obj_t* main_steps_label_ = nullptr;
    lv_obj_t* main_alarm_label_ = nullptr;

    lv_obj_t* date_label_ = nullptr;
    lv_obj_t* date_ = nullptr;

    lv_obj_t* weather_label_ = nullptr;
    lv_obj_t* weather_ = nullptr;

    lv_obj_t* page_network_label_ = nullptr;

    const char* battery_icon_ = nullptr;
    const char* network_icon_ = nullptr;
    bool muted_ = false;
    std::string current_theme_name_;

    std::chrono::system_clock::time_point last_status_update_time_;
    esp_timer_handle_t notification_timer_ = nullptr;

    // 离线模式时间管理
    bool offline_mode_ = true;
    time_t offline_time_base_ = 0;  // 离线模式基准时间
    uint64_t offline_time_start_us_ = 0;  // 离线模式开始时的微秒时间戳

    // 启动动画
    lv_obj_t* boot_screen_ = nullptr;
    lv_obj_t* boot_label_ = nullptr;
    esp_timer_handle_t boot_timer_ = nullptr;

    friend class DisplayLockGuard;
    virtual bool Lock(int timeout_ms = 0) = 0;
    virtual void Unlock() = 0;

    DisplayFonts fonts_;

    void UpdateFunPageUi();
    void UpdateQuickPageUi();
    void UpdateGamePageUi();
    void UpdateReflexPageUi();
    void UpdateMemoryPageUi();
    void UpdateBreathePageUi();
    void UpdateSoundPageUi();
    void EnsureGameTimer();
    void StopGameTimer();
    void AdvanceGameTick();
    void SaveGameBestScore();
    void EnsureReflexTimer();
    void StopReflexTimer();
    void TriggerReflexReady();
    void SaveReflexBest();
    void EnsureMemoryTimer();
    void StopMemoryTimer();
    void AdvanceMemoryTick();
    void StartMemoryRound();
    void SaveMemoryBest();
    void EnsureBreatheTimer();
    void StopBreatheTimer();
    void AdvanceBreatheTick();
    void UpdateAlarmMusicUi();
    void EnsureSoundTimer();
    void StopSoundTimer();
    void AdvanceSoundTick();
    void StartSoundPreviewPlayback(int index);
    void EnsureSoundPreviewTask();
    void SoundPreviewTask();
};

class DisplayLockGuard {
public:
    explicit DisplayLockGuard(Display* display) : display_(display) {
        if (!display_->Lock(30000)) {
            ESP_LOGE("Display", "Failed to lock display");
        }
    }

    ~DisplayLockGuard() {
        display_->Unlock();
    }

private:
    Display* display_;
};

class NoDisplay : public Display {
private:
    bool Lock(int timeout_ms = 0) override {
        (void)timeout_ms;
        return true;
    }

    void Unlock() override {}
};

#endif
