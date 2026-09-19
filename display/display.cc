#include <esp_log.h>
#include <esp_err.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <algorithm>
#include <string>
#include <cstdlib>
#include <cstring>

#include "display.h"
#include "board.h"
#include "app/application.h"
#include "system/system_info.h"
#include "font_awesome_symbols.h"
#include "audio_codec.h"
#include "settings/settings.h"
#include "assets/lang_config.h"
#include "sensors/mpu6050/mpu6050.h"
#include "protocol.h"
#include "weather_icon.cc"
LV_IMG_DECLARE(weather_icon)
#define TAG "Display"

// 声音预览数据定义（依赖 Lang::Sounds，放在 .cc 中）
const SoundPreview kSoundPreviews[] = {
    {"音乐1", "歌曲选择", Lang::Sounds::P3_0},
    {"音乐2", "歌曲选择", Lang::Sounds::P3_1},
    {"音乐3", "歌曲选择", Lang::Sounds::P3_2},
    {"音乐4", "歌曲选择", Lang::Sounds::P3_3},
    {"音乐5", "歌曲选择", Lang::Sounds::P3_4},
    {"音乐6", "歌曲选择", Lang::Sounds::P3_5},
    {"音乐7", "歌曲选择", Lang::Sounds::P3_6},
    {"音乐8", "歌曲选择", Lang::Sounds::P3_7},
    {"音乐9", "歌曲选择", Lang::Sounds::P3_8},
    {"音乐10", "歌曲选择", Lang::Sounds::P3_9},
};

const int kSoundPreviewCount = static_cast<int>(sizeof(kSoundPreviews) / sizeof(kSoundPreviews[0]));

Display::Display() {
    Settings settings("fun", false);
    game_best_score_ = settings.GetInt("best_score", 0);
    reflex_best_ms_ = settings.GetInt("reflex_best_ms", 0);
    memory_best_ = settings.GetInt("memory_best", 0);
    breathe_pace_seconds_ = ClampValue(settings.GetInt("breathe_pace", 4), 3, 6);
    alarm_music_index_ = ClampValue(settings.GetInt("alarm_music", 0), 0, kSoundPreviewCount - 1);
    alarm_music_path_ = std::string(kSoundPreviews[alarm_music_index_].name);

    // 初始化离线模式时间：2026年1月1日 12:00:00 星期一
    struct tm default_time = {0};
    default_time.tm_year = 2026 - 1900;  // 年份从1900开始
    default_time.tm_mon = 0;              // 1月（0-11）
    default_time.tm_mday = 1;             // 1号
    default_time.tm_hour = 12;            // 12点
    default_time.tm_min = 0;              // 0分
    default_time.tm_sec = 0;              // 0秒
    default_time.tm_wday = 1;             // 星期一
    offline_time_base_ = mktime(&default_time);
    offline_time_start_us_ = esp_timer_get_time();
    offline_mode_ = true;

    // Notification timer
    esp_timer_create_args_t notification_timer_args = {
            .callback = [](void *arg) {
            Display *display = static_cast<Display*>(arg);
            DisplayLockGuard lock(display);
            lv_obj_add_flag(display->notification_label_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(display->status_label_, LV_OBJ_FLAG_HIDDEN);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "notification_timer",
        .skip_unhandled_events = false,
    };
    ESP_ERROR_CHECK(esp_timer_create(&notification_timer_args, &notification_timer_));

    // Create a power management lock
    auto ret = esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, "display_update", &pm_lock_);
    if (ret == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(TAG, "Power management not supported");
    } else {
        ESP_ERROR_CHECK(ret);
    }
}

Display::~Display() {
    if (boot_timer_ != nullptr) {
        esp_timer_stop(boot_timer_);
        esp_timer_delete(boot_timer_);
        boot_timer_ = nullptr;
    }

    StopGameTimer();
    if (game_timer_ != nullptr) {
        esp_timer_delete(game_timer_);
        game_timer_ = nullptr;
    }

    StopReflexTimer();
    if (reflex_timer_ != nullptr) {
        esp_timer_delete(reflex_timer_);
        reflex_timer_ = nullptr;
    }

    StopMemoryTimer();
    if (memory_timer_ != nullptr) {
        esp_timer_delete(memory_timer_);
        memory_timer_ = nullptr;
    }

    StopBreatheTimer();
    if (breathe_timer_ != nullptr) {
        esp_timer_delete(breathe_timer_);
        breathe_timer_ = nullptr;
    }

    StopSoundTimer();
    if (sound_timer_ != nullptr) {
        esp_timer_delete(sound_timer_);
        sound_timer_ = nullptr;
    }

    if (notification_timer_ != nullptr) {
        esp_timer_stop(notification_timer_);
        esp_timer_delete(notification_timer_);
    }

    if (network_label_ != nullptr) {
        lv_obj_del(network_label_);
        lv_obj_del(notification_label_);
        lv_obj_del(status_label_);
        lv_obj_del(mute_label_);
        lv_obj_del(battery_label_);
        lv_obj_del(emotion_label_);
    }
    if( low_battery_popup_ != nullptr ) {
        lv_obj_del(low_battery_popup_);
    }
    if (pm_lock_ != nullptr) {
        esp_pm_lock_delete(pm_lock_);
    }
}

// 实现获取当前页面的函数
OledDisplayPage Display::GetCurrentPage() const {
    return current_page_;  // 返回存储的当前页面
}

OledDisplaySwitch Display::GetCurrentIndex() const {
    return current_index_;  // 返回存储页面的索引
}

void Display::SetTime(const char* time_str)
{
    DisplayLockGuard lock(this);
    if (page_time_label_ == nullptr) {
        return;
    }
    if (strcmp(lv_label_get_text(page_time_label_), time_str) != 0) 
    {
        lv_label_set_text(page_time_label_, time_str);
    }
}

void Display::next(void)
{
   
}

void Display::prev(void)
{
   
}
void Display::SetStatus(const char* status) {
    DisplayLockGuard lock(this);
    if (status_label_ == nullptr) {
        return;
    }
    lv_label_set_text(status_label_, status);
    lv_obj_clear_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    last_status_update_time_ = std::chrono::system_clock::now();
}


void Display::ShowNotification(const std::string &notification, int duration_ms) {
    ShowNotification(notification.c_str(), duration_ms);
}

void Display::ShowNotification(const char* notification, int duration_ms) {
    DisplayLockGuard lock(this);
    if (notification_label_ == nullptr) {
        return;
    }
    lv_label_set_text(notification_label_, notification);
    lv_obj_clear_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);

    esp_timer_stop(notification_timer_);
    ESP_ERROR_CHECK(esp_timer_start_once(notification_timer_, duration_ms * 1000));
}

void Display::TimeSettingAdjust(int delta)
{
    DisplayLockGuard lock(this);
    if (current_page_ != PAGE_TIME_SETTING) {
        return;
    }

    ESP_LOGI(TAG, "TimeSettingAdjust: field=%d, delta=%d", time_setting_field_, delta);
    ESP_LOGI(TAG, "Before adjust: Y=%d M=%d D=%d H=%d M=%d S=%d",
        time_setting_year_, time_setting_month_, time_setting_day_,
        time_setting_hour_, time_setting_minute_, time_setting_second_);

    switch (time_setting_field_) {
        case 0: // 年
            time_setting_year_ = ClampValue(time_setting_year_ + delta, 2020, 2099);
            ESP_LOGI(TAG, "Adjusted year to: %d", time_setting_year_);
            break;
        case 1: // 月
            time_setting_month_ = ClampValue(time_setting_month_ + delta, 1, 12);
            ESP_LOGI(TAG, "Adjusted month to: %d", time_setting_month_);
            break;
        case 2: // 日
            {
                int max_day = 31;
                if (time_setting_month_ == 2) {
                    bool is_leap = (time_setting_year_ % 4 == 0 && time_setting_year_ % 100 != 0) || (time_setting_year_ % 400 == 0);
                    max_day = is_leap ? 29 : 28;
                } else if (time_setting_month_ == 4 || time_setting_month_ == 6 || time_setting_month_ == 9 || time_setting_month_ == 11) {
                    max_day = 30;
                }
                time_setting_day_ = ClampValue(time_setting_day_ + delta, 1, max_day);
                ESP_LOGI(TAG, "Adjusted day to: %d (max: %d)", time_setting_day_, max_day);
            }
            break;
        case 3: // 时
            time_setting_hour_ = (time_setting_hour_ + delta + 24) % 24;
            ESP_LOGI(TAG, "Adjusted hour to: %d", time_setting_hour_);
            break;
        case 4: // 分
            time_setting_minute_ = (time_setting_minute_ + delta + 60) % 60;
            ESP_LOGI(TAG, "Adjusted minute to: %d", time_setting_minute_);
            break;
        case 5: // 秒
            time_setting_second_ = (time_setting_second_ + delta + 60) % 60;
            ESP_LOGI(TAG, "Adjusted second to: %d", time_setting_second_);
            break;
        default:
            ESP_LOGW(TAG, "Invalid time_setting_field_: %d", time_setting_field_);
            return;
    }

    ESP_LOGI(TAG, "After adjust: Y=%d M=%d D=%d H=%d M=%d S=%d",
        time_setting_year_, time_setting_month_, time_setting_day_,
        time_setting_hour_, time_setting_minute_, time_setting_second_);

    // 用数组批量重置所有标签样式，减少 LVGL API 调用序列
    lv_obj_t* time_labels[6] = {
        time_setting_year_label_, time_setting_month_label_, time_setting_day_label_,
        time_setting_hour_label_, time_setting_minute_label_, time_setting_second_label_
    };
    for (int i = 0; i < 6; i++) {
        if (time_labels[i] == nullptr) continue;
        bool active = (i == time_setting_field_);
        lv_obj_set_style_border_width(time_labels[i], active ? 2 : 1, 0);
        lv_obj_set_style_border_color(time_labels[i], lv_color_black(), 0);
        lv_obj_set_style_bg_color(time_labels[i], active ? lv_color_make(200, 200, 200) : lv_color_white(), 0);
    }

    // 更新各字段值
    static constexpr int kBufSize = 8;
    char buf[kBufSize];
    if (time_setting_year_label_ != nullptr) {
        snprintf(buf, kBufSize, "%04d", time_setting_year_);
        lv_label_set_text(time_setting_year_label_, buf);
    }
    if (time_setting_month_label_ != nullptr) {
        snprintf(buf, kBufSize, "%02d", time_setting_month_);
        lv_label_set_text(time_setting_month_label_, buf);
    }
    if (time_setting_day_label_ != nullptr) {
        snprintf(buf, kBufSize, "%02d", time_setting_day_);
        lv_label_set_text(time_setting_day_label_, buf);
    }
    if (time_setting_hour_label_ != nullptr) {
        snprintf(buf, kBufSize, "%02d", time_setting_hour_);
        lv_label_set_text(time_setting_hour_label_, buf);
    }
    if (time_setting_minute_label_ != nullptr) {
        snprintf(buf, kBufSize, "%02d", time_setting_minute_);
        lv_label_set_text(time_setting_minute_label_, buf);
    }
    if (time_setting_second_label_ != nullptr) {
        snprintf(buf, kBufSize, "%02d", time_setting_second_);
        lv_label_set_text(time_setting_second_label_, buf);
    }

    // 更新星期显示
    if (time_setting_weekday_label_ != nullptr) {
        struct tm tm_temp = {0};
        tm_temp.tm_year = time_setting_year_ - 1900;
        tm_temp.tm_mon = time_setting_month_ - 1;
        tm_temp.tm_mday = time_setting_day_;
        mktime(&tm_temp);

        static const char* const kWeekDayNames[7] = {
            "星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"
        };
        lv_label_set_text(time_setting_weekday_label_, kWeekDayNames[tm_temp.tm_wday]);
    }

    // 显示当前设置的字段名称
    const char* field_names[6] = {"年", "月", "日", "时", "分", "秒"};
    char notification[32];
    snprintf(notification, sizeof(notification), "设置%s", field_names[time_setting_field_]);
    ShowNotification(notification, 1000);
}

void Display::TimeSettingNextField(void)
{
    bool should_exit = false;

    {
        DisplayLockGuard lock(this);
        if (current_page_ != PAGE_TIME_SETTING) {
            return;
        }

        ESP_LOGI(TAG, "TimeSettingNextField: current field=%d", time_setting_field_);

        // 如果当前在秒字段（字段5），下一次切换应该保存并退出
        if (time_setting_field_ == 5) {
            ESP_LOGI(TAG, "Reached last field (seconds), applying settings");

            // 应用设置的时间
            struct tm new_time = {0};
            new_time.tm_year = time_setting_year_ - 1900;
            new_time.tm_mon = time_setting_month_ - 1;
            new_time.tm_mday = time_setting_day_;
            new_time.tm_hour = time_setting_hour_;
            new_time.tm_min = time_setting_minute_;
            new_time.tm_sec = time_setting_second_;

            time_t new_time_t = mktime(&new_time);

            // 更新离线时间基准
            offline_time_base_ = new_time_t;
            offline_time_start_us_ = esp_timer_get_time();

            ESP_LOGI(TAG, "Time set to: %04d-%02d-%02d %02d:%02d:%02d",
                time_setting_year_, time_setting_month_, time_setting_day_,
                time_setting_hour_, time_setting_minute_, time_setting_second_);

            // 隐藏时间设置页面
            if (time_setting_screen_ != nullptr) {
                lv_obj_add_flag(time_setting_screen_, LV_OBJ_FLAG_HIDDEN);
            }

            should_exit = true;
        } else {
            time_setting_field_ = time_setting_field_ + 1;
            ESP_LOGI(TAG, "TimeSettingNextField: new field=%d", time_setting_field_);

            TimeSettingAdjust(0);  // 刷新UI高亮
        }
    }

    // 在锁外调用 Return_Switch_Page
    if (should_exit) {
        Return_Switch_Page();
        // 更新主页的时间和日期显示
        UpdateTime();
        UpdateDate();
    }
}

void Display::TimeSettingApply(void)
{
    DisplayLockGuard lock(this);
    if (current_page_ != PAGE_TIME_SETTING) {
        return;
    }

    // 应用设置的时间
    struct tm new_time = {0};
    new_time.tm_year = time_setting_year_ - 1900;
    new_time.tm_mon = time_setting_month_ - 1;
    new_time.tm_mday = time_setting_day_;
    new_time.tm_hour = time_setting_hour_;
    new_time.tm_min = time_setting_minute_;
    new_time.tm_sec = time_setting_second_;

    time_t new_time_t = mktime(&new_time);

    // 更新离线时间基准
    offline_time_base_ = new_time_t;
    offline_time_start_us_ = esp_timer_get_time();

    ESP_LOGI(TAG, "Time set to: %04d-%02d-%02d %02d:%02d:%02d",
        time_setting_year_, time_setting_month_, time_setting_day_,
        time_setting_hour_, time_setting_minute_, time_setting_second_);

    // 返回菜单页
    Return_Switch_Page();
}


// Sound functions → display_sound.cc

void Display::UpdateMpu6050Display()
{
    DisplayLockGuard lock(this);
    if (current_page_ != PAGE_MPU6050) return;

    MPU6050& mpu = MPU6050::GetInstance();
    const mpu6050_angle_t& angle = mpu.GetAngle();

    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f", angle.pitch);
    lv_label_set_text(mpu6050_pitch_label_, buf);

    snprintf(buf, sizeof(buf), "%.1f", angle.roll);
    lv_label_set_text(mpu6050_roll_label_, buf);

    snprintf(buf, sizeof(buf), "%.1f", angle.yaw);
    lv_label_set_text(mpu6050_yaw_label_, buf);
}

void Display::UpdateSportDisplay()
{
    if (main_steps_label_ != nullptr) {
        const mpu6050_sport_t& sport = MPU6050::GetInstance().GetSportData();
        char steps_summary[16];
        FormatCompactSteps(steps_summary, sizeof(steps_summary), (unsigned long)sport.steps);

        DisplayLockGuard summary_lock(this);
        lv_label_set_text(main_steps_label_, steps_summary);
    }

    DisplayLockGuard lock(this);
    if (current_page_ != PAGE_SPORT) return;

    MPU6050& mpu = MPU6050::GetInstance();
    const mpu6050_sport_t& sport = mpu.GetSportData();

    char buf[32];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)sport.steps);
    lv_label_set_text(sport_steps_label_, buf);

    snprintf(buf, sizeof(buf), "%.1f", sport.calories);
    lv_label_set_text(sport_calories_label_, buf);

    snprintf(buf, sizeof(buf), "%.1f", sport.distance_m);
    lv_label_set_text(sport_distance_label_, buf);
}

void Display::UpdateTime(void)
{
    time_t now;
    struct tm* tm;

    if (offline_mode_) {
        // 离线模式：使用离线时间基准 + 经过的时间
        uint64_t elapsed_us = esp_timer_get_time() - offline_time_start_us_;
        now = offline_time_base_ + (elapsed_us / 1000000);
        tm = localtime(&now);
    } else {
        // 在线模式：使用系统时间
        now = time(NULL);
        tm = localtime(&now);
        // 检查系统时间是否已设置
        if (tm->tm_year < 2025 - 1900) {
            ESP_LOGW(TAG, "System time is not set, tm_year: %d", tm->tm_year);
            return;
        }
    }

    char time_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm);
    SetTime(time_str);

    if (main_steps_label_ != nullptr) {
        const mpu6050_sport_t& sport = MPU6050::GetInstance().GetSportData();
        char steps_summary[16];
        FormatCompactSteps(steps_summary, sizeof(steps_summary), (unsigned long)sport.steps);

        DisplayLockGuard lock(this);
        lv_label_set_text(main_steps_label_, steps_summary);
    }
}

void Display::SetDate(const char* date_str)
{
    DisplayLockGuard lock(this);
    if (date_label_ == nullptr) {
        return;
    }
    lv_label_set_text(date_label_, date_str);
}



void Display::UpdateDate(void)
{
    time_t now;
    struct tm* tm;

    if (offline_mode_) {
        // 离线模式：使用离线时间基准 + 经过的时间
        uint64_t elapsed_us = esp_timer_get_time() - offline_time_start_us_;
        now = offline_time_base_ + (elapsed_us / 1000000);
        tm = localtime(&now);
    } else {
        // 在线模式：使用系统时间
        now = time(NULL);
        tm = localtime(&now);
        // 检查系统时间是否已设置（仅在在线模式下检查）
        if (tm->tm_year < 2025 - 1900) {
            ESP_LOGW(TAG, "System time is not set, tm_year: %d", tm->tm_year);
            return;
        }
    }

    static const char* const kWeekDayShort[7] = {
        reinterpret_cast<const char*>(u8"\u65e5"),
        reinterpret_cast<const char*>(u8"\u4e00"),
        reinterpret_cast<const char*>(u8"\u4e8c"),
        reinterpret_cast<const char*>(u8"\u4e09"),
        reinterpret_cast<const char*>(u8"\u56db"),
        reinterpret_cast<const char*>(u8"\u4e94"),
        reinterpret_cast<const char*>(u8"\u516d"),
    };
    char date_str[32];
    snprintf(date_str, sizeof(date_str), "%02d-%02d %s",
        tm->tm_mon + 1, tm->tm_mday, kWeekDayShort[tm->tm_wday]);
    SetDate(date_str);
}

void Display::SetOnlineMode(bool online)
{
    if (online && offline_mode_) {
        // 从离线模式切换到在线模式
        offline_mode_ = false;
        ESP_LOGI(TAG, "Switched to online mode");
    } else if (!online && !offline_mode_) {
        // 从在线模式切换到离线模式
        offline_mode_ = true;
        // 重置离线时间基准为当前系统时间
        time_t now = time(NULL);
        struct tm* tm = localtime(&now);
        if (tm->tm_year >= 2025 - 1900) {
            offline_time_base_ = now;
        } else {
            // 如果系统时间无效，使用默认时间
            struct tm default_time = {0};
            default_time.tm_year = 2026 - 1900;
            default_time.tm_mon = 0;
            default_time.tm_mday = 1;
            default_time.tm_hour = 12;
            default_time.tm_min = 0;
            default_time.tm_sec = 0;
            default_time.tm_wday = 1;
            offline_time_base_ = mktime(&default_time);
        }
        offline_time_start_us_ = esp_timer_get_time();
        ESP_LOGI(TAG, "Switched to offline mode");
    }
}

void Display::SetWeather(void)
{
    DisplayLockGuard lock(this);
    auto& board = Board::GetInstance();
    auto weather = board.GetWeather();
    char weather_data[128] = {0};
    memset(weather_data, 0, sizeof(weather_data));
    snprintf(weather_data, sizeof(weather_data)-1, "%s %s",
                        weather->Get_City_Value(),
                        weather->Get_Weather_Value());
    if (city_text_ == nullptr)
        return ;
    lv_label_set_text(city_text_, weather_data);
    if (main_weather_label_ != nullptr) {
        lv_label_set_text(main_weather_label_, weather->Get_Weather_Value());
    }

    memset(weather_data, 0, sizeof(weather_data));
    snprintf(weather_data, sizeof(weather_data)-1, "%s℃",
                        weather->Get_Temperature_Value());
    snprintf(weather_data, sizeof(weather_data) - 1, "%s℃",
                        weather->Get_Temperature_Value());
    if (temp_text_ == nullptr)
        return;
    lv_label_set_text(temp_text_, weather_data);
    if (main_temperature_label_ != nullptr) {
        lv_label_set_text(main_temperature_label_, weather_data);
    }

    if (weather_icon_ == nullptr)
        return;
    uint8_t code = weather->Get_Code();
    switch (code)
    {
        case 0:
        case 1:
        case 2:
        case 3:
            lv_img_set_src(weather_icon_, &qingtian);           /* 晴天 */
            break;
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
            lv_img_set_src(weather_icon_, &duoyun);             /* 多云 */
            break;
        case 9:
            lv_img_set_src(weather_icon_, &yintian);            /* 阴天 */
            break;
        case 10:
        case 11:
        case 12:
            lv_img_set_src(weather_icon_, &leizhenyu);           /* 阵雨 */
            break;
        case 13:
            lv_img_set_src(weather_icon_, &xiaoyu);              /* 小雨 */
            break;
        case 14:
            lv_img_set_src(weather_icon_, &zhongyu);             /* 中雨 */
            break;
        case 15:
            lv_img_set_src(weather_icon_, &dayu);                /* 大雨 */
            break;
        case 16:
        case 17:
        case 18:
            lv_img_set_src(weather_icon_, &dabaoyu);                /* 暴雨 */
            break;
        case 19:
        case 20:
            lv_img_set_src(weather_icon_, &yujiaxue);               /* 雨夹雪 */
            break;
        case 21:
        case 22:
        case 23:
        case 24:
        case 25:
            lv_img_set_src(weather_icon_, &xue);                    /* 下雪 */
            break;
        case 30:
            lv_img_set_src(weather_icon_, &wu);                    /* 雾 */
            break;
        case 31:
            lv_img_set_src(weather_icon_, &mai);                    /* 霾 */
            break;
        default:
            break;
    }
        
}

void Display::UpdateWeather(void)
{
    auto& board = Board::GetInstance();
    auto weather = board.GetWeather();
    if (weather->Get_Weather() == true)
        SetWeather();
}

void Display::UpDateNetWork(void)
{
    DisplayLockGuard lock(this);

    const char* icon = nullptr;
    auto& board = Board::GetInstance();
    icon = board.GetNetworkStateIcon();
    if (page_network_label_ != nullptr && icon != nullptr && network_icon_ != icon) {
        DisplayLockGuard lock(this);
        network_icon_ = icon;
        lv_label_set_text(page_network_label_, network_icon_);
    }
}
void Display::UpDateHealth(void)
{
    DisplayLockGuard lock(this);
    if ((heart_value_ == NULL) || (spo2_value_ == NULL))
        return;
    auto max30102 = Board::GetInstance().GetMAX30102();
    lv_label_set_text_fmt(heart_value_, "%d", max30102->Max30102_Get_Heart());
    lv_label_set_text_fmt(spo2_value_, "%d", max30102->Max30102_Get_SpO2());
    ESP_LOGI(TAG, "Heart: %d, SpO2: %d", max30102->Max30102_Get_Heart(), max30102->Max30102_Get_SpO2());
}


void Display::UpDateBattery(void)
{
    esp_pm_lock_acquire(pm_lock_);
    
    if (battery_label_ == nullptr) 
        return;
    const char* icon = nullptr;
    auto Battery = Board::GetInstance().GetBatteryMonitor();
    int battery_level = Battery->get_battery_percent();
    const char* levels[] = {
                FONT_AWESOME_BATTERY_EMPTY, // 0-19%
                FONT_AWESOME_BATTERY_1,    // 20-39%
                FONT_AWESOME_BATTERY_2,    // 40-59%
                FONT_AWESOME_BATTERY_3,    // 60-79%
                FONT_AWESOME_BATTERY_FULL, // 80-99%
                FONT_AWESOME_BATTERY_FULL, // 100%
            };
    icon = levels[battery_level / 20];
    
    // printf("Percent: %d%%\n",battery_level);
    DisplayLockGuard lock(this);
    lv_label_set_text(battery_label_, icon);  // 或具体电量图标：""（满电）、""（75%）等
    // lv_label_set_text_fmt(battery_label_, "%d%%", battery_level);
    esp_pm_lock_release(pm_lock_);
}



void Display::UpdateStatusBar(bool update_all) {
    (void)update_all;
    auto codec = Board::GetInstance().GetAudioCodec();

    {
        DisplayLockGuard lock(this);
        const bool muted_now = codec != nullptr && codec->output_volume() == 0;
        muted_ = muted_now;

        if (mute_label_ != nullptr) {
            if (muted_now) {
                lv_label_set_text(mute_label_, "");
                lv_obj_add_flag(mute_label_, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_label_set_text(mute_label_, "");
                lv_obj_add_flag(mute_label_, LV_OBJ_FLAG_HIDDEN);
            }
        }

        if (ring_icon_ != nullptr) {
            if (muted_now) {
                lv_label_set_text(ring_icon_, FONT_AWESOME_VOLUME_MUTE);
                lv_obj_clear_flag(ring_icon_, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_label_set_text(ring_icon_, FONT_AWESOME_BELL);
                lv_obj_clear_flag(ring_icon_, LV_OBJ_FLAG_HIDDEN);
            }
        }

        /* Home state updates suppressed per user request */
    }

    if (current_page_ == PAGE_CHAT &&
        last_status_update_time_ + std::chrono::seconds(10) < std::chrono::system_clock::now()) {
        time_t now = time(NULL);
        struct tm* tm = localtime(&now);
        if (tm->tm_year >= 2025 - 1900) {
            char time_str[16];
            strftime(time_str, sizeof(time_str), "%H:%M  ", tm);
            SetStatus(time_str);
        } else {
            ESP_LOGW(TAG, "System time is not set, tm_year: %d", tm->tm_year);
        }
    }
}

void Display::SetEmotion(const char* emotion) {
    struct Emotion {
        const char* icon;
        const char* text;
    };

    static const std::vector<Emotion> emotions = {
        {FONT_AWESOME_EMOJI_NEUTRAL, "neutral"},
        {FONT_AWESOME_EMOJI_HAPPY, "happy"},
        {FONT_AWESOME_EMOJI_LAUGHING, "laughing"},
        {FONT_AWESOME_EMOJI_FUNNY, "funny"},
        {FONT_AWESOME_EMOJI_SAD, "sad"},
        {FONT_AWESOME_EMOJI_ANGRY, "angry"},
        {FONT_AWESOME_EMOJI_CRYING, "crying"},
        {FONT_AWESOME_EMOJI_LOVING, "loving"},
        {FONT_AWESOME_EMOJI_EMBARRASSED, "embarrassed"},
        {FONT_AWESOME_EMOJI_SURPRISED, "surprised"},
        {FONT_AWESOME_EMOJI_SHOCKED, "shocked"},
        {FONT_AWESOME_EMOJI_THINKING, "thinking"},
        {FONT_AWESOME_EMOJI_WINKING, "winking"},
        {FONT_AWESOME_EMOJI_COOL, "cool"},
        {FONT_AWESOME_EMOJI_RELAXED, "relaxed"},
        {FONT_AWESOME_EMOJI_DELICIOUS, "delicious"},
        {FONT_AWESOME_EMOJI_KISSY, "kissy"},
        {FONT_AWESOME_EMOJI_CONFIDENT, "confident"},
        {FONT_AWESOME_EMOJI_SLEEPY, "sleepy"},
        {FONT_AWESOME_EMOJI_SILLY, "silly"},
        {FONT_AWESOME_EMOJI_CONFUSED, "confused"}
    };
    
    // 查找匹配的表情
    std::string_view emotion_view(emotion);
    auto it = std::find_if(emotions.begin(), emotions.end(),
        [&emotion_view](const Emotion& e) { return e.text == emotion_view; });
    
    DisplayLockGuard lock(this);
    if (emotion_label_ == nullptr) {
        return;
    }

    // 如果找到匹配的表情就显示对应图标，否则显示默认的neutral表情
    if (it != emotions.end()) {
        lv_label_set_text(emotion_label_, it->icon);
    } else {
        lv_label_set_text(emotion_label_, FONT_AWESOME_EMOJI_NEUTRAL);
    }
}

void Display::SetIcon(const char* icon) {
    DisplayLockGuard lock(this);
    if (emotion_label_ == nullptr) {
        return;
    }
    lv_label_set_text(emotion_label_, icon);
}

void Display::SetPreviewImage(const lv_img_dsc_t* image) {
    // Do nothing
}

void Display::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (chat_message_label_ == nullptr) {
        return;
    }
    lv_label_set_text(chat_message_label_, content);
}

void Display::SetFlashlightText(const char* text) {
    (void)text;
}

void Display::SetTheme(const std::string& theme_name) {
    current_theme_name_ = theme_name;
    Settings settings("display", true);
    settings.SetString("theme", theme_name);
}

void Display::UpdateAboutStats() {
    if (about_stats_label_ == nullptr) return;

    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t total_heap = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    int used_percent = (int)((total_heap - free_heap) * 100 / total_heap);

    int rssi = 0;
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;
    }

    char buf[128];
    snprintf(buf, sizeof(buf),
        "RAM:%d/%dKB(%d%%)  Up:%llds  WiFi:%ddBm",
        (int)(free_heap / 1024), (int)(total_heap / 1024), used_percent,
        SystemInfo::GetBootElapsedMs() / 1000, rssi);
    lv_label_set_text(about_stats_label_, buf);
}

void Display::ShowStandbyScreen(bool show) {
    if (show) {
        SetChatMessage("system", "");
        SetEmotion("sleepy");
    } else {
        SetChatMessage("system", "");
        SetEmotion("neutral");
    }
}

void Display::ShowBootScreen() {
    DisplayLockGuard lock(this);

    // 隐藏所有菜单页
    for (int i = 0; i < Switch_Count; i++) {
        if (pages[i].page != nullptr) {
            lv_obj_add_flag(pages[i].page, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // 设置当前页面为启动页面
    current_page_ = PAGE_BOOT;

    // 显示启动屏幕并移到最前面
    if (boot_screen_ != nullptr) {
        lv_obj_clear_flag(boot_screen_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(boot_screen_);

        // 淡入动画
        if (boot_label_ != nullptr) {
            lv_obj_set_style_opa(boot_label_, LV_OPA_TRANSP, 0);
            lv_anim_t anim;
            lv_anim_init(&anim);
            lv_anim_set_var(&anim, boot_label_);
            lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
            lv_anim_set_time(&anim, 1200);
            lv_anim_set_exec_cb(&anim, [](void* v, int32_t val) {
                lv_obj_set_style_opa((lv_obj_t*)v, val, 0);
            });
            lv_anim_start(&anim);
        }
    }

    // 创建定时器，2秒后切换到主页
    if (boot_timer_ == nullptr) {
        esp_timer_create_args_t boot_timer_args = {
            .callback = [](void* arg) {
                Display* display = static_cast<Display*>(arg);
                DisplayLockGuard lock(display);

                // 隐藏启动屏幕
                if (display->boot_screen_ != nullptr) {
                    lv_obj_add_flag(display->boot_screen_, LV_OBJ_FLAG_HIDDEN);
                }

                // 切换到主页
                display->Switch_Main_Page();
                // 注意：不在此处 delete 定时器（ESP-IDF 不允许回调内删除）
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "boot_timer"
        };
        esp_timer_create(&boot_timer_args, &boot_timer_);
    }

    // 启动定时器，2000ms后触发
    esp_timer_start_once(boot_timer_, 10000000);
}

void Display::StartStopwatch() {
    if (!stopwatch_running_) {
        stopwatch_running_ = true;
        // 创建定时器回调函数
        esp_timer_create_args_t stopwatch_timer_args = {
            .callback = [](void* arg) {
                Display* display = static_cast<Display*>(arg);
                display->UpdateStopwatch();
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "stopwatch_timer"
        };
        ESP_ERROR_CHECK(esp_timer_create(&stopwatch_timer_args, &stopwatch_timer_));
        // 启动定时器，每100毫秒更新一次
        ESP_ERROR_CHECK(esp_timer_start_periodic(stopwatch_timer_, 100000));
        
        SetStopwatchButtonHighlight(1); // 开始状态，高亮开始按钮
    }
}

void Display::PauseStopwatch() {
    if (stopwatch_running_) {
        stopwatch_running_ = false;
        if (stopwatch_timer_ != nullptr) {
            esp_timer_stop(stopwatch_timer_);
            esp_timer_delete(stopwatch_timer_);
            stopwatch_timer_ = nullptr;
        }
        
        SetStopwatchButtonHighlight(2); // 暂停状态，高亮暂停按钮
    }
}

void Display::ResetStopwatch() {
    PauseStopwatch();
    stopwatch_time_ = 0;
    if (miaobiao_time_label_ != nullptr) {
        DisplayLockGuard lock(this);
        lv_label_set_text(miaobiao_time_label_, "00:00:00");
    }
    SetStopwatchButtonHighlight(0); // 重置状态，高亮重置按钮
}

void Display::SetStopwatchButtonHighlight(int state) {
    DisplayLockGuard lock(this);
    
    // 重置所有按钮的样式
    if (miaobiao_start_btn_) {
        lv_obj_set_style_border_width(miaobiao_start_btn_, 0, 0);
        lv_obj_set_style_bg_color(miaobiao_start_btn_, lv_color_white(), 0);
    }
    if (miaobiao_stop_btn_) {
        lv_obj_set_style_border_width(miaobiao_stop_btn_, 0, 0);
        lv_obj_set_style_bg_color(miaobiao_stop_btn_, lv_color_white(), 0);
    }
    if (miaobiao_reset_btn_) {
        lv_obj_set_style_border_width(miaobiao_reset_btn_, 0, 0);
        lv_obj_set_style_bg_color(miaobiao_reset_btn_, lv_color_white(), 0);
    }
    
    // 根据状态设置高亮
    switch (state) {
        case 1: // 开始状态
            if (miaobiao_start_btn_) {
                lv_obj_set_style_border_width(miaobiao_start_btn_, 2, 0);
                lv_obj_set_style_border_color(miaobiao_start_btn_, lv_color_black(), 0);
                lv_obj_set_style_bg_color(miaobiao_start_btn_, lv_color_make(192, 192, 192), 0); // 灰色
            }
            break;
        case 2: // 暂停状态
            if (miaobiao_stop_btn_) {
                lv_obj_set_style_border_width(miaobiao_stop_btn_, 2, 0);
                lv_obj_set_style_border_color(miaobiao_stop_btn_, lv_color_black(), 0);
                lv_obj_set_style_bg_color(miaobiao_stop_btn_, lv_color_make(192, 192, 192), 0); // 灰色
            }
            break;
        case 0: // 重置状态
            if (miaobiao_reset_btn_) {
                lv_obj_set_style_border_width(miaobiao_reset_btn_, 2, 0);
                lv_obj_set_style_border_color(miaobiao_reset_btn_, lv_color_black(), 0);
                lv_obj_set_style_bg_color(miaobiao_reset_btn_, lv_color_make(192, 192, 192), 0); // 灰色
            }
            break;
    }
}

void Display::UpdateStopwatch() {
    if (stopwatch_running_) {
        stopwatch_time_ += 100; // 每次增加100毫秒
        if (miaobiao_time_label_ != nullptr) {
            DisplayLockGuard lock(this);
            char time_str[9];
            int hours = stopwatch_time_ / 3600000;
            int minutes = (stopwatch_time_ % 3600000) / 60000;
            int seconds = (stopwatch_time_ % 60000) / 1000;
            snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", hours, minutes, seconds);
            lv_label_set_text(miaobiao_time_label_, time_str);
        }
    }
}

void Display::SetAlarmTime(int hours, int minutes, int seconds) {
    alarm_hours_ = hours;
    alarm_minutes_ = minutes;
    alarm_seconds_ = seconds;
    
    // 更新主时间标签
    if (naozhong_time_label_ != nullptr) {
        DisplayLockGuard lock(this);
        char time_str[9];
        snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", hours, minutes, seconds);
        lv_label_set_text(naozhong_time_label_, time_str);
    }
    
    // 更新拆分的时间标签
    if (naozhong_hour_label_ != nullptr) {
        DisplayLockGuard lock(this);
        char hour_str[3];
        snprintf(hour_str, sizeof(hour_str), "%02d", hours);
        lv_label_set_text(naozhong_hour_label_, hour_str);
    }
    
    if (naozhong_minute_label_ != nullptr) {
        DisplayLockGuard lock(this);
        char minute_str[3];
        snprintf(minute_str, sizeof(minute_str), "%02d", minutes);
        lv_label_set_text(naozhong_minute_label_, minute_str);
    }
    
    if (naozhong_second_label_ != nullptr) {
        DisplayLockGuard lock(this);
        char second_str[3];
        snprintf(second_str, sizeof(second_str), "%02d", seconds);
        lv_label_set_text(naozhong_second_label_, second_str);
    }

    if (main_alarm_label_ != nullptr) {
        DisplayLockGuard lock(this);
        char alarm_str[8];
        snprintf(alarm_str, sizeof(alarm_str), "%02d:%02d", hours, minutes);
        lv_label_set_text(main_alarm_label_, alarm_str);
    }

    UpdateAlarmMusicUi();
}

void Display::SetAlarmMusic(const std::string& music_path) {
    alarm_music_path_ = music_path;
    for (int i = 0; i < kSoundPreviewCount; ++i) {
        if (alarm_music_path_ == kSoundPreviews[i].name) {
            alarm_music_index_ = i;
            break;
        }
    }

    Settings settings("fun", true);
    settings.SetInt("alarm_music", alarm_music_index_);
}

void Display::AdjustAlarmMusic(int delta) {
    alarm_music_index_ = (alarm_music_index_ + delta + kSoundPreviewCount) % kSoundPreviewCount;
    alarm_music_path_ = kSoundPreviews[alarm_music_index_].name;

    Settings settings("fun", true);
    settings.SetInt("alarm_music", alarm_music_index_);

    UpdateAlarmMusicUi();
    ShowNotification(std::string("铃声: ") + alarm_music_path_, 1200);
    StartSoundPreviewPlayback(alarm_music_index_);
}

void Display::UpdateAlarmMusicUi() {
    if (naozhong_music_label_ == nullptr) {
        return;
    }
    DisplayLockGuard lock(this);
    lv_label_set_text(naozhong_music_label_, alarm_music_path_.c_str());
}

void Display::SetAlarmSetState(int state) {
    alarm_set_state_ = state;
    DisplayLockGuard lock(this);

    if (naozhong_hour_label_) {
        lv_obj_set_style_border_width(naozhong_hour_label_, 1, 0);
        lv_obj_set_style_border_color(naozhong_hour_label_, lv_color_black(), 0);
        lv_obj_set_style_bg_color(naozhong_hour_label_, lv_color_white(), 0);
    }
    if (naozhong_minute_label_) {
        lv_obj_set_style_border_width(naozhong_minute_label_, 1, 0);
        lv_obj_set_style_border_color(naozhong_minute_label_, lv_color_black(), 0);
        lv_obj_set_style_bg_color(naozhong_minute_label_, lv_color_white(), 0);
    }
    if (naozhong_second_label_) {
        lv_obj_set_style_border_width(naozhong_second_label_, 1, 0);
        lv_obj_set_style_border_color(naozhong_second_label_, lv_color_black(), 0);
        lv_obj_set_style_bg_color(naozhong_second_label_, lv_color_white(), 0);
    }
    if (naozhong_music_card_) {
        lv_obj_set_style_border_width(naozhong_music_card_, 1, 0);
        lv_obj_set_style_border_color(naozhong_music_card_, lv_color_black(), 0);
        lv_obj_set_style_bg_color(naozhong_music_card_, lv_color_white(), 0);
    }

    if (state == 0 && naozhong_hour_label_) {
        lv_obj_set_style_border_width(naozhong_hour_label_, 2, 0);
        lv_obj_set_style_border_color(naozhong_hour_label_, lv_color_black(), 0);
        lv_obj_set_style_bg_color(naozhong_hour_label_, lv_color_make(192, 192, 192), 0);
    } else if (state == 1 && naozhong_minute_label_) {
        lv_obj_set_style_border_width(naozhong_minute_label_, 2, 0);
        lv_obj_set_style_border_color(naozhong_minute_label_, lv_color_black(), 0);
        lv_obj_set_style_bg_color(naozhong_minute_label_, lv_color_make(192, 192, 192), 0);
    } else if (state == 2 && naozhong_second_label_) {
        lv_obj_set_style_border_width(naozhong_second_label_, 2, 0);
        lv_obj_set_style_border_color(naozhong_second_label_, lv_color_black(), 0);
        lv_obj_set_style_bg_color(naozhong_second_label_, lv_color_make(192, 192, 192), 0);
    } else if (state == 3 && naozhong_music_card_) {
        lv_obj_set_style_border_width(naozhong_music_card_, 2, 0);
        lv_obj_set_style_border_color(naozhong_music_card_, lv_color_black(), 0);
        lv_obj_set_style_bg_color(naozhong_music_card_, lv_color_make(192, 192, 192), 0);
    }
}

void Display::StartAlarm() {
    if (!alarm_enabled_) {
        alarm_enabled_ = true;
        // 闹钟检查已合并到 Application::OnClockTimer，不再需要独立定时器
        SetAlarmButtonHighlight(1);
    }

    if (main_alarm_label_ != nullptr) {
        DisplayLockGuard lock(this);
        char alarm_str[8];
        snprintf(alarm_str, sizeof(alarm_str), "%02d:%02d", alarm_hours_, alarm_minutes_);
        lv_label_set_text(main_alarm_label_, alarm_str);
    }
}

void Display::StopAlarm() {
    if (alarm_enabled_) {
        alarm_enabled_ = false;
        alarm_triggered_today_ = false;
        SetAlarmButtonHighlight(2);
    }

    if (main_alarm_label_ != nullptr) {
        DisplayLockGuard lock(this);
        lv_label_set_text(main_alarm_label_, "关闭");
    }
}

void Display::CheckAlarm() {
    if (!alarm_enabled_) return;  // 闹钟未启用时快速返回

    time_t now = time(NULL);
    struct tm* tm = localtime(&now);

    // 检查时间是否匹配
    if (tm->tm_hour == alarm_hours_ && tm->tm_min == alarm_minutes_ && tm->tm_sec == alarm_seconds_) {
        // 只触发一次，防止重复触发
        if (alarm_triggered_today_) {
            return;
        }
        alarm_triggered_today_ = true;
        
        // 触发闹钟
        ESP_LOGI(TAG, "Alarm triggered!");
        
        // 确保在正确的页面
        if (GetCurrentPage() != PAGE_NAOZHONG) {
            // 如果当前不在闹钟页面，切换到闹钟页面
            Switch_Naozhong_Page();
        }
        
        // 异步播放当前选择的闹钟铃声，避免同步塞队列阻塞交互
        StartAlarmPlayback();
        
        // 显示闹钟通知
        ShowNotification("闹钟响了", 5000);
        
        // 确保通知标签可见
        DisplayLockGuard lock(this);
        if (notification_label_ != nullptr) {
            lv_obj_clear_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void Display::SetAlarmButtonHighlight(int state) {
    DisplayLockGuard lock(this);
    
    // 重置所有按钮的样式
    if (naozhong_set_btn_) {
        lv_obj_set_style_border_width(naozhong_set_btn_, 0, 0);
        lv_obj_set_style_bg_color(naozhong_set_btn_, lv_color_white(), 0);
    }
    if (naozhong_start_btn_) {
        lv_obj_set_style_border_width(naozhong_start_btn_, 0, 0);
        lv_obj_set_style_bg_color(naozhong_start_btn_, lv_color_white(), 0);
    }
    if (naozhong_stop_btn_) {
        lv_obj_set_style_border_width(naozhong_stop_btn_, 0, 0);
        lv_obj_set_style_bg_color(naozhong_stop_btn_, lv_color_white(), 0);
    }
    
    // 根据状态设置高亮
    switch (state) {
        case 0: // 设置状态
            if (naozhong_set_btn_) {
                lv_obj_set_style_border_width(naozhong_set_btn_, 2, 0);
                lv_obj_set_style_border_color(naozhong_set_btn_, lv_color_black(), 0);
                lv_obj_set_style_bg_color(naozhong_set_btn_, lv_color_make(192, 192, 192), 0); // 灰色
            }
            break;
        case 1: // 开启状态
            if (naozhong_start_btn_) {
                lv_obj_set_style_border_width(naozhong_start_btn_, 2, 0);
                lv_obj_set_style_border_color(naozhong_start_btn_, lv_color_black(), 0);
                lv_obj_set_style_bg_color(naozhong_start_btn_, lv_color_make(192, 192, 192), 0); // 灰色
            }
            break;
        case 2: // 关闭状态
            if (naozhong_stop_btn_) {
                lv_obj_set_style_border_width(naozhong_stop_btn_, 2, 0);
                lv_obj_set_style_border_color(naozhong_stop_btn_, lv_color_black(), 0);
                lv_obj_set_style_bg_color(naozhong_stop_btn_, lv_color_make(192, 192, 192), 0); // 灰色
            }
            break;
    }
}
