#include "oled_display.h"
#include "font_awesome_symbols.h"
#include "assets/lang_config.h"

#include <string>
#include <algorithm>

#include <esp_app_desc.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_lvgl_port.h>

#include "images/image.h"
#define TAG "OledDisplay"
LV_FONT_DECLARE(font_awesome_30_1);
LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_puhui_16_4);

static lv_coord_t GetFontLineHeight(const lv_font_t* font, lv_coord_t fallback = 16) {
    return font != nullptr ? font->line_height : fallback;
}

static lv_coord_t GetHeaderHeight(const DisplayFonts& fonts) {
    return std::max<lv_coord_t>(18, GetFontLineHeight(fonts.text_font, 16));
}

static lv_coord_t GetPageMargin() {
    return 3;
}

static const char* const kSwitchHints[Switch_Count] = {
    "主页信息",
    "AI对话",
    "心率血氧",
    "实时天气",
    "手电颜色",
    "秒表计时",
    "闹钟设置",
    "姿态角度",
    "步数热量",
    "歌曲选择",
    "亮度音量",
    "游戏休闲",
    "重新配网",
    "设备信息",
};

static void SetRootStyle(lv_obj_t* obj) {
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

static void SetPanelStyle(lv_obj_t* obj, bool filled = false, lv_coord_t radius = 8, lv_coord_t border_width = 1) {
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, border_width, 0);
    lv_obj_set_style_border_color(obj, lv_color_black(), 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, filled ? lv_color_black() : lv_color_white(), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

static void SetSolidBlockStyle(lv_obj_t* obj, lv_coord_t radius = 2) {
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

static lv_obj_t* CreateCompactButton(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
    lv_coord_t width, lv_coord_t height, const char* text, const lv_font_t* font) {
    lv_obj_t* button = lv_btn_create(parent);
    SetPanelStyle(button, false, 6, 1);
    lv_obj_set_size(button, width, height);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_pad_all(button, 0, 0);

    lv_obj_t* label = lv_label_create(button);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

static lv_obj_t* CreateHeaderBar(lv_obj_t* parent, const char* icon, const char* title, const DisplayFonts& fonts) {
    lv_obj_t* header = lv_obj_create(parent);
    SetPanelStyle(header, true, 0, 0);
    lv_obj_set_size(header, lv_obj_get_width(parent), GetHeaderHeight(fonts));
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);

    if (icon != nullptr && icon[0] != '\0') {
        lv_obj_t* icon_label = lv_label_create(header);
        lv_obj_set_style_text_font(icon_label, fonts.icon_font, 0);
        lv_obj_set_style_text_color(icon_label, lv_color_white(), 0);
        lv_label_set_text(icon_label, icon);
        lv_obj_align(icon_label, LV_ALIGN_LEFT_MID, 4, 0);
    }

    lv_obj_t* title_label = lv_label_create(header);
    lv_obj_set_style_text_font(title_label, fonts.text_font, 0);
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    lv_label_set_text(title_label, title);
    lv_obj_center(title_label);
    return header;
}

OledDisplay::OledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
    int width, int height, bool mirror_x, bool mirror_y, DisplayFonts fonts)
    : panel_io_(panel_io), panel_(panel), fonts_(fonts) {
    width_ = width;
    height_ = height;
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 3;  // 从 1 提升到 3，防止被低优先级任务长期阻塞导致卡顿
    port_cfg.timer_period_ms = 50;
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding OLED display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * height_),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = true,
        .rotation = {
            .swap_xy = false,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
        .flags = {
            .buff_dma = 1,
            .buff_spiram = 0,
            .sw_rotate = 0,
            .full_refresh = 0,
            .direct_mode = 0,
        },
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    if (height_ == 64) {
        SetupUI_128x64();
    } else {
        SetupUI_128x32();
    }
}

OledDisplay::~OledDisplay() {
    if (content_ != nullptr) {
        lv_obj_del(content_);
    }
    if (status_bar_ != nullptr) {
        lv_obj_del(status_bar_);
    }
    if (side_bar_ != nullptr) {
        lv_obj_del(side_bar_);
    }
    if (dialogue_ != nullptr) {
        lv_obj_del(dialogue_);
    }
    if (main_screen_ != nullptr) {
        lv_obj_del(main_screen_);
    }
    if (health_screen_ != nullptr) {
        lv_obj_del(health_screen_);
    }
    if (weather_screen_ != nullptr) {
        lv_obj_del(weather_screen_);
    }
    if (about_screen_ != nullptr) {
        lv_obj_del(about_screen_);
    }
    if (fun_screen_ != nullptr) {
        lv_obj_del(fun_screen_);
    }
    if (quick_screen_ != nullptr) {
        lv_obj_del(quick_screen_);
    }
    if (game_screen_ != nullptr) {
        lv_obj_del(game_screen_);
    }
    if (reflex_screen_ != nullptr) {
        lv_obj_del(reflex_screen_);
    }
    if (memory_screen_ != nullptr) {
        lv_obj_del(memory_screen_);
    }
    if (breathe_screen_ != nullptr) {
        lv_obj_del(breathe_screen_);
    }
    if (sound_screen_ != nullptr) {
        lv_obj_del(sound_screen_);
    }
    if (time_setting_screen_ != nullptr) {
        lv_obj_del(time_setting_screen_);
    }

    if (panel_ != nullptr) {
        esp_lcd_panel_del(panel_);
    }
    if (panel_io_ != nullptr) {
        esp_lcd_panel_io_del(panel_io_);
    }
    lvgl_port_deinit();
}

bool OledDisplay::Lock(int timeout_ms) {
    return lvgl_port_lock(timeout_ms);
}
void OledDisplay::Unlock() {
    lvgl_port_unlock();
}

void OledDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (chat_message_label_ == nullptr) {
        return;
    }
    lv_label_set_text(chat_message_label_, content);
    lv_obj_clear_flag(content_right_, LV_OBJ_FLAG_HIDDEN);
}

// 动画状态标志
static bool is_animating = false;

void OledDisplay::slide_to(OledDisplaySwitch new_index) {
    if (new_index == current_index_) {
        return;
    }

    // 计算滑动方向（环形处理）
    int direction = (new_index > current_index_) ? 1 : -1;
    if (abs(new_index - current_index_) > Switch_Count / 2) {
        direction = -direction;
    }

    // 先更新布局到新状态
    current_index_ = new_index;
    UpdateSwitchPagePosition();

    // 简单的视觉反馈：所有卡片从偏移位置弹回目标位置
    const int bounce_offset = direction * 30;
    for (int i = 0; i < Switch_Count; i++) {
        lv_obj_t* item = pages[i].page;
        if (item == nullptr) continue;

        int pos = (i - current_index_ + Switch_Count) % Switch_Count;
        // 只对可见卡片做动画
        if (pos <= 2 || pos >= Switch_Count - 2) {
            int target_x = lv_obj_get_x(item);
            lv_obj_set_x(item, target_x + bounce_offset);

            lv_anim_t anim;
            lv_anim_init(&anim);
            lv_anim_set_var(&anim, item);
            lv_anim_set_values(&anim, target_x + bounce_offset, target_x);
            lv_anim_set_duration(&anim, 200);
            lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)lv_obj_set_x);
            lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
            lv_anim_start(&anim);
        }
    }
}

void OledDisplay::next(void) {
    // 防止动画过程中重复触发
    if (is_animating) {
        return;
    }
    
    DisplayLockGuard lock(this); 
    current_page_ = PAGE_SWITCH;
    OledDisplaySwitch new_index = (OledDisplaySwitch)((current_index_ + 1) % Switch_Count);
    slide_to(new_index);
}

void OledDisplay::prev(void) {
    // 防止动画过程中重复触发
    if (is_animating) {
        return;
    }

    DisplayLockGuard lock(this);
    current_page_ = PAGE_SWITCH;
    OledDisplaySwitch new_index = (OledDisplaySwitch)((current_index_ - 1 + Switch_Count) % Switch_Count);
    slide_to(new_index);
}

void OledDisplay::OnSwitchPageShown() {
    DisplayLockGuard lock(this);
    // 显示菜单页的 header 和 hint label
    if (switch_header_ != nullptr) {
        lv_obj_clear_flag(switch_header_, LV_OBJ_FLAG_HIDDEN);
    }
    if (switch_hint_label_ != nullptr) {
        lv_obj_clear_flag(switch_hint_label_, LV_OBJ_FLAG_HIDDEN);
    }

    // 显示所有菜单页图标
    for (int i = 0; i < Switch_Count; i++) {
        if (pages[i].page != nullptr) {
            lv_obj_clear_flag(pages[i].page, LV_OBJ_FLAG_HIDDEN);
        }
    }
}


void OledDisplay::Health_Check_Page_Init(lv_obj_t * screen) {
    const lv_coord_t margin = 2;
    const lv_coord_t gap = 2;
    const lv_coord_t card_width = width_ - margin * 2;
    const lv_coord_t card_height = (height_ - margin * 2 - gap) / 2;

    health_screen_ = lv_obj_create(screen);
    SetRootStyle(health_screen_);
    lv_obj_set_size(health_screen_, width_, height_);
    lv_obj_add_flag(health_screen_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* heart_card = lv_obj_create(health_screen_);
    SetPanelStyle(heart_card, false, 8, 1);
    lv_obj_set_size(heart_card, card_width, card_height);
    lv_obj_set_pos(heart_card, margin, margin);

    lv_obj_t* heart_tag = lv_label_create(heart_card);
    lv_obj_set_style_text_font(heart_tag, fonts_.text_font, 0);
    lv_label_set_text(heart_tag, "心率");
    lv_obj_align(heart_tag, LV_ALIGN_LEFT_MID, 5, 0);

    heart_value_ = lv_label_create(heart_card);
    lv_obj_set_width(heart_value_, 52);
    lv_obj_set_style_text_font(heart_value_, &font_puhui_16_4, 0);
    lv_obj_set_style_text_align(heart_value_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(heart_value_, "0");
    lv_obj_align(heart_value_, LV_ALIGN_RIGHT_MID, -5, 0);

    lv_obj_t* spo2_card = lv_obj_create(health_screen_);
    SetPanelStyle(spo2_card, false, 8, 1);
    lv_obj_set_size(spo2_card, card_width, card_height);
    lv_obj_set_pos(spo2_card, margin, margin + card_height + gap);

    lv_obj_t* spo2_tag = lv_label_create(spo2_card);
    lv_obj_set_style_text_font(spo2_tag, fonts_.text_font, 0);
    lv_label_set_text(spo2_tag, "血氧 %");
    lv_obj_align(spo2_tag, LV_ALIGN_LEFT_MID, 5, 0);

    spo2_value_ = lv_label_create(spo2_card);
    lv_obj_set_width(spo2_value_, 52);
    lv_obj_set_style_text_font(spo2_value_, &font_puhui_16_4, 0);
    lv_obj_set_style_text_align(spo2_value_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(spo2_value_, "0");
    lv_obj_align(spo2_value_, LV_ALIGN_RIGHT_MID, -5, 0);
}

void OledDisplay::Main_Page_Init(lv_obj_t * screen) {
    const lv_coord_t margin = 2;
    const lv_coord_t gap = 2;
    const lv_coord_t card_width = width_ - margin * 2;
    const lv_coord_t info_top = margin;
    const lv_coord_t info_height = 14;
    const lv_coord_t time_top = info_top + info_height + gap;
    const lv_coord_t time_height = 26;
    const lv_coord_t metric_top = time_top + time_height + gap;
    const lv_coord_t metric_height = height_ - metric_top - margin;
    const lv_coord_t metric_gap = 2;
    const lv_coord_t metric_width = (card_width - metric_gap) / 2;

    main_screen_ = lv_obj_create(screen);
    SetRootStyle(main_screen_);
    lv_obj_set_size(main_screen_, width_, height_);
    lv_obj_add_flag(main_screen_, LV_OBJ_FLAG_HIDDEN);

    home_ = lv_obj_create(main_screen_);
    SetRootStyle(home_);
    lv_obj_set_size(home_, width_, height_);

    lv_obj_t* info_strip = lv_obj_create(main_screen_);
    SetPanelStyle(info_strip, false, 7, 1);
    lv_obj_set_size(info_strip, card_width, info_height);
    lv_obj_set_pos(info_strip, margin, info_top);

    date_label_ = lv_label_create(info_strip);
    lv_obj_set_width(date_label_, 54);
    lv_label_set_long_mode(date_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(date_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_color(date_label_, lv_color_black(), 0);
    lv_obj_set_style_text_align(date_label_, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(date_label_, "--/--");
    lv_obj_align(date_label_, LV_ALIGN_LEFT_MID, 5, 0);

    ring_icon_ = lv_label_create(info_strip);
    lv_obj_set_style_text_font(ring_icon_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(ring_icon_, lv_color_black(), 0);
    lv_label_set_text(ring_icon_, FONT_AWESOME_BELL);
    lv_obj_align(ring_icon_, LV_ALIGN_RIGHT_MID, -43, 0);

    page_network_label_ = lv_label_create(info_strip);
    lv_obj_set_style_text_font(page_network_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(page_network_label_, lv_color_black(), 0);
    lv_label_set_text(page_network_label_, "");
    lv_obj_align(page_network_label_, LV_ALIGN_RIGHT_MID, -23, 0);

    battery_label_ = lv_label_create(info_strip);
    lv_obj_set_style_text_font(battery_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, lv_color_black(), 0);
    lv_label_set_text(battery_label_, "");
    lv_obj_align(battery_label_, LV_ALIGN_RIGHT_MID, -5, 0);

    time_ = lv_obj_create(main_screen_);
    SetPanelStyle(time_, false, 8, 1);
    lv_obj_set_size(time_, card_width, time_height);
    lv_obj_set_pos(time_, margin, time_top);

    page_time_label_ = lv_label_create(time_);
    lv_obj_set_width(page_time_label_, card_width - 38);
    lv_label_set_long_mode(page_time_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(page_time_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(page_time_label_, &font_puhui_20_4, 0);
    lv_obj_set_style_text_color(page_time_label_, lv_color_black(), 0);
    lv_label_set_text(page_time_label_, "--:--");
    lv_obj_align(page_time_label_, LV_ALIGN_LEFT_MID, 4, 1);

    main_temperature_label_ = lv_label_create(time_);
    lv_obj_set_width(main_temperature_label_, 30);
    lv_label_set_long_mode(main_temperature_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(main_temperature_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_color(main_temperature_label_, lv_color_black(), 0);
    lv_obj_set_style_text_align(main_temperature_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(main_temperature_label_, "--℃");
    lv_obj_align(main_temperature_label_, LV_ALIGN_TOP_RIGHT, -4, 1);

    main_state_label_ = lv_label_create(time_);
    lv_obj_set_width(main_state_label_, 28);
    lv_label_set_long_mode(main_state_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(main_state_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_color(main_state_label_, lv_color_black(), 0);
    lv_obj_set_style_text_align(main_state_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(main_state_label_, "");
    lv_obj_align(main_state_label_, LV_ALIGN_BOTTOM_RIGHT, -4, -1);
    lv_obj_add_flag(main_state_label_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* steps_card = lv_obj_create(main_screen_);
    SetPanelStyle(steps_card, false, 7, 1);
    lv_obj_set_size(steps_card, metric_width, metric_height);
    lv_obj_set_pos(steps_card, margin, metric_top);

    lv_obj_t* left_icon = lv_label_create(steps_card);
    lv_obj_set_style_text_font(left_icon, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(left_icon, lv_color_black(), 0);
    lv_label_set_text(left_icon, FONT_AWESOME_LOCATION_ARROW);
    lv_obj_align(left_icon, LV_ALIGN_LEFT_MID, 4, 0);

    main_steps_label_ = lv_label_create(steps_card);
    lv_obj_set_width(main_steps_label_, 36);
    lv_label_set_long_mode(main_steps_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(main_steps_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(main_steps_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(main_steps_label_, "0");
    lv_obj_align(main_steps_label_, LV_ALIGN_RIGHT_MID, -4, 0);

    lv_obj_t* alarm_card = lv_obj_create(main_screen_);
    SetPanelStyle(alarm_card, false, 7, 1);
    lv_obj_set_size(alarm_card, metric_width, metric_height);
    lv_obj_set_pos(alarm_card, margin + metric_width + metric_gap, metric_top);

    lv_obj_t* right_icon = lv_label_create(alarm_card);
    lv_obj_set_style_text_font(right_icon, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(right_icon, lv_color_black(), 0);
    lv_label_set_text(right_icon, FONT_AWESOME_BELL);
    lv_obj_align(right_icon, LV_ALIGN_LEFT_MID, 4, 0);

    main_alarm_label_ = lv_label_create(alarm_card);
    lv_obj_set_width(main_alarm_label_, 38);
    lv_label_set_long_mode(main_alarm_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(main_alarm_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(main_alarm_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(main_alarm_label_, "关闭");
    lv_obj_align(main_alarm_label_, LV_ALIGN_RIGHT_MID, -4, 0);
}
void OledDisplay::Weather_Page_Init(lv_obj_t * screen) {
    const lv_coord_t margin = 2;
    const lv_coord_t gap = 2;
    const lv_coord_t card_width = width_ - margin * 2;
    const lv_coord_t summary_height = 18;
    const lv_coord_t detail_top = margin + summary_height + gap;
    const lv_coord_t detail_height = height_ - detail_top - margin;
    const lv_coord_t icon_width = 34;
    const lv_coord_t info_width = card_width - icon_width - gap;

    weather_screen_ = lv_obj_create(screen);
    SetRootStyle(weather_screen_);
    lv_obj_set_size(weather_screen_, width_, height_);
    lv_obj_add_flag(weather_screen_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* summary_card = lv_obj_create(weather_screen_);
    SetPanelStyle(summary_card, false, 8, 1);
    lv_obj_set_size(summary_card, card_width, summary_height);
    lv_obj_set_pos(summary_card, margin, margin);

    temp_text_ = lv_label_create(summary_card);
    lv_obj_set_width(temp_text_, 40);
    lv_obj_set_style_text_font(temp_text_, &font_puhui_16_4, 0);
    lv_obj_set_style_text_align(temp_text_, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(temp_text_, "--℃");
    lv_obj_align(temp_text_, LV_ALIGN_LEFT_MID, 5, 0);

    main_weather_label_ = lv_label_create(summary_card);
    lv_obj_set_width(main_weather_label_, 72);
    lv_obj_set_style_text_font(main_weather_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(main_weather_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(main_weather_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(main_weather_label_, "加载中");
    lv_obj_align(main_weather_label_, LV_ALIGN_RIGHT_MID, -4, 0);

    lv_obj_t* icon_card = lv_obj_create(weather_screen_);
    SetPanelStyle(icon_card, false, 8, 1);
    lv_obj_set_size(icon_card, icon_width, detail_height);
    lv_obj_set_pos(icon_card, margin, detail_top);

    weather_icon_ = lv_img_create(icon_card);
    lv_obj_center(weather_icon_);

    lv_obj_t* city_card = lv_obj_create(weather_screen_);
    SetPanelStyle(city_card, false, 8, 1);
    lv_obj_set_size(city_card, info_width, detail_height);
    lv_obj_set_pos(city_card, margin + icon_width + gap, detail_top);

    lv_obj_t* city_tag = lv_label_create(city_card);
    lv_obj_set_style_text_font(city_tag, fonts_.text_font, 0);
    lv_label_set_text(city_tag, "城市");
    lv_obj_align(city_tag, LV_ALIGN_TOP_LEFT, 4, 2);

    city_text_ = lv_label_create(city_card);
    lv_obj_set_width(city_text_, info_width - 8);
    lv_obj_set_style_text_font(city_text_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(city_text_, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(city_text_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(city_text_, "天气加载中");
    lv_obj_align(city_text_, LV_ALIGN_BOTTOM_LEFT, 4, -2);
}
void OledDisplay::Switch_Page_Init(lv_obj_t * screen) {
    const lv_coord_t header_height = GetHeaderHeight(fonts_);
    int i = 0;

    switch_header_ = CreateHeaderBar(screen, FONT_AWESOME_GEAR, "", fonts_);
    switch_title_label_ = lv_label_create(switch_header_);
    lv_obj_set_style_text_font(switch_title_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_color(switch_title_label_, lv_color_white(), 0);
    lv_label_set_text(switch_title_label_, home_options[current_index_].text);
    lv_obj_center(switch_title_label_);

    switch_indicator_label_ = lv_label_create(switch_header_);
    lv_obj_set_style_text_font(switch_indicator_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_color(switch_indicator_label_, lv_color_white(), 0);
    char indicator[16];
    snprintf(indicator, sizeof(indicator), "1/%d", Switch_Count);
    lv_label_set_text(switch_indicator_label_, indicator);
    lv_obj_align(switch_indicator_label_, LV_ALIGN_RIGHT_MID, -4, 0);

    switch_hint_label_ = lv_label_create(screen);
    lv_obj_set_width(switch_hint_label_, width_ - 8);
    lv_obj_set_style_text_font(switch_hint_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(switch_hint_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(switch_hint_label_, LV_LABEL_LONG_CLIP);
    lv_label_set_text(switch_hint_label_, kSwitchHints[current_index_]);
    lv_obj_align(switch_hint_label_, LV_ALIGN_BOTTOM_MID, 0, -1);

    for (i = 0; i < Switch_Count; i++) {
        pages[i].page = lv_obj_create(screen);
        SetPanelStyle(pages[i].page, false, 8, 1);
        lv_obj_set_size(pages[i].page, 40, 32);
        lv_obj_set_flex_flow(pages[i].page, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(pages[i].page, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        pages[i].img_select = lv_img_create(pages[i].page);
        lv_img_set_src(pages[i].img_select, home_options[i].img);
        lv_obj_set_size(pages[i].img_select, 24, 24);
        lv_img_set_zoom(pages[i].img_select, 192);

        pages[i].label_select = lv_label_create(pages[i].page);
        lv_label_set_text(pages[i].label_select, home_options[i].text);
        lv_obj_set_style_text_font(pages[i].label_select, fonts_.text_font, 0);
        lv_obj_set_style_text_align(pages[i].label_select, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_add_flag(pages[i].label_select, LV_OBJ_FLAG_HIDDEN);

        lv_obj_set_x(pages[i].page, 200);
        lv_obj_set_y(pages[i].page, header_height + 4);

        // 初始化时隐藏所有菜单页
        lv_obj_add_flag(pages[i].page, LV_OBJ_FLAG_HIDDEN);
    }

    // 初始化时隐藏 header 和 hint label
    lv_obj_add_flag(switch_header_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(switch_hint_label_, LV_OBJ_FLAG_HIDDEN);

    UpdateSwitchPagePosition();
}

void OledDisplay::UpdateSwitchPagePosition() {
    DisplayLockGuard lock(this);

    if (switch_title_label_ != nullptr) {
        lv_label_set_text(switch_title_label_, home_options[current_index_].text);
    }
    if (switch_indicator_label_ != nullptr) {
        char indicator[16];
        snprintf(indicator, sizeof(indicator), "%d/%d", current_index_ + 1, Switch_Count);
        lv_label_set_text(switch_indicator_label_, indicator);
    }
    if (switch_hint_label_ != nullptr) {
        lv_label_set_text(switch_hint_label_, kSwitchHints[current_index_]);
    }

    for (int i = 0; i < Switch_Count; i++) {
        int pos = (i - current_index_ + Switch_Count) % Switch_Count;

        // 确保所有卡片可见（slide_to 动画可能隐藏了远距离卡片）
        lv_obj_clear_flag(pages[i].page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(pages[i].img_select, LV_OBJ_FLAG_HIDDEN);

        if (pos == 0) {
            lv_obj_set_size(pages[i].page, 40, 28);
            lv_obj_set_x(pages[i].page, 44);
            lv_obj_set_y(pages[i].page, 20);
            lv_obj_set_style_border_width(pages[i].page, 2, 0);
            lv_obj_set_style_radius(pages[i].page, 10, 0);
            lv_obj_set_size(pages[i].img_select, 22, 22);
            lv_img_set_zoom(pages[i].img_select, 176);
            lv_obj_add_flag(pages[i].label_select, LV_OBJ_FLAG_HIDDEN);
        } else if (pos == 1) {
            lv_obj_set_size(pages[i].page, 22, 22);
            lv_obj_set_x(pages[i].page, 89);
            lv_obj_set_y(pages[i].page, 24);
            lv_obj_set_style_border_width(pages[i].page, 1, 0);
            lv_obj_set_style_radius(pages[i].page, 8, 0);
            lv_obj_set_size(pages[i].img_select, 16, 16);
            lv_img_set_zoom(pages[i].img_select, 132);
            lv_obj_add_flag(pages[i].label_select, LV_OBJ_FLAG_HIDDEN);
        } else if (pos == 2) {
            lv_obj_set_size(pages[i].page, 16, 16);
            lv_obj_set_x(pages[i].page, 113);
            lv_obj_set_y(pages[i].page, 28);
            lv_obj_set_style_border_width(pages[i].page, 0, 0);
            lv_obj_set_size(pages[i].img_select, 12, 12);
            lv_img_set_zoom(pages[i].img_select, 96);
            lv_obj_add_flag(pages[i].label_select, LV_OBJ_FLAG_HIDDEN);
        } else if (pos == Switch_Count - 1) {
            lv_obj_set_size(pages[i].page, 22, 22);
            lv_obj_set_x(pages[i].page, 15);
            lv_obj_set_y(pages[i].page, 24);
            lv_obj_set_style_border_width(pages[i].page, 1, 0);
            lv_obj_set_style_radius(pages[i].page, 8, 0);
            lv_obj_set_size(pages[i].img_select, 16, 16);
            lv_img_set_zoom(pages[i].img_select, 132);
            lv_obj_add_flag(pages[i].label_select, LV_OBJ_FLAG_HIDDEN);
        } else if (pos == Switch_Count - 2) {
            lv_obj_set_size(pages[i].page, 16, 16);
            lv_obj_set_x(pages[i].page, -1);
            lv_obj_set_y(pages[i].page, 28);
            lv_obj_set_style_border_width(pages[i].page, 0, 0);
            lv_obj_set_size(pages[i].img_select, 12, 12);
            lv_img_set_zoom(pages[i].img_select, 96);
            lv_obj_add_flag(pages[i].label_select, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_set_size(pages[i].page, 16, 16);
            lv_obj_set_x(pages[i].page, pos < Switch_Count / 2 ? -40 : 146);
            lv_obj_set_y(pages[i].page, 28);
            lv_obj_set_style_border_width(pages[i].page, 0, 0);
            lv_img_set_zoom(pages[i].img_select, 96);
            lv_obj_add_flag(pages[i].label_select, LV_OBJ_FLAG_HIDDEN);
        }

        const bool active = (pos == 0);
        lv_obj_set_style_bg_color(pages[i].page, lv_color_white(), 0);
        lv_obj_set_style_text_color(pages[i].page, lv_color_black(), 0);
        lv_obj_set_style_img_recolor_opa(pages[i].img_select, LV_OPA_TRANSP, 0);
        lv_obj_set_style_img_opa(pages[i].img_select, active ? LV_OPA_COVER : LV_OPA_80, 0);
    }
}

/**
 * @brief 初始化对话页面
 * @param screen 父屏幕对象
 * @details 创建对话页面的UI布局，包括状态栏、内容区域和低电量弹窗
 */
void OledDisplay::Dialougue_Page_Init(lv_obj_t * screen) {
    const lv_coord_t status_height = GetHeaderHeight(fonts_);

    // 创建对话页面主容器
    dialogue_screen_ = lv_obj_create(screen);
    if (dialogue_screen_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create dialogue screen");
        return;
    }
    
    // 设置页面字体和文本颜色
    lv_obj_set_style_text_font(dialogue_screen_, fonts_.text_font, 0);
    lv_obj_set_style_text_color(dialogue_screen_, lv_color_black(), 0);
    // 初始状态下隐藏页面
    lv_obj_add_flag(dialogue_screen_, LV_OBJ_FLAG_HIDDEN);
    // 强制定位到屏幕左上角
    lv_obj_set_pos(dialogue_screen_, 0, 0);
    // 强制设置为全屏尺寸
    lv_obj_set_size(dialogue_screen_, LV_HOR_RES, LV_VER_RES);
    // 清除内边距
    lv_obj_set_style_pad_all(dialogue_screen_, 0, LV_PART_MAIN);
    // 清除边框
    lv_obj_set_style_border_width(dialogue_screen_, 0, LV_PART_MAIN);
    
    /* 主容器 */
    container_ = lv_obj_create(dialogue_screen_);
    // 设置容器尺寸为全屏
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    // 设置为垂直布局
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    // 关键：设置垂直对齐为顶部对齐，子元素从顶部开始排列
    lv_obj_set_flex_align(container_, 
        LV_FLEX_ALIGN_START,  // 主轴（垂直）顶部对齐
        LV_FLEX_ALIGN_CENTER, // 交叉轴（水平）居中
        LV_FLEX_ALIGN_CENTER);
    // 清除内边距
    lv_obj_set_style_pad_all(container_, 0, 0);
    // 清除边框
    lv_obj_set_style_border_width(container_, 0, 0);
    // 清除状态栏与内容区间距
    lv_obj_set_style_pad_row(container_, 0, 0);


    /* 内容区域 */
    content_ = lv_obj_create(container_);
    // 关闭滚动条
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    // 清除圆角
    lv_obj_set_style_radius(content_, 0, 0);
    // 清除内边距
    lv_obj_set_style_pad_all(content_, 0, 0);
    // 设置宽度为全屏
    lv_obj_set_width(content_, LV_HOR_RES); 
    // 设置高度为屏幕高度减去状态栏高度
    lv_obj_set_height(content_, height_ - status_height); 
    // 设置为水平布局
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_ROW);
    // 设置元素居中对齐
    lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    // 设置元素间距为8像素
    lv_obj_set_style_pad_gap(content_, 8, 0); 

    /* 状态栏 */
    status_bar_ = lv_obj_create(container_);
    // 设置状态栏尺寸：全屏宽度，高度16像素
    lv_obj_set_size(status_bar_, width_, status_height);
    // 清除边框
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    // 清除内边距
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    // 清除圆角
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_color(status_bar_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_COVER, 0);


    // 创建左侧固定宽度的容器
    content_left_ = lv_obj_create(content_);
    // 设置宽度为32像素，高度自适应
    lv_obj_set_size(content_left_, 32, LV_SIZE_CONTENT);
    // 清除内边距
    lv_obj_set_style_pad_all(content_left_, 0, 0);
    // 清除边框
    lv_obj_set_style_border_width(content_left_, 0, 0);

    // 创建情感图标标签
    emotion_label_ = lv_label_create(content_left_);
    // 设置图标字体
    lv_obj_set_style_text_font(emotion_label_, &font_awesome_30_1, 0);
    // 设置芯片图标
    lv_label_set_text(emotion_label_, FONT_AWESOME_USER);
    // 居中显示
    lv_obj_center(emotion_label_);
    // 设置顶部内边距为8像素
    lv_obj_set_style_pad_top(emotion_label_, 8, 0);

    // 创建右侧可扩展的容器
    content_right_ = lv_obj_create(content_);
    // 尺寸自适应
    lv_obj_set_size(content_right_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    // 添加内边距
    lv_obj_set_style_pad_all(content_right_, 8, 0);
    // 添加边框
    lv_obj_set_style_border_width(content_right_, 1, 0);
    // 边框颜色
    lv_obj_set_style_border_color(content_right_, lv_color_black(), 0);
    // 添加圆角
    lv_obj_set_style_radius(content_right_, 8, 0);
    // 设置弹性增长，占满剩余空间
    lv_obj_set_flex_grow(content_right_, 1);
    // 初始状态下隐藏
    lv_obj_add_flag(content_right_, LV_OBJ_FLAG_HIDDEN);

    // 创建聊天消息标签
    chat_message_label_ = lv_label_create(content_right_);
    // 初始文本为空
    lv_label_set_text(chat_message_label_, "");
    // 设置为循环滚动模式
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    // 设置文本左对齐
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_LEFT, 0);
    // 设置宽度为屏幕宽度减去左侧容器宽度和内边距
    lv_obj_set_width(chat_message_label_, width_ - 48);
    // 移除顶部内边距，使用容器的内边距
    lv_obj_set_style_pad_top(chat_message_label_, 0, 0);

    // 延迟一定的时间后开始滚动字幕
    static lv_anim_t a;
    // 初始化动画
    lv_anim_init(&a);
    // 设置1000毫秒延迟
    lv_anim_set_delay(&a, 1000);
    // 设置无限重复
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    // 应用动画到聊天消息标签
    lv_obj_set_style_anim(chat_message_label_, &a, LV_PART_MAIN);
    // 设置动画速度，根据文本长度自动调整
    lv_obj_set_style_anim_duration(chat_message_label_, lv_anim_speed_clamped(30, 300, 60000), LV_PART_MAIN);

    /* 状态栏内容 */
    // 设置状态栏为水平布局
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
    // 清除内边距
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    // 清除边框
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    // 清除列间距
    lv_obj_set_style_pad_column(status_bar_, 0, 0);

    // 创建网络状态标签
    network_label_ = lv_label_create(status_bar_);
    // 初始文本为空
    lv_label_set_text(network_label_, "");
    // 设置图标字体
    lv_obj_set_style_text_font(network_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(network_label_, lv_color_white(), 0);

    // 创建通知标签
    notification_label_ = lv_label_create(status_bar_);
    // 设置弹性增长
    lv_obj_set_flex_grow(notification_label_, 1);
    // 设置文本居中对齐
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    // 初始文本为空
    lv_label_set_text(notification_label_, "");
    lv_obj_set_style_text_color(notification_label_, lv_color_white(), 0);
    // 初始状态下隐藏
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    // 创建状态标签
    status_label_ = lv_label_create(status_bar_);
    // 设置弹性增长
    lv_obj_set_flex_grow(status_label_, 1);
    // 设置初始文本为"初始化中"
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    // 设置文本居中对齐
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label_, lv_color_white(), 0);

    // 创建静音标签
    mute_label_ = lv_label_create(status_bar_);
    // 初始文本为空
    lv_label_set_text(mute_label_, "");
    // 设置图标字体
    lv_obj_set_style_text_font(mute_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(mute_label_, lv_color_white(), 0);
    // 初始状态下隐藏
    lv_obj_add_flag(mute_label_, LV_OBJ_FLAG_HIDDEN);

    // 电池标签（已注释）
    // battery_label_ = lv_label_create(status_bar_);
    // lv_label_set_text(battery_label_, "");
    // lv_obj_set_style_text_font(battery_label_, fonts_.icon_font, 0);
    //     // 电量标签在状态栏显示
    
    // 创建低电量弹窗
    low_battery_popup_ = lv_obj_create(dialogue_screen_);
    // 关闭滚动条
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    // 设置弹窗尺寸：宽度为屏幕的90%，高度为2行文本
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, fonts_.text_font->line_height * 2);
    // 定位到屏幕底部中央
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, 0);
    // 设置背景颜色为黑色
    lv_obj_set_style_bg_color(low_battery_popup_, lv_color_black(), 0);
    // 设置圆角为10像素
    lv_obj_set_style_radius(low_battery_popup_, 10, 0);
    
    // 创建低电量标签
    low_battery_label_ = lv_label_create(low_battery_popup_);
    // 设置文本为"电池需要充电"
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    // 设置文本颜色为白色
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    // 居中显示
    lv_obj_center(low_battery_label_);
    // 初始状态下隐藏
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
}

void OledDisplay::About_Page_Init(lv_obj_t * screen) {
    const lv_coord_t margin = 2;
    const lv_coord_t gap = 2;
    const lv_coord_t card_width = width_ - margin * 2;
    const lv_coord_t row_height = 16;
    const lv_coord_t footer_top = margin + row_height + gap + row_height + gap;
    const lv_coord_t footer_height = height_ - footer_top - margin;

    about_screen_ = lv_obj_create(screen);
    if (about_screen_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create about screen");
        return;
    }

    SetRootStyle(about_screen_);
    lv_obj_set_size(about_screen_, width_, height_);
    lv_obj_add_flag(about_screen_, LV_OBJ_FLAG_HIDDEN);

    const auto* app_desc = esp_app_get_description();

    lv_obj_t* project_card = lv_obj_create(about_screen_);
    SetPanelStyle(project_card, false, 8, 1);
    lv_obj_set_size(project_card, card_width, row_height);
    lv_obj_set_pos(project_card, margin, margin);

    lv_obj_t* board_label = lv_label_create(project_card);
    lv_obj_set_width(board_label, card_width - 8);
    lv_obj_set_style_text_font(board_label, fonts_.text_font, 0);
    lv_label_set_text_fmt(board_label, "题目：  %s", "基于ESP32的多功能智能时间手表与AI交互系统");
    lv_label_set_long_mode(board_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(board_label, LV_ALIGN_LEFT_MID, 4, 0);

    lv_obj_t* version_card = lv_obj_create(about_screen_);
    SetPanelStyle(version_card, false, 8, 1);
    lv_obj_set_size(version_card, card_width, row_height);
    lv_obj_set_pos(version_card, margin, margin + row_height + gap);

    lv_obj_t* version_label = lv_label_create(version_card);
    lv_obj_set_width(version_label, card_width - 8);
    lv_obj_set_style_text_font(version_label, fonts_.text_font, 0);
    lv_obj_set_style_text_align(version_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(version_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text_fmt(version_label, "口号：  %s", "AI 赋能设计，设计点亮AI！Al for Design & Design for Al!");
    lv_obj_align(version_label, LV_ALIGN_LEFT_MID, 4, 0);

    lv_obj_t* footer_card = lv_obj_create(about_screen_);
    SetPanelStyle(footer_card, false, 8, 1);
    lv_obj_set_size(footer_card, card_width, footer_height);
    lv_obj_set_pos(footer_card, margin, footer_top);

    about_stats_label_ = lv_label_create(footer_card);
    lv_obj_set_width(about_stats_label_, card_width - 10);
    lv_obj_set_style_text_font(about_stats_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(about_stats_label_, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(about_stats_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(about_stats_label_, "加载中...");
    lv_obj_align(about_stats_label_, LV_ALIGN_CENTER, 0, 0);
}

void OledDisplay::Wait_Page_Init(lv_obj_t * screen) {


}

void OledDisplay::Reconfig_Page_Init(lv_obj_t * screen) {
    const lv_coord_t margin = 2;
    const lv_coord_t gap = 2;
    const lv_coord_t card_width = width_ - margin * 2;
    const lv_coord_t row_height = 16;
    const lv_coord_t content_top = margin + row_height + gap;
    const lv_coord_t content_height = height_ - content_top - margin;

    reconfig_screen_ = lv_obj_create(screen);
    if (reconfig_screen_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create reconfig screen");
        return;
    }

    SetRootStyle(reconfig_screen_);
    lv_obj_set_size(reconfig_screen_, width_, height_);
    lv_obj_add_flag(reconfig_screen_, LV_OBJ_FLAG_HIDDEN);

    // Title card
    lv_obj_t* title_card = lv_obj_create(reconfig_screen_);
    SetPanelStyle(title_card, false, 8, 1);
    lv_obj_set_size(title_card, card_width, row_height);
    lv_obj_set_pos(title_card, margin, margin);

    lv_obj_t* title_label = lv_label_create(title_card);
    lv_obj_set_style_text_font(title_label, fonts_.text_font, 0);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title_label, "WiFi配置模式");
    lv_obj_center(title_label);

    // Content card
    lv_obj_t* content_card = lv_obj_create(reconfig_screen_);
    SetPanelStyle(content_card, false, 8, 1);
    lv_obj_set_size(content_card, card_width, content_height);
    lv_obj_set_pos(content_card, margin, content_top);

    lv_obj_t* content_label = lv_label_create(content_card);
    lv_obj_set_width(content_label, card_width - 10);
    lv_obj_set_style_text_font(content_label, fonts_.text_font, 0);
    lv_obj_set_style_text_align(content_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(content_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(content_label, "请按页面按钮确认");

    lv_obj_align(content_label, LV_ALIGN_CENTER, 0, 0);
}

void OledDisplay::Flashlight_Page_Init(lv_obj_t * screen) {
    const lv_coord_t margin = 2;
    const lv_coord_t gap = 2;
    const lv_coord_t card_width = width_ - margin * 2;
    const lv_coord_t title_height = 14;
    const lv_coord_t current_height = 18;
    const lv_coord_t palette_top = margin + title_height + gap + current_height + gap;
    const lv_coord_t palette_height = height_ - palette_top - margin;

    flashlight_screen_ = lv_obj_create(screen);
    if (flashlight_screen_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create flashlight screen");
        return;
    }

    SetRootStyle(flashlight_screen_);
    lv_obj_set_size(flashlight_screen_, width_, height_);
    lv_obj_add_flag(flashlight_screen_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* title_card = lv_obj_create(flashlight_screen_);
    SetPanelStyle(title_card, false, 8, 1);
    lv_obj_set_size(title_card, card_width, title_height);
    lv_obj_set_pos(title_card, margin, margin);

    lv_obj_t* icon = lv_label_create(title_card);
    lv_obj_set_style_text_font(icon, fonts_.icon_font, 0);
    lv_label_set_text(icon, FONT_AWESOME_POWER);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 4, 0);

    lv_obj_t* title = lv_label_create(title_card);
    lv_obj_set_style_text_font(title, fonts_.text_font, 0);
    lv_label_set_text(title, "手电");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 22, 0);

    lv_obj_t* current_card = lv_obj_create(flashlight_screen_);
    SetPanelStyle(current_card, false, 8, 1);
    lv_obj_set_size(current_card, card_width, current_height);
    lv_obj_set_pos(current_card, margin, margin + title_height + gap);

    flashlight_text_ = lv_label_create(current_card);
    if (flashlight_text_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create flashlight text");
        return;
    }
    lv_obj_set_width(flashlight_text_, card_width - 10);
    lv_obj_set_style_text_font(flashlight_text_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(flashlight_text_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(flashlight_text_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(flashlight_text_, "颜色 白色");
    lv_obj_align(flashlight_text_, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t* palette_card = lv_obj_create(flashlight_screen_);
    SetPanelStyle(palette_card, false, 8, 1);
    lv_obj_set_size(palette_card, card_width, palette_height);
    lv_obj_set_pos(palette_card, margin, palette_top);

    lv_obj_t* palette = lv_label_create(palette_card);
    lv_obj_set_width(palette, card_width - 10);
    lv_obj_set_style_text_font(palette, fonts_.text_font, 0);
    lv_obj_set_style_text_align(palette, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(palette, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(palette, "白 红 橙 黄 绿 蓝 靛 紫");
    lv_obj_align(palette, LV_ALIGN_CENTER, 0, 0);
}

void OledDisplay::Miaobiao_Page_Init(lv_obj_t * screen) {
    const lv_coord_t margin = 2;
    const lv_coord_t gap = 2;
    const lv_coord_t card_width = width_ - margin * 2;
    const lv_coord_t title_height = 14;
    const lv_coord_t time_height = 22;
    const lv_coord_t button_width = 38;
    const lv_coord_t button_height = 18;
    const lv_coord_t button_gap = 4;
    const lv_coord_t button_y = margin + title_height + gap + time_height + gap;

    miaobiao_screen_ = lv_obj_create(screen);
    if (miaobiao_screen_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create miaobiao screen");
        return;
    }

    SetRootStyle(miaobiao_screen_);
    lv_obj_set_size(miaobiao_screen_, width_, height_);
    lv_obj_add_flag(miaobiao_screen_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* title_card = lv_obj_create(miaobiao_screen_);
    SetPanelStyle(title_card, false, 8, 1);
    lv_obj_set_size(title_card, card_width, title_height);
    lv_obj_set_pos(title_card, margin, margin);

    lv_obj_t* title = lv_label_create(title_card);
    lv_obj_set_style_text_font(title, fonts_.text_font, 0);
    lv_label_set_text(title, "秒表");
    lv_obj_center(title);

    lv_obj_t* time_card = lv_obj_create(miaobiao_screen_);
    SetPanelStyle(time_card, false, 8, 1);
    lv_obj_set_size(time_card, card_width, time_height);
    lv_obj_set_pos(time_card, margin, margin + title_height + gap);

    miaobiao_time_label_ = lv_label_create(time_card);
    if (miaobiao_time_label_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create miaobiao time label");
        return;
    }
    lv_obj_set_width(miaobiao_time_label_, card_width - 10);
    lv_label_set_text(miaobiao_time_label_, "00:00:00");
    lv_obj_set_style_text_font(miaobiao_time_label_, &font_puhui_16_4, 0);
    lv_obj_set_style_text_color(miaobiao_time_label_, lv_color_black(), 0);
    lv_obj_set_style_text_align(miaobiao_time_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(miaobiao_time_label_);

    miaobiao_start_btn_ = CreateCompactButton(
        miaobiao_screen_, margin, button_y, button_width, button_height,
        FONT_AWESOME_PAUSE, fonts_.icon_font);
    if (miaobiao_start_btn_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create start button");
        return;
    }
    lv_obj_add_event_cb(miaobiao_start_btn_, [](lv_event_t *e) {
        OledDisplay* display = static_cast<OledDisplay*>(lv_event_get_user_data(e));
        display->PauseStopwatch();
    }, LV_EVENT_CLICKED, this);

    miaobiao_stop_btn_ = CreateCompactButton(
        miaobiao_screen_, margin + button_width + button_gap, button_y, button_width, button_height,
        FONT_AWESOME_PLAY, fonts_.icon_font);
    if (miaobiao_stop_btn_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create pause button");
        return;
    }
    lv_obj_add_event_cb(miaobiao_stop_btn_, [](lv_event_t *e) {
        OledDisplay* display = static_cast<OledDisplay*>(lv_event_get_user_data(e));
        display->StartStopwatch();
    }, LV_EVENT_CLICKED, this);

    miaobiao_reset_btn_ = CreateCompactButton(
        miaobiao_screen_, margin + (button_width + button_gap) * 2, button_y, button_width, button_height,
        FONT_AWESOME_TRASH, fonts_.icon_font);
    if (miaobiao_reset_btn_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create reset button");
        return;
    }
    lv_obj_add_event_cb(miaobiao_reset_btn_, [](lv_event_t *e) {
        OledDisplay* display = static_cast<OledDisplay*>(lv_event_get_user_data(e));
        display->ResetStopwatch();
    }, LV_EVENT_CLICKED, this);
}

void OledDisplay::Boot_Page_Init(lv_obj_t* screen) {
    boot_screen_ = lv_obj_create(screen);
    if (boot_screen_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create boot screen");
        return;
    }

    SetRootStyle(boot_screen_);
    lv_obj_set_size(boot_screen_, width_, height_);
    lv_obj_set_style_bg_color(boot_screen_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(boot_screen_, LV_OPA_COVER, 0);
    lv_obj_add_flag(boot_screen_, LV_OBJ_FLAG_HIDDEN);  // 初始化时隐藏

    boot_label_ = lv_label_create(boot_screen_);
    if (boot_label_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create boot label");
        return;
    }
    lv_obj_set_style_text_font(boot_label_, &font_puhui_16_4, 0);
    lv_obj_set_style_text_color(boot_label_, lv_color_black(), 0);
    lv_label_set_text(boot_label_, "XiaoZhi");
    lv_obj_center(boot_label_);
    lv_obj_set_style_opa(boot_label_, LV_OPA_TRANSP, 0);
}

void OledDisplay::Naozhong_Page_Init(lv_obj_t * screen) {
    const lv_coord_t margin = 2;
    const lv_coord_t gap = 2;
    const lv_coord_t card_width = width_ - margin * 2;
    const lv_coord_t time_height = 22;
    const lv_coord_t music_height = 16;
    const lv_coord_t button_width = 38;
    const lv_coord_t button_height = 18;
    const lv_coord_t button_gap = 4;
    const lv_coord_t time_y = margin;
    const lv_coord_t music_y = time_y + time_height + gap;
    const lv_coord_t button_y = music_y + music_height + gap;

    naozhong_screen_ = lv_obj_create(screen);
    if (naozhong_screen_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create naozhong screen");
        return;
    }

    SetRootStyle(naozhong_screen_);
    lv_obj_set_size(naozhong_screen_, width_, height_);
    lv_obj_add_flag(naozhong_screen_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* time_card = lv_obj_create(naozhong_screen_);
    SetPanelStyle(time_card, false, 8, 1);
    lv_obj_set_size(time_card, card_width, time_height);
    lv_obj_set_pos(time_card, margin, time_y);

    lv_obj_t* time_container = lv_obj_create(time_card);
    SetRootStyle(time_container);
    lv_obj_set_size(time_container, 90, 18);
    lv_obj_set_layout(time_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(time_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(time_container, 2, 0);
    lv_obj_center(time_container);

    naozhong_hour_label_ = lv_label_create(time_container);
    lv_label_set_text(naozhong_hour_label_, "00");
    lv_obj_set_size(naozhong_hour_label_, 22, 18);
    lv_obj_set_style_text_font(naozhong_hour_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(naozhong_hour_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(naozhong_hour_label_, lv_color_black(), 0);
    lv_obj_set_style_border_width(naozhong_hour_label_, 1, 0);
    lv_obj_set_style_border_color(naozhong_hour_label_, lv_color_black(), 0);
    lv_obj_set_style_radius(naozhong_hour_label_, 4, 0);
    lv_obj_set_style_pad_all(naozhong_hour_label_, 1, 0);
    lv_obj_set_style_bg_color(naozhong_hour_label_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(naozhong_hour_label_, LV_OPA_COVER, 0);

    lv_obj_t *colon1 = lv_label_create(time_container);
    lv_label_set_text(colon1, ":");
    lv_obj_set_style_text_font(colon1, fonts_.text_font, 0);
    lv_obj_set_style_text_color(colon1, lv_color_black(), 0);

    naozhong_minute_label_ = lv_label_create(time_container);
    lv_label_set_text(naozhong_minute_label_, "00");
    lv_obj_set_size(naozhong_minute_label_, 22, 18);
    lv_obj_set_style_text_font(naozhong_minute_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(naozhong_minute_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(naozhong_minute_label_, lv_color_black(), 0);
    lv_obj_set_style_border_width(naozhong_minute_label_, 1, 0);
    lv_obj_set_style_border_color(naozhong_minute_label_, lv_color_black(), 0);
    lv_obj_set_style_radius(naozhong_minute_label_, 4, 0);
    lv_obj_set_style_pad_all(naozhong_minute_label_, 1, 0);
    lv_obj_set_style_bg_color(naozhong_minute_label_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(naozhong_minute_label_, LV_OPA_COVER, 0);

    lv_obj_t *colon2 = lv_label_create(time_container);
    lv_label_set_text(colon2, ":");
    lv_obj_set_style_text_font(colon2, fonts_.text_font, 0);
    lv_obj_set_style_text_color(colon2, lv_color_black(), 0);

    naozhong_second_label_ = lv_label_create(time_container);
    lv_label_set_text(naozhong_second_label_, "00");
    lv_obj_set_size(naozhong_second_label_, 22, 18);
    lv_obj_set_style_text_font(naozhong_second_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(naozhong_second_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(naozhong_second_label_, lv_color_black(), 0);
    lv_obj_set_style_border_width(naozhong_second_label_, 1, 0);
    lv_obj_set_style_border_color(naozhong_second_label_, lv_color_black(), 0);
    lv_obj_set_style_radius(naozhong_second_label_, 4, 0);
    lv_obj_set_style_pad_all(naozhong_second_label_, 1, 0);
    lv_obj_set_style_bg_color(naozhong_second_label_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(naozhong_second_label_, LV_OPA_COVER, 0);

    naozhong_time_label_ = lv_label_create(naozhong_screen_);
    if (naozhong_time_label_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create naozhong time label");
        return;
    }
    lv_label_set_text(naozhong_time_label_, "00:00:00");
    lv_obj_add_flag(naozhong_time_label_, LV_OBJ_FLAG_HIDDEN);

    naozhong_music_card_ = lv_obj_create(naozhong_screen_);
    SetPanelStyle(naozhong_music_card_, false, 8, 1);
    lv_obj_set_size(naozhong_music_card_, card_width, music_height);
    lv_obj_set_pos(naozhong_music_card_, margin, music_y);

    lv_obj_t* music_tag = lv_label_create(naozhong_music_card_);
    lv_obj_set_style_text_font(music_tag, fonts_.text_font, 0);
    lv_label_set_text(music_tag, "铃声");
    lv_obj_align(music_tag, LV_ALIGN_LEFT_MID, 4, 0);

    naozhong_music_label_ = lv_label_create(naozhong_music_card_);
    lv_obj_set_width(naozhong_music_label_, card_width - 36);
    lv_obj_set_style_text_font(naozhong_music_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(naozhong_music_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(naozhong_music_label_, LV_LABEL_LONG_CLIP);
    lv_label_set_text(naozhong_music_label_, "音乐1");
    lv_obj_align(naozhong_music_label_, LV_ALIGN_RIGHT_MID, -4, 0);

    naozhong_set_btn_ = CreateCompactButton(
        naozhong_screen_, margin, button_y, button_width, button_height,
        FONT_AWESOME_GEAR, fonts_.icon_font);
    if (naozhong_set_btn_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create set button");
        return;
    }
    lv_obj_add_event_cb(naozhong_set_btn_, [](lv_event_t *e) {
        OledDisplay* display = static_cast<OledDisplay*>(lv_event_get_user_data(e));
        time_t now = time(NULL);
        struct tm* tm = localtime(&now);
        int hours = tm->tm_hour;
        int minutes = tm->tm_min + 1;
        int seconds = tm->tm_sec;
        if (minutes >= 60) {
            minutes = 0;
            hours++;
            if (hours >= 24) {
                hours = 0;
            }
        }
        display->SetAlarmTime(hours, minutes, seconds);
        display->SetAlarmButtonHighlight(0);
    }, LV_EVENT_CLICKED, this);

    naozhong_start_btn_ = CreateCompactButton(
        naozhong_screen_, margin + button_width + button_gap, button_y, button_width, button_height,
        FONT_AWESOME_STOP, fonts_.icon_font);
    if (naozhong_start_btn_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create start button");
        return;
    }
    lv_obj_add_event_cb(naozhong_start_btn_, [](lv_event_t *e) {
        OledDisplay* display = static_cast<OledDisplay*>(lv_event_get_user_data(e));
        display->StopAlarm();
    }, LV_EVENT_CLICKED, this);

    naozhong_stop_btn_ = CreateCompactButton(
        naozhong_screen_, margin + (button_width + button_gap) * 2, button_y, button_width, button_height,
        FONT_AWESOME_PLAY, fonts_.icon_font);
    if (naozhong_stop_btn_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create stop button");
        return;
    }
    lv_obj_add_event_cb(naozhong_stop_btn_, [](lv_event_t *e) {
        OledDisplay* display = static_cast<OledDisplay*>(lv_event_get_user_data(e));
        display->StartAlarm();
    }, LV_EVENT_CLICKED, this);
}
void OledDisplay::SetupUI_128x64() {
    DisplayLockGuard lock(this);
    default_screen = lv_screen_active();

    Boot_Page_Init(default_screen);
    Switch_Page_Init(default_screen);
    Main_Page_Init(default_screen);
    Dialougue_Page_Init(default_screen);
    Health_Check_Page_Init(default_screen);
    Weather_Page_Init(default_screen);
    About_Page_Init(default_screen);
    Reconfig_Page_Init(default_screen);
    Flashlight_Page_Init(default_screen);
    Miaobiao_Page_Init(default_screen);
	Naozhong_Page_Init(default_screen);
    Mpu6050_Page_Init(default_screen);
    Sport_Page_Init(default_screen);
    Fun_Page_Init(default_screen);
    Game_Page_Init(default_screen);
    Reflex_Page_Init(default_screen);
    Memory_Page_Init(default_screen);
    Breathe_Page_Init(default_screen);
    Quick_Page_Init(default_screen);
    Sound_Page_Init(default_screen);
    Time_Setting_Page_Init(default_screen);
    Wait_Page_Init(default_screen);

    UpdateFunPageUi();
    game_running_ = false;
    game_over_ = false;
    game_score_ = 0;
    game_player_lane_ = 1;
    game_obstacle_lane_ = 0;
    game_obstacle_y_ = 2;
    UpdateGamePageUi();
    UpdateReflexPageUi();
    UpdateMemoryPageUi();
    UpdateBreathePageUi();

    // 立即显示开机画面，避免短暂露出菜单页
    if (boot_screen_ != nullptr) {
        lv_obj_clear_flag(boot_screen_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(boot_screen_);
        // 提前显示文字，避免白屏（ShowBootScreen 的淡入动画会重新设置）
        if (boot_label_ != nullptr) {
            lv_obj_set_style_opa(boot_label_, LV_OPA_COVER, 0);
        }
    }
}

void OledDisplay::SetupUI_128x32() {
   
}

void OledDisplay::SetFlashlightText(const char* text) {
    DisplayLockGuard lock(this);
    if (flashlight_text_ == nullptr) {
        return;
    }
    // 只在文本实际变化时才更新，避免不必要的重绘
    if (strcmp(lv_label_get_text(flashlight_text_), text) == 0) {
        return;
    }
    lv_label_set_text(flashlight_text_, text);
}

void OledDisplay::Mpu6050_Page_Init(lv_obj_t * screen) {
    const lv_coord_t margin = 2;
    const lv_coord_t gap = 3;
    const lv_coord_t card_width = width_ - margin * 2;
    const lv_coord_t row_height = 18;

    mpu6050_screen_ = lv_obj_create(screen);
    if (mpu6050_screen_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create mpu6050 screen");
        return;
    }

    SetRootStyle(mpu6050_screen_);
    lv_obj_set_size(mpu6050_screen_, width_, height_);
    lv_obj_add_flag(mpu6050_screen_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* pitch_card = lv_obj_create(mpu6050_screen_);
    SetPanelStyle(pitch_card, false, 8, 1);
    lv_obj_set_size(pitch_card, card_width, row_height);
    lv_obj_set_pos(pitch_card, margin, margin);

    lv_obj_t* pitch_tag = lv_label_create(pitch_card);
    lv_obj_set_style_text_font(pitch_tag, fonts_.text_font, 0);
    lv_label_set_text(pitch_tag, "俯仰");
    lv_obj_align(pitch_tag, LV_ALIGN_LEFT_MID, 4, 0);

    mpu6050_pitch_label_ = lv_label_create(pitch_card);
    lv_obj_set_width(mpu6050_pitch_label_, 56);
    lv_label_set_long_mode(mpu6050_pitch_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(mpu6050_pitch_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(mpu6050_pitch_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(mpu6050_pitch_label_, "0.0");
    lv_obj_align(mpu6050_pitch_label_, LV_ALIGN_RIGHT_MID, -4, 0);

    lv_obj_t* roll_card = lv_obj_create(mpu6050_screen_);
    SetPanelStyle(roll_card, false, 8, 1);
    lv_obj_set_size(roll_card, card_width, row_height);
    lv_obj_set_pos(roll_card, margin, margin + row_height + gap);

    lv_obj_t* roll_tag = lv_label_create(roll_card);
    lv_obj_set_style_text_font(roll_tag, fonts_.text_font, 0);
    lv_label_set_text(roll_tag, "横滚");
    lv_obj_align(roll_tag, LV_ALIGN_LEFT_MID, 4, 0);

    mpu6050_roll_label_ = lv_label_create(roll_card);
    lv_obj_set_width(mpu6050_roll_label_, 56);
    lv_label_set_long_mode(mpu6050_roll_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(mpu6050_roll_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(mpu6050_roll_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(mpu6050_roll_label_, "0.0");
    lv_obj_align(mpu6050_roll_label_, LV_ALIGN_RIGHT_MID, -4, 0);

    lv_obj_t* lower_card = lv_obj_create(mpu6050_screen_);
    SetPanelStyle(lower_card, false, 8, 1);
    lv_obj_set_size(lower_card, card_width, row_height);
    lv_obj_set_pos(lower_card, margin, margin + (row_height + gap) * 2);

    lv_obj_t* yaw_tag = lv_label_create(lower_card);
    lv_obj_set_style_text_font(yaw_tag, fonts_.text_font, 0);
    lv_label_set_text(yaw_tag, "航向");
    lv_obj_align(yaw_tag, LV_ALIGN_LEFT_MID, 4, 0);

    mpu6050_yaw_label_ = lv_label_create(lower_card);
    lv_obj_set_width(mpu6050_yaw_label_, 56);
    lv_label_set_long_mode(mpu6050_yaw_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(mpu6050_yaw_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(mpu6050_yaw_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(mpu6050_yaw_label_, "0.0");
    lv_obj_align(mpu6050_yaw_label_, LV_ALIGN_RIGHT_MID, -4, 0);
}

void OledDisplay::Sport_Page_Init(lv_obj_t * screen) {
    const lv_coord_t margin = 2;
    const lv_coord_t gap = 3;
    const lv_coord_t card_width = width_ - margin * 2;
    const lv_coord_t row_height = 18;

    sport_screen_ = lv_obj_create(screen);
    if (sport_screen_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create sport screen");
        return;
    }

    SetRootStyle(sport_screen_);
    lv_obj_set_size(sport_screen_, width_, height_);
    lv_obj_add_flag(sport_screen_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* hero_card = lv_obj_create(sport_screen_);
    SetPanelStyle(hero_card, false, 8, 1);
    lv_obj_set_size(hero_card, card_width, row_height);
    lv_obj_set_pos(hero_card, margin, margin);

    lv_obj_t* steps_title = lv_label_create(hero_card);
    lv_obj_set_style_text_font(steps_title, fonts_.text_font, 0);
    lv_label_set_text(steps_title, "步数");
    lv_obj_align(steps_title, LV_ALIGN_LEFT_MID, 4, 0);

    sport_steps_label_ = lv_label_create(hero_card);
    lv_obj_set_width(sport_steps_label_, 62);
    lv_label_set_long_mode(sport_steps_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(sport_steps_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(sport_steps_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(sport_steps_label_, "0");
    lv_obj_align(sport_steps_label_, LV_ALIGN_RIGHT_MID, -4, 0);

    lv_obj_t* calories_card = lv_obj_create(sport_screen_);
    SetPanelStyle(calories_card, false, 8, 1);
    lv_obj_set_size(calories_card, card_width, row_height);
    lv_obj_set_pos(calories_card, margin, margin + row_height + gap);

    lv_obj_t* calories_title = lv_label_create(calories_card);
    lv_obj_set_style_text_font(calories_title, fonts_.text_font, 0);
    lv_label_set_text(calories_title, "热量");
    lv_obj_align(calories_title, LV_ALIGN_LEFT_MID, 4, 0);

    sport_calories_label_ = lv_label_create(calories_card);
    lv_obj_set_width(sport_calories_label_, 52);
    lv_label_set_long_mode(sport_calories_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(sport_calories_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(sport_calories_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(sport_calories_label_, "0.0");
    lv_obj_align(sport_calories_label_, LV_ALIGN_RIGHT_MID, -4, 0);

    lv_obj_t* distance_card = lv_obj_create(sport_screen_);
    SetPanelStyle(distance_card, false, 8, 1);
    lv_obj_set_size(distance_card, card_width, row_height);
    lv_obj_set_pos(distance_card, margin, margin + (row_height + gap) * 2);

    lv_obj_t* distance_title = lv_label_create(distance_card);
    lv_obj_set_style_text_font(distance_title, fonts_.text_font, 0);
    lv_label_set_text(distance_title, "距离");
    lv_obj_align(distance_title, LV_ALIGN_LEFT_MID, 4, 0);

    sport_distance_label_ = lv_label_create(distance_card);
    lv_obj_set_width(sport_distance_label_, 52);
    lv_label_set_long_mode(sport_distance_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(sport_distance_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(sport_distance_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(sport_distance_label_, "0.0");
    lv_obj_align(sport_distance_label_, LV_ALIGN_RIGHT_MID, -4, 0);
}

void OledDisplay::Fun_Page_Init(lv_obj_t* screen) {
    const lv_coord_t margin = 2;
    const lv_coord_t gap = 2;
    const lv_coord_t card_width = width_ - margin * 2;
    const lv_coord_t header_height = 14;
    const lv_coord_t preview_height = 28;
    const lv_coord_t hint_height = height_ - margin * 2 - gap * 2 - header_height - preview_height;

    fun_screen_ = lv_obj_create(screen);
    if (fun_screen_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create fun screen");
        return;
    }

    SetRootStyle(fun_screen_);
    lv_obj_set_size(fun_screen_, width_, height_);
    lv_obj_add_flag(fun_screen_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* header_card = lv_obj_create(fun_screen_);
    SetPanelStyle(header_card, false, 8, 1);
    lv_obj_set_size(header_card, card_width, header_height);
    lv_obj_set_pos(header_card, margin, margin);

    lv_obj_t* header_title = lv_label_create(header_card);
    lv_obj_set_style_text_font(header_title, fonts_.text_font, 0);
    lv_label_set_text(header_title, "娱乐");
    lv_obj_align(header_title, LV_ALIGN_LEFT_MID, 5, 0);

    fun_indicator_label_ = lv_label_create(header_card);
    lv_obj_set_style_text_font(fun_indicator_label_, fonts_.text_font, 0);
    lv_label_set_text(fun_indicator_label_, "1/6");
    lv_obj_align(fun_indicator_label_, LV_ALIGN_RIGHT_MID, -4, 0);

    fun_card_ = lv_obj_create(fun_screen_);
    SetPanelStyle(fun_card_, false, 8, 1);
    lv_obj_set_size(fun_card_, card_width, preview_height);
    lv_obj_set_pos(fun_card_, margin, margin + header_height + gap);

    fun_icon_label_ = lv_label_create(fun_card_);
    lv_obj_set_style_text_font(fun_icon_label_, fonts_.icon_font, 0);
    lv_label_set_text(fun_icon_label_, FONT_AWESOME_PLAY);
    lv_obj_align(fun_icon_label_, LV_ALIGN_LEFT_MID, 8, 0);

    fun_title_label_ = lv_label_create(fun_card_);
    lv_obj_set_width(fun_title_label_, card_width - 40);
    lv_obj_set_style_text_font(fun_title_label_, &font_puhui_16_4, 0);
    lv_obj_set_style_text_align(fun_title_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(fun_title_label_, LV_LABEL_LONG_CLIP);
    lv_label_set_text(fun_title_label_, "躲避");
    lv_obj_align(fun_title_label_, LV_ALIGN_CENTER, 8, 0);

    lv_obj_t* info_card = lv_obj_create(fun_screen_);
    SetPanelStyle(info_card, false, 8, 1);
    lv_obj_set_size(info_card, card_width, hint_height);
    lv_obj_set_pos(info_card, margin, margin + header_height + gap + preview_height + gap);

    fun_hint_label_ = lv_label_create(info_card);
    lv_obj_set_width(fun_hint_label_, card_width - 10);
    lv_obj_set_style_text_font(fun_hint_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(fun_hint_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(fun_hint_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(fun_hint_label_, "躲开方块");
    lv_obj_align(fun_hint_label_, LV_ALIGN_CENTER, 0, 0);
}

void OledDisplay::Game_Page_Init(lv_obj_t* screen) {
    const lv_coord_t margin = GetPageMargin();
    const lv_coord_t card_width = width_ - margin * 2;
    const lv_coord_t field_top = 0;
    const lv_coord_t field_height = 44;
    const lv_coord_t status_top = field_top + field_height;
    const lv_coord_t status_height = height_ - status_top;

    game_screen_ = lv_obj_create(screen);
    if (game_screen_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create game screen");
        return;
    }

    SetRootStyle(game_screen_);
    lv_obj_set_size(game_screen_, width_, height_);
    lv_obj_add_flag(game_screen_, LV_OBJ_FLAG_HIDDEN);

    game_field_ = lv_obj_create(game_screen_);
    SetPanelStyle(game_field_, false, 6, 1);
    lv_obj_set_size(game_field_, card_width, field_height);
    lv_obj_align(game_field_, LV_ALIGN_TOP_MID, 0, field_top);

    for (int i = 1; i < 4; ++i) {
        lv_obj_t* lane = lv_obj_create(game_field_);
        SetSolidBlockStyle(lane, 0);
        lv_obj_set_size(lane, 1, field_height - 4);
        lv_obj_set_pos(lane, (card_width * i) / 4, 2);
    }

    game_player_ = lv_obj_create(game_field_);
    SetSolidBlockStyle(game_player_, 1);
    lv_obj_set_size(game_player_, 16, 4);

    game_obstacle_ = lv_obj_create(game_field_);
    SetSolidBlockStyle(game_obstacle_, 1);
    lv_obj_set_size(game_obstacle_, 16, 4);

    game_status_card_ = lv_obj_create(game_screen_);
    SetPanelStyle(game_status_card_, false, 6, 1);
    lv_obj_set_size(game_status_card_, card_width, status_height);
    lv_obj_align(game_status_card_, LV_ALIGN_TOP_MID, 0, status_top);

    game_status_label_ = lv_label_create(game_status_card_);
    lv_obj_set_style_text_font(game_status_label_, fonts_.text_font, 0);
    lv_label_set_text(game_status_label_, "就绪");
    lv_obj_align(game_status_label_, LV_ALIGN_LEFT_MID, 4, 0);

    game_score_label_ = lv_label_create(game_status_card_);
    lv_obj_set_style_text_font(game_score_label_, fonts_.text_font, 0);
    lv_label_set_text(game_score_label_, "00/00");
    lv_obj_align(game_score_label_, LV_ALIGN_RIGHT_MID, -4, 0);
}

void OledDisplay::Reflex_Page_Init(lv_obj_t* screen) {
    const lv_coord_t margin = 2;
    const lv_coord_t gap = 2;
    const lv_coord_t card_width = width_ - margin * 2;
    const lv_coord_t status_top = margin;
    const lv_coord_t status_height = 15;
    const lv_coord_t value_top = status_top + status_height + gap;
    const lv_coord_t value_height = 24;
    const lv_coord_t hint_top = value_top + value_height + gap;
    const lv_coord_t hint_height = height_ - hint_top - margin;

    reflex_screen_ = lv_obj_create(screen);
    if (reflex_screen_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create reflex screen");
        return;
    }

    SetRootStyle(reflex_screen_);
    lv_obj_set_size(reflex_screen_, width_, height_);
    lv_obj_add_flag(reflex_screen_, LV_OBJ_FLAG_HIDDEN);

    reflex_status_card_ = lv_obj_create(reflex_screen_);
    SetPanelStyle(reflex_status_card_, false, 8, 1);
    lv_obj_set_size(reflex_status_card_, card_width, status_height);
    lv_obj_set_pos(reflex_status_card_, margin, status_top);

    reflex_status_label_ = lv_label_create(reflex_status_card_);
    lv_obj_set_width(reflex_status_label_, 40);
    lv_obj_set_style_text_font(reflex_status_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(reflex_status_label_, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(reflex_status_label_, LV_LABEL_LONG_CLIP);
    lv_label_set_text(reflex_status_label_, "准备");
    lv_obj_align(reflex_status_label_, LV_ALIGN_LEFT_MID, 4, 0);

    reflex_best_label_ = lv_label_create(reflex_status_card_);
    lv_obj_set_width(reflex_best_label_, 58);
    lv_obj_set_style_text_font(reflex_best_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(reflex_best_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(reflex_best_label_, LV_LABEL_LONG_CLIP);
    lv_label_set_text(reflex_best_label_, "最佳---");
    lv_obj_align(reflex_best_label_, LV_ALIGN_RIGHT_MID, -4, 0);

    lv_obj_t* value_card = lv_obj_create(reflex_screen_);
    SetPanelStyle(value_card, true, 8, 1);
    lv_obj_set_style_text_color(value_card, lv_color_white(), 0);
    lv_obj_set_size(value_card, card_width, value_height);
    lv_obj_set_pos(value_card, margin, value_top);

    reflex_value_label_ = lv_label_create(value_card);
    lv_obj_set_width(reflex_value_label_, card_width - 12);
    lv_obj_set_style_text_font(reflex_value_label_, &font_puhui_16_4, 0);
    lv_obj_set_style_text_color(reflex_value_label_, lv_color_white(), 0);
    lv_obj_set_style_text_align(reflex_value_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(reflex_value_label_, LV_LABEL_LONG_CLIP);
    lv_label_set_text(reflex_value_label_, "点击");
    lv_obj_align(reflex_value_label_, LV_ALIGN_CENTER, 0, -1);

    lv_obj_t* info_card = lv_obj_create(reflex_screen_);
    SetPanelStyle(info_card, false, 8, 1);
    lv_obj_set_size(info_card, card_width, hint_height);
    lv_obj_set_pos(info_card, margin, hint_top);

    reflex_hint_label_ = lv_label_create(info_card);
    lv_obj_set_width(reflex_hint_label_, card_width - 12);
    lv_obj_set_style_text_font(reflex_hint_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(reflex_hint_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(reflex_hint_label_, LV_LABEL_LONG_CLIP);
    lv_label_set_text(reflex_hint_label_, "点击准备");
    lv_obj_align(reflex_hint_label_, LV_ALIGN_CENTER, 0, 0);
}

void OledDisplay::Memory_Page_Init(lv_obj_t* screen) {
    const lv_coord_t margin = 2;
    const lv_coord_t gap = 2;
    const lv_coord_t card_width = width_ - margin * 2;
    const lv_coord_t status_top = margin;
    const lv_coord_t status_height = 14;
    const lv_coord_t board_top = status_top + status_height + gap;
    const lv_coord_t board_height = 28;
    const lv_coord_t hint_top = board_top + board_height + gap;
    const lv_coord_t hint_height = height_ - hint_top - margin;
    const lv_coord_t lane_width = 34;
    const lv_coord_t lane_height = 20;
    const lv_coord_t lane_y = 4;

    memory_screen_ = lv_obj_create(screen);
    if (memory_screen_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create memory screen");
        return;
    }

    SetRootStyle(memory_screen_);
    lv_obj_set_size(memory_screen_, width_, height_);
    lv_obj_add_flag(memory_screen_, LV_OBJ_FLAG_HIDDEN);

    memory_status_card_ = lv_obj_create(memory_screen_);
    SetPanelStyle(memory_status_card_, false, 8, 1);
    lv_obj_set_size(memory_status_card_, card_width, status_height);
    lv_obj_set_pos(memory_status_card_, margin, status_top);

    memory_status_label_ = lv_label_create(memory_status_card_);
    lv_obj_set_width(memory_status_label_, 36);
    lv_obj_set_style_text_font(memory_status_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(memory_status_label_, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(memory_status_label_, LV_LABEL_LONG_CLIP);
    lv_label_set_text(memory_status_label_, "开始");
    lv_obj_align(memory_status_label_, LV_ALIGN_LEFT_MID, 4, 0);

    memory_score_label_ = lv_label_create(memory_status_card_);
    lv_obj_set_width(memory_score_label_, 40);
    lv_obj_set_style_text_font(memory_score_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(memory_score_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(memory_score_label_, LV_LABEL_LONG_CLIP);
    lv_label_set_text(memory_score_label_, "00/00");
    lv_obj_align(memory_score_label_, LV_ALIGN_RIGHT_MID, -4, 0);

    memory_board_card_ = lv_obj_create(memory_screen_);
    SetPanelStyle(memory_board_card_, false, 8, 1);
    lv_obj_set_size(memory_board_card_, card_width, board_height);
    lv_obj_set_pos(memory_board_card_, margin, board_top);

    for (int i = 0; i < 3; ++i) {
        memory_lane_blocks_[i] = lv_obj_create(memory_board_card_);
        SetPanelStyle(memory_lane_blocks_[i], false, 4, 1);
        lv_obj_set_size(memory_lane_blocks_[i], lane_width, lane_height);
        lv_obj_set_pos(memory_lane_blocks_[i], 7 + i * 38, lane_y);

        memory_lane_labels_[i] = lv_label_create(memory_lane_blocks_[i]);
        lv_obj_set_style_text_font(memory_lane_labels_[i], fonts_.text_font, 0);
        lv_label_set_text(memory_lane_labels_[i], i == 0 ? "左" : (i == 1 ? "中" : "右"));
        lv_obj_center(memory_lane_labels_[i]);
    }

    lv_obj_t* hint_card = lv_obj_create(memory_screen_);
    SetPanelStyle(hint_card, false, 8, 1);
    lv_obj_set_size(hint_card, card_width, hint_height);
    lv_obj_set_pos(hint_card, margin, hint_top);

    memory_hint_label_ = lv_label_create(hint_card);
    lv_obj_set_width(memory_hint_label_, card_width - 10);
    lv_obj_set_style_text_font(memory_hint_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(memory_hint_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(memory_hint_label_, LV_LABEL_LONG_CLIP);
    lv_label_set_text(memory_hint_label_, "按下开始");
    lv_obj_align(memory_hint_label_, LV_ALIGN_CENTER, 0, 0);
}

void OledDisplay::Breathe_Page_Init(lv_obj_t* screen) {
    const lv_coord_t margin = 2;
    const lv_coord_t gap = 2;
    const lv_coord_t card_width = width_ - margin * 2;
    const lv_coord_t phase_top = margin;
    const lv_coord_t phase_height = 14;
    const lv_coord_t status_top = phase_top + phase_height + gap;
    const lv_coord_t status_height = 22;
    const lv_coord_t progress_top = status_top + status_height + gap;
    const lv_coord_t progress_height = height_ - progress_top - margin;

    breathe_screen_ = lv_obj_create(screen);
    if (breathe_screen_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create breathe screen");
        return;
    }

    SetRootStyle(breathe_screen_);
    lv_obj_set_size(breathe_screen_, width_, height_);
    lv_obj_add_flag(breathe_screen_, LV_OBJ_FLAG_HIDDEN);

    breathe_phase_card_ = lv_obj_create(breathe_screen_);
    SetPanelStyle(breathe_phase_card_, false, 8, 1);
    lv_obj_set_size(breathe_phase_card_, card_width, phase_height);
    lv_obj_set_pos(breathe_phase_card_, margin, phase_top);

    breathe_phase_label_ = lv_label_create(breathe_phase_card_);
    lv_obj_set_width(breathe_phase_label_, 64);
    lv_obj_set_style_text_font(breathe_phase_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(breathe_phase_label_, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(breathe_phase_label_, LV_LABEL_LONG_CLIP);
    lv_label_set_text(breathe_phase_label_, "准备");
    lv_obj_align(breathe_phase_label_, LV_ALIGN_LEFT_MID, 4, 0);

    breathe_cycle_label_ = lv_label_create(breathe_phase_card_);
    lv_obj_set_width(breathe_cycle_label_, 40);
    lv_obj_set_style_text_font(breathe_cycle_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(breathe_cycle_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(breathe_cycle_label_, LV_LABEL_LONG_CLIP);
    lv_label_set_text(breathe_cycle_label_, "轮00");
    lv_obj_align(breathe_cycle_label_, LV_ALIGN_RIGHT_MID, -2, 0);

    lv_obj_t* status_card = lv_obj_create(breathe_screen_);
    SetPanelStyle(status_card, false, 8, 1);
    lv_obj_set_size(status_card, card_width, status_height);
    lv_obj_set_pos(status_card, margin, status_top);

    breathe_status_label_ = lv_label_create(status_card);
    lv_obj_set_width(breathe_status_label_, card_width - 10);
    lv_obj_set_style_text_font(breathe_status_label_, &font_puhui_16_4, 0);
    lv_obj_set_style_text_align(breathe_status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(breathe_status_label_, LV_LABEL_LONG_CLIP);
    lv_label_set_text(breathe_status_label_, "节奏 4秒");
    lv_obj_align(breathe_status_label_, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t* info_card = lv_obj_create(breathe_screen_);
    SetPanelStyle(info_card, false, 8, 1);
    lv_obj_set_size(info_card, card_width, progress_height);
    lv_obj_set_pos(info_card, margin, progress_top);

    lv_obj_t* flow_tag = lv_label_create(info_card);
    lv_obj_set_style_text_font(flow_tag, fonts_.text_font, 0);
    lv_label_set_text(flow_tag, "进度");
    lv_obj_align(flow_tag, LV_ALIGN_LEFT_MID, 5, 0);

    breathe_bar_track_ = lv_obj_create(info_card);
    SetPanelStyle(breathe_bar_track_, false, 3, 1);
    lv_obj_set_style_bg_opa(breathe_bar_track_, LV_OPA_TRANSP, 0);
    lv_obj_set_size(breathe_bar_track_, card_width - 48, 6);
    lv_obj_align(breathe_bar_track_, LV_ALIGN_RIGHT_MID, -5, 0);

    breathe_bar_fill_ = lv_obj_create(breathe_bar_track_);
    SetSolidBlockStyle(breathe_bar_fill_, 2);
    lv_obj_set_size(breathe_bar_fill_, 36, 4);
    lv_obj_set_pos(breathe_bar_fill_, 1, 1);
}

void OledDisplay::Quick_Page_Init(lv_obj_t* screen) {
    const lv_coord_t margin = 2;
    const lv_coord_t gap = 2;
    const lv_coord_t card_width = width_ - margin * 2;
    const lv_coord_t card_height = (height_ - margin * 2 - gap) / 2;

    quick_screen_ = lv_obj_create(screen);
    if (quick_screen_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create quick screen");
        return;
    }

    SetRootStyle(quick_screen_);
    lv_obj_set_size(quick_screen_, width_, height_);
    lv_obj_add_flag(quick_screen_, LV_OBJ_FLAG_HIDDEN);

    quick_brightness_card_ = lv_obj_create(quick_screen_);
    SetPanelStyle(quick_brightness_card_, false, 8, 1);
    lv_obj_set_style_text_color(quick_brightness_card_, lv_color_black(), 0);
    lv_obj_set_size(quick_brightness_card_, card_width, card_height);
    lv_obj_set_pos(quick_brightness_card_, margin, margin);

    lv_obj_t* brightness_title = lv_label_create(quick_brightness_card_);
    lv_obj_set_style_text_font(brightness_title, fonts_.text_font, 0);
    lv_label_set_text(brightness_title, "亮度");
    lv_obj_align(brightness_title, LV_ALIGN_TOP_LEFT, 5, 2);

    quick_brightness_value_ = lv_label_create(quick_brightness_card_);
    lv_obj_set_width(quick_brightness_value_, 34);
    lv_obj_set_style_text_font(quick_brightness_value_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(quick_brightness_value_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(quick_brightness_value_, LV_LABEL_LONG_CLIP);
    lv_label_set_text(quick_brightness_value_, "75%");
    lv_obj_align(quick_brightness_value_, LV_ALIGN_TOP_RIGHT, -5, 2);

    quick_brightness_bar_track_ = lv_obj_create(quick_brightness_card_);
    SetPanelStyle(quick_brightness_bar_track_, false, 3, 1);
    lv_obj_set_style_bg_opa(quick_brightness_bar_track_, LV_OPA_TRANSP, 0);
    lv_obj_set_size(quick_brightness_bar_track_, card_width - 12, 5);
    lv_obj_align(quick_brightness_bar_track_, LV_ALIGN_BOTTOM_MID, 0, -4);

    quick_brightness_bar_fill_ = lv_obj_create(quick_brightness_bar_track_);
    SetSolidBlockStyle(quick_brightness_bar_fill_, 2);
    lv_obj_set_size(quick_brightness_bar_fill_, 56, 3);
    lv_obj_set_pos(quick_brightness_bar_fill_, 1, 1);

    quick_volume_card_ = lv_obj_create(quick_screen_);
    SetPanelStyle(quick_volume_card_, false, 8, 1);
    lv_obj_set_style_text_color(quick_volume_card_, lv_color_black(), 0);
    lv_obj_set_size(quick_volume_card_, card_width, card_height);
    lv_obj_set_pos(quick_volume_card_, margin, margin + card_height + gap);

    lv_obj_t* volume_title = lv_label_create(quick_volume_card_);
    lv_obj_set_style_text_font(volume_title, fonts_.text_font, 0);
    lv_label_set_text(volume_title, "音量");
    lv_obj_align(volume_title, LV_ALIGN_TOP_LEFT, 5, 2);

    quick_volume_value_ = lv_label_create(quick_volume_card_);
    lv_obj_set_width(quick_volume_value_, 34);
    lv_obj_set_style_text_font(quick_volume_value_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(quick_volume_value_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(quick_volume_value_, LV_LABEL_LONG_CLIP);
    lv_label_set_text(quick_volume_value_, "100%");
    lv_obj_align(quick_volume_value_, LV_ALIGN_TOP_RIGHT, -5, 2);

    quick_volume_bar_track_ = lv_obj_create(quick_volume_card_);
    SetPanelStyle(quick_volume_bar_track_, false, 3, 1);
    lv_obj_set_style_bg_opa(quick_volume_bar_track_, LV_OPA_TRANSP, 0);
    lv_obj_set_size(quick_volume_bar_track_, card_width - 12, 5);
    lv_obj_align(quick_volume_bar_track_, LV_ALIGN_BOTTOM_MID, 0, -4);

    quick_volume_bar_fill_ = lv_obj_create(quick_volume_bar_track_);
    SetSolidBlockStyle(quick_volume_bar_fill_, 2);
    lv_obj_set_size(quick_volume_bar_fill_, 60, 3);
    lv_obj_set_pos(quick_volume_bar_fill_, 1, 1);
}

void OledDisplay::Sound_Page_Init(lv_obj_t* screen) {
    const lv_coord_t margin = 2;
    const lv_coord_t gap = 2;
    const lv_coord_t card_width = width_ - margin * 2;
    const lv_coord_t header_height = 18;
    const lv_coord_t name_top = header_height + gap;
    const lv_coord_t name_height = 18;
    const lv_coord_t info_top = name_top + name_height + gap;
    const lv_coord_t info_height = height_ - info_top - margin;

    sound_screen_ = lv_obj_create(screen);
    if (sound_screen_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create sound screen");
        return;
    }

    SetRootStyle(sound_screen_);
    lv_obj_set_size(sound_screen_, width_, height_);
    lv_obj_add_flag(sound_screen_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* header = lv_obj_create(sound_screen_);
    SetPanelStyle(header, true, 0, 0);
    lv_obj_set_size(header, width_, header_height);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* header_icon = lv_label_create(header);
    lv_obj_set_style_text_font(header_icon, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(header_icon, lv_color_white(), 0);
    lv_label_set_text(header_icon, FONT_AWESOME_MUSIC);
    lv_obj_align(header_icon, LV_ALIGN_LEFT_MID, 4, 0);

    lv_obj_t* header_title = lv_label_create(header);
    lv_obj_set_style_text_font(header_title, fonts_.text_font, 0);
    lv_obj_set_style_text_color(header_title, lv_color_white(), 0);
    lv_label_set_text(header_title, "音乐");
    lv_obj_align(header_title, LV_ALIGN_LEFT_MID, 20, 0);

    sound_indicator_label_ = lv_label_create(header);
    lv_obj_set_width(sound_indicator_label_, 40);
    lv_obj_set_style_text_font(sound_indicator_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_color(sound_indicator_label_, lv_color_white(), 0);
    lv_obj_set_style_text_align(sound_indicator_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(sound_indicator_label_, LV_LABEL_LONG_CLIP);
    lv_label_set_text(sound_indicator_label_, "1/10");
    lv_obj_align(sound_indicator_label_, LV_ALIGN_RIGHT_MID, -4, 0);

    lv_obj_t* name_card = lv_obj_create(sound_screen_);
    SetPanelStyle(name_card, false, 8, 1);
    lv_obj_set_size(name_card, card_width, name_height);
    lv_obj_set_pos(name_card, margin, name_top);

    sound_name_label_ = lv_label_create(name_card);
    lv_obj_set_width(sound_name_label_, card_width - 10);
    lv_obj_set_style_text_font(sound_name_label_, &font_puhui_16_4, 0);
    lv_obj_set_style_text_color(sound_name_label_, lv_color_black(), 0);
    lv_obj_set_style_text_align(sound_name_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(sound_name_label_, LV_LABEL_LONG_CLIP);
    lv_label_set_text(sound_name_label_, "音乐1");
    lv_obj_align(sound_name_label_, LV_ALIGN_CENTER, 0, -1);

    sound_state_label_ = nullptr;
    sound_mode_label_ = nullptr;

    lv_obj_t* info_card = lv_obj_create(sound_screen_);
    SetPanelStyle(info_card, false, 8, 1);
    lv_obj_set_size(info_card, card_width, info_height);
    lv_obj_set_pos(info_card, margin, info_top);

    sound_desc_label_ = lv_label_create(info_card);
    lv_obj_set_width(sound_desc_label_, card_width - 12);
    lv_obj_set_style_text_font(sound_desc_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(sound_desc_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(sound_desc_label_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(sound_desc_label_, "歌曲 1");
    lv_obj_align(sound_desc_label_, LV_ALIGN_CENTER, 0, 0);

    sound_position_track_ = nullptr;
    sound_position_fill_ = nullptr;
}

void OledDisplay::Time_Setting_Page_Init(lv_obj_t* screen) {
    const lv_coord_t margin = 2;
    const lv_coord_t gap = 2;
    const lv_coord_t card_width = width_ - margin * 2;
    const lv_coord_t date_height = 20;
    const lv_coord_t time_height = 22;
    const lv_coord_t weekday_height = 16;
    const lv_coord_t date_y = margin;
    const lv_coord_t time_y = date_y + date_height + gap;
    const lv_coord_t weekday_y = time_y + time_height + gap;

    time_setting_screen_ = lv_obj_create(screen);
    if (time_setting_screen_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create time setting screen");
        return;
    }

    SetRootStyle(time_setting_screen_);
    lv_obj_set_size(time_setting_screen_, width_, height_);
    lv_obj_add_flag(time_setting_screen_, LV_OBJ_FLAG_HIDDEN);

    // 日期卡片（年-月-日）
    lv_obj_t* date_card = lv_obj_create(time_setting_screen_);
    SetPanelStyle(date_card, false, 8, 1);
    lv_obj_set_size(date_card, card_width, date_height);
    lv_obj_set_pos(date_card, margin, date_y);

    lv_obj_t* date_container = lv_obj_create(date_card);
    SetRootStyle(date_container);
    lv_obj_set_size(date_container, card_width - 4, 18);
    lv_obj_set_layout(date_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(date_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(date_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(date_container, 2, 0);
    lv_obj_center(date_container);

    time_setting_year_label_ = lv_label_create(date_container);
    lv_label_set_text(time_setting_year_label_, "2026");
    lv_obj_set_size(time_setting_year_label_, 38, 18);
    lv_obj_set_style_text_font(time_setting_year_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(time_setting_year_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(time_setting_year_label_, lv_color_black(), 0);
    lv_obj_set_style_border_width(time_setting_year_label_, 1, 0);
    lv_obj_set_style_border_color(time_setting_year_label_, lv_color_black(), 0);
    lv_obj_set_style_radius(time_setting_year_label_, 4, 0);
    lv_obj_set_style_pad_all(time_setting_year_label_, 1, 0);
    lv_obj_set_style_bg_color(time_setting_year_label_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(time_setting_year_label_, LV_OPA_COVER, 0);

    lv_obj_t* dash1 = lv_label_create(date_container);
    lv_obj_set_style_text_font(dash1, fonts_.text_font, 0);
    lv_label_set_text(dash1, "-");

    time_setting_month_label_ = lv_label_create(date_container);
    lv_label_set_text(time_setting_month_label_, "01");
    lv_obj_set_size(time_setting_month_label_, 24, 18);
    lv_obj_set_style_text_font(time_setting_month_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(time_setting_month_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(time_setting_month_label_, lv_color_black(), 0);
    lv_obj_set_style_border_width(time_setting_month_label_, 1, 0);
    lv_obj_set_style_border_color(time_setting_month_label_, lv_color_black(), 0);
    lv_obj_set_style_radius(time_setting_month_label_, 4, 0);
    lv_obj_set_style_pad_all(time_setting_month_label_, 1, 0);
    lv_obj_set_style_bg_color(time_setting_month_label_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(time_setting_month_label_, LV_OPA_COVER, 0);

    lv_obj_t* dash2 = lv_label_create(date_container);
    lv_obj_set_style_text_font(dash2, fonts_.text_font, 0);
    lv_label_set_text(dash2, "-");

    time_setting_day_label_ = lv_label_create(date_container);
    lv_label_set_text(time_setting_day_label_, "01");
    lv_obj_set_size(time_setting_day_label_, 24, 18);
    lv_obj_set_style_text_font(time_setting_day_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(time_setting_day_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(time_setting_day_label_, lv_color_black(), 0);
    lv_obj_set_style_border_width(time_setting_day_label_, 1, 0);
    lv_obj_set_style_border_color(time_setting_day_label_, lv_color_black(), 0);
    lv_obj_set_style_radius(time_setting_day_label_, 4, 0);
    lv_obj_set_style_pad_all(time_setting_day_label_, 1, 0);
    lv_obj_set_style_bg_color(time_setting_day_label_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(time_setting_day_label_, LV_OPA_COVER, 0);

    // 时间卡片（时:分:秒）
    lv_obj_t* time_card = lv_obj_create(time_setting_screen_);
    SetPanelStyle(time_card, false, 8, 1);
    lv_obj_set_size(time_card, card_width, time_height);
    lv_obj_set_pos(time_card, margin, time_y);

    lv_obj_t* time_container = lv_obj_create(time_card);
    SetRootStyle(time_container);
    lv_obj_set_size(time_container, 90, 18);
    lv_obj_set_layout(time_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(time_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(time_container, 2, 0);
    lv_obj_center(time_container);

    time_setting_hour_label_ = lv_label_create(time_container);
    lv_label_set_text(time_setting_hour_label_, "12");
    lv_obj_set_size(time_setting_hour_label_, 22, 18);
    lv_obj_set_style_text_font(time_setting_hour_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(time_setting_hour_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(time_setting_hour_label_, lv_color_black(), 0);
    lv_obj_set_style_border_width(time_setting_hour_label_, 1, 0);
    lv_obj_set_style_border_color(time_setting_hour_label_, lv_color_black(), 0);
    lv_obj_set_style_radius(time_setting_hour_label_, 4, 0);
    lv_obj_set_style_pad_all(time_setting_hour_label_, 1, 0);
    lv_obj_set_style_bg_color(time_setting_hour_label_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(time_setting_hour_label_, LV_OPA_COVER, 0);

    lv_obj_t* colon1 = lv_label_create(time_container);
    lv_obj_set_style_text_font(colon1, fonts_.text_font, 0);
    lv_label_set_text(colon1, ":");

    time_setting_minute_label_ = lv_label_create(time_container);
    lv_label_set_text(time_setting_minute_label_, "00");
    lv_obj_set_size(time_setting_minute_label_, 22, 18);
    lv_obj_set_style_text_font(time_setting_minute_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(time_setting_minute_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(time_setting_minute_label_, lv_color_black(), 0);
    lv_obj_set_style_border_width(time_setting_minute_label_, 1, 0);
    lv_obj_set_style_border_color(time_setting_minute_label_, lv_color_black(), 0);
    lv_obj_set_style_radius(time_setting_minute_label_, 4, 0);
    lv_obj_set_style_pad_all(time_setting_minute_label_, 1, 0);
    lv_obj_set_style_bg_color(time_setting_minute_label_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(time_setting_minute_label_, LV_OPA_COVER, 0);

    lv_obj_t* colon2 = lv_label_create(time_container);
    lv_obj_set_style_text_font(colon2, fonts_.text_font, 0);
    lv_label_set_text(colon2, ":");

    time_setting_second_label_ = lv_label_create(time_container);
    lv_label_set_text(time_setting_second_label_, "00");
    lv_obj_set_size(time_setting_second_label_, 22, 18);
    lv_obj_set_style_text_font(time_setting_second_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_align(time_setting_second_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(time_setting_second_label_, lv_color_black(), 0);
    lv_obj_set_style_border_width(time_setting_second_label_, 1, 0);
    lv_obj_set_style_border_color(time_setting_second_label_, lv_color_black(), 0);
    lv_obj_set_style_radius(time_setting_second_label_, 4, 0);
    lv_obj_set_style_pad_all(time_setting_second_label_, 1, 0);
    lv_obj_set_style_bg_color(time_setting_second_label_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(time_setting_second_label_, LV_OPA_COVER, 0);

    // 星期卡片
    lv_obj_t* weekday_card = lv_obj_create(time_setting_screen_);
    SetPanelStyle(weekday_card, false, 8, 1);
    lv_obj_set_size(weekday_card, card_width, weekday_height);
    lv_obj_set_pos(weekday_card, margin, weekday_y);

    time_setting_weekday_label_ = lv_label_create(weekday_card);
    lv_label_set_text(time_setting_weekday_label_, "星期一");
    lv_obj_set_style_text_font(time_setting_weekday_label_, fonts_.text_font, 0);
    lv_obj_set_style_text_color(time_setting_weekday_label_, lv_color_black(), 0);
    lv_obj_center(time_setting_weekday_label_);
}
