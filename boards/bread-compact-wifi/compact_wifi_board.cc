#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "system_reset.h"
#include "app/application.h"
#include "button.h"
#include "config.h"
#include "services/mcp_server.h"
#include "led/single_led.h"
#include "sensors/mpu6050/mpu6050.h"
#include "sensors/paj7620/paj7620.h"
#include "assets/lang_config.h"
#include <wifi_station.h>
#include <esp_log.h>
#include <esp_err.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <algorithm>
#include <cctype>
#include <array>
#include <cstdio>

#define UTF8_LIT(s) reinterpret_cast<const char*>(u8##s)

#ifdef SH1106
#include <esp_lcd_panel_sh1106.h>
#endif

#define TAG "CompactWifiBoard"
#define OLED_CMD_SET_CONTRAST 0x81

LV_FONT_DECLARE(font_puhui_14_1);
LV_FONT_DECLARE(font_awesome_14_1);
LV_FONT_DECLARE(font_puhui_30_4);

class OledContrastBacklight : public Backlight {
public:
    OledContrastBacklight(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel)
        : panel_io_(panel_io), panel_(panel) {}

protected:
    void SetBrightnessImpl(uint8_t brightness) override {
        if (panel_io_ == nullptr || panel_ == nullptr) {
            return;
        }

        if (brightness == 0) {
            esp_lcd_panel_disp_on_off(panel_, false);
            return;
        }

        esp_lcd_panel_disp_on_off(panel_, true);
        uint8_t contrast = static_cast<uint8_t>((brightness * 255) / 100);
        if (contrast == 0) {
            contrast = 1;
        }
        esp_lcd_panel_io_tx_param(panel_io_, OLED_CMD_SET_CONTRAST, &contrast, 1);
    }

private:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
};

class CompactWifiBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    Backlight* backlight_ = nullptr;
    Button boot_button_;
    Button touch_button_;
    Button switch_button_;
    Button page_button_;
    SingleLed* flashlight_led_ = nullptr;
    bool flashlight_on_ = false;
    
    // 手电筒颜色索引
    int current_color_index_ = 0;

    // 秒表状态：0 = 重置，1 = 运行中，2 = 已暂停
    int stopwatch_state = 0;
    struct Color {
        uint8_t r, g, b;
        const char* name;
    };
    Color colors_[8] = {
        {255, 255, 255, UTF8_LIT("\u767d\u8272")},
        {255, 0, 0, UTF8_LIT("\u7ea2\u8272")},
        {255, 165, 0, UTF8_LIT("\u6a59\u8272")},
        {255, 255, 0, UTF8_LIT("\u9ec4\u8272")},
        {0, 255, 0, UTF8_LIT("\u7eff\u8272")},
        {0, 0, 255, UTF8_LIT("\u84dd\u8272")},
        {75, 0, 130, UTF8_LIT("\u975b\u8272")},
        {238, 130, 238, UTF8_LIT("\u7d2b\u8272")}
    };
    int color_count_ = 8;
    MAX30102& max30102 = MAX30102::GetInstance(MAX30102_I2C_SDA_PIN, MAX30102_I2C_SCL_PIN, GPIO_NUM_NC,MAX30102_I2C_PORT, MAX30102_I2C_ADDR);
    MPU6050& mpu6050_ = MPU6050::GetInstance(MPU6050_SDA_PIN, MPU6050_SCL_PIN);
    Paj7620& paj7620_ = Paj7620::GetInstance(PAJ7620_SDA_PIN, PAJ7620_SCL_PIN, PAJ7620_INT_PIN);

    static std::string NormalizeColorName(std::string color) {
        std::transform(color.begin(), color.end(), color.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        color.erase(std::remove_if(color.begin(), color.end(), [](unsigned char c) {
            return std::isspace(c) || c == '-' || c == '_';
        }), color.end());
        return color;
    }

    bool SetFlashlightColorByName(const std::string& color_name) {
        struct ColorAlias {
            const char* alias;
            int index;
        };
        static const ColorAlias aliases[] = {
            {"white", 0}, {"warmwhite", 0}, {"coldwhite", 0}, {"red", 1},
            {"orange", 2}, {"yellow", 3}, {"green", 4}, {"blue", 5},
            {"indigo", 6}, {"violet", 7}, {"purple", 7},
            {"baise", 0}, {"hongse", 1}, {"chengse", 2}, {"huangse", 3},
            {"lvse", 4}, {"lanse", 5}, {"dianse", 6}, {"zise", 7},
            {UTF8_LIT("\u767d\u8272"), 0}, {UTF8_LIT("\u7ea2\u8272"), 1},
            {UTF8_LIT("\u6a59\u8272"), 2}, {UTF8_LIT("\u9ec4\u8272"), 3},
            {UTF8_LIT("\u7eff\u8272"), 4}, {UTF8_LIT("\u84dd\u8272"), 5},
            {UTF8_LIT("\u975b\u8272"), 6}, {UTF8_LIT("\u7d2b\u8272"), 7},
        };

        auto normalized = NormalizeColorName(color_name);
        for (const auto& alias : aliases) {
            if (normalized == NormalizeColorName(alias.alias)) {
                current_color_index_ = alias.index;
                auto& color = colors_[current_color_index_];
                if (flashlight_led_) {
                    flashlight_led_->SetColor(color.r, color.g, color.b);
                }
                TurnOnFlashlight();
                UpdateFlashlightDisplayText();
                return true;
            }
        }
        return false;
    }

    void UpdateFlashlightDisplayText() {
        char text[48];
        snprintf(text, sizeof(text), "%s %s", UTF8_LIT("\u989c\u8272"), colors_[current_color_index_].name);
        GetDisplay()->SetFlashlightText(text);
    }

    void AdjustOutputVolume(int delta) {
        auto codec = GetAudioCodec();
        int volume = codec->output_volume() + delta;
        char volume_text[16];
        if (volume > 100) {
            volume = 100;
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        } else if (volume < 0) {
            volume = 0;
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        } else {
            snprintf(volume_text, sizeof(volume_text), "音量 %d%%", volume);
            GetDisplay()->ShowNotification(volume_text);
        }
        codec->SetOutputVolume(volume);
    }

    void CycleFlashlightColor(int delta) {
        if (!flashlight_led_) {
            return;
        }
        current_color_index_ = (current_color_index_ + delta + color_count_) % color_count_;
        Color& color = colors_[current_color_index_];
        flashlight_led_->SetColor(color.r, color.g, color.b);
        TurnOnFlashlight();
        UpdateFlashlightDisplayText();
    }

    void HandleAlarmSettingClick() {
        auto display = GetDisplay();
        if (display->GetCurrentPage() == PAGE_NAOZHONG && alarm_set_state == 3) {
            Application::GetInstance().GetAudioService().StopPlayback();
        }
        if (alarm_set_state < 0 || alarm_set_state == 4) {
            alarm_set_state = 1;
        } else if (alarm_set_state == 1) {
            alarm_set_state = 0;
        } else if (alarm_set_state == 0) {
            alarm_set_state = 2;
        } else if (alarm_set_state == 2) {
            alarm_set_state = 3;
        } else if (alarm_set_state == 3) {
            alarm_set_state = 4;
        } else {
            alarm_set_state = 1;
        }

        char state_str[32];
        switch (alarm_set_state) {
            case 0:
                sprintf(state_str, "璁剧疆灏忔椂: %02d", alarm_hours);
                break;
            case 1:
                sprintf(state_str, "璁剧疆鍒嗛挓: %02d", alarm_minutes);
                break;
            case 3:
                sprintf(state_str, "閾冨０閫夋嫨");
                break;
            case 4:
                sprintf(state_str, "寮€鍚?鍏抽棴");
                break;
            default:
                sprintf(state_str, "璁剧疆绉? %02d", alarm_seconds);
                break;
        }
        display->ShowNotification(state_str, 2000);
        display->SetAlarmButtonHighlight(0);
        display->SetAlarmSetState(alarm_set_state);
    }

    void HandleAlarmSettingLongPress() {
        auto display = GetDisplay();
        display->SetAlarmTime(alarm_hours, alarm_minutes, alarm_seconds);
        char time_str[32];
        sprintf(time_str, "闂归挓璁剧疆涓? %02d:%02d:%02d", alarm_hours, alarm_minutes, alarm_seconds);
        display->ShowNotification(time_str, 3000);
        alarm_set_state = 4;
        display->SetAlarmSetState(-1);
    }

    void HandleBootButtonClick() {
        auto display = GetDisplay();
        if (display->GetCurrentPage() == PAGE_FUN) {
            display->Return_Switch_Page();
            return;
        }

        if (display->GetCurrentPage() == PAGE_GAME || display->GetCurrentPage() == PAGE_REFLEX ||
            display->GetCurrentPage() == PAGE_MEMORY || display->GetCurrentPage() == PAGE_BREATHE) {
            display->Switch_Fun_Page();
            return;
        }

        if (display->GetCurrentPage() == PAGE_SOUND) {
            display->Return_Switch_Page();
            return;
        }

        if (display->GetCurrentPage() == PAGE_TIME_SETTING) {
            display->Return_Switch_Page();
            return;
        }

        if (display->GetCurrentPage() == PAGE_QUICK) {
            display->Return_Switch_Page();
            return;
        }

        if (display->GetCurrentPage() == PAGE_NAOZHONG) {
            display->StopSoundPreviewPlayback();
            alarm_set_state = -1;
            display->SetAlarmSetState(-1);
            display->Return_Switch_Page();
            return;
        }

        if (display->GetCurrentPage() == PAGE_SWITCH) {
            display->Switch_Main_Page();
            return;
        }

        // 除主页外，其余页面统一执行返回
        if (display->GetCurrentPage() != PAGE_MAIN) {
            display->Return_Switch_Page();
            return;
        }
        display->Return_Switch_Page();
    }

    void HandleTouchButtonClick() {
        auto display = GetDisplay();
        if (display->GetCurrentPage() == PAGE_INIT) {
            return;
        }
        if (display->GetCurrentPage() == PAGE_NAOZHONG && suppress_touch_alarm_click_) {
            suppress_touch_alarm_click_ = false;
            return;
        }
        if (display->GetCurrentPage() == PAGE_MAIN) {
            display->Switch_Dialogue_Page();
        } else if (display->GetCurrentPage() == PAGE_SWITCH) {
            display->prev();
        } else if (display->GetCurrentPage() == PAGE_FUN) {
            display->FunPagePrev();
        } else if (display->GetCurrentPage() == PAGE_GAME) {
            display->GameMove(-1);
        } else if (display->GetCurrentPage() == PAGE_REFLEX) {
            display->ReflexTap();
        } else if (display->GetCurrentPage() == PAGE_MEMORY) {
            display->MemoryTap(0);
        } else if (display->GetCurrentPage() == PAGE_BREATHE) {
            display->BreatheAdjust(-1);
        } else if (display->GetCurrentPage() == PAGE_QUICK) {
            display->QuickPageAdjust(-10);
        } else if (display->GetCurrentPage() == PAGE_SOUND) {
            display->SoundPagePrev();
        } else if (display->GetCurrentPage() == PAGE_TIME_SETTING) {
            display->TimeSettingAdjust(-1);
        } else if (display->GetCurrentPage() == PAGE_MIAOBIAO) {
            display->StartStopwatch();
            stopwatch_state = 1;
        } else if (display->GetCurrentPage() == PAGE_NAOZHONG) {
            if (alarm_set_state == 0) {
                alarm_hours = (alarm_hours + 1) % 24;
                char state_str[32];
                sprintf(state_str, "璁剧疆灏忔椂: %02d", alarm_hours);
                display->ShowNotification(state_str, 1000);
                display->SetAlarmTime(alarm_hours, alarm_minutes, alarm_seconds);
            } else if (alarm_set_state == 1) {
                alarm_minutes = (alarm_minutes + 1) % 60;
                char state_str[32];
                sprintf(state_str, "璁剧疆鍒嗛挓: %02d", alarm_minutes);
                display->ShowNotification(state_str, 1000);
                display->SetAlarmTime(alarm_hours, alarm_minutes, alarm_seconds);
            } else if (alarm_set_state == 2) {
                alarm_seconds = (alarm_seconds + 1) % 60;
                char state_str[32];
                sprintf(state_str, "璁剧疆绉? %02d", alarm_seconds);
                display->ShowNotification(state_str, 1000);
                display->SetAlarmTime(alarm_hours, alarm_minutes, alarm_seconds);
            } else if (alarm_set_state == 3) {
                display->AdjustAlarmMusic(1);
            } else if (alarm_set_state == 4) {
                display->StartAlarm();
            }
        } else {
            Application::GetInstance().ToggleChatState();
        }
    }

    void HandleTouchButtonPressDown() {
        auto display = GetDisplay();
        if (display->GetCurrentPage() == PAGE_MAIN) {
            return;
        }
        if (display->GetCurrentPage() == PAGE_NAOZHONG) {
            StartAlarmAdjustPress(1, 1);
            return;
        }
        Application::GetInstance().StartListening();
    }

    void HandleTouchButtonPressUp() {
        auto display = GetDisplay();
        if (display->GetCurrentPage() == PAGE_MAIN) {
            return;
        }
        if (display->GetCurrentPage() == PAGE_NAOZHONG) {
            StopAlarmAdjustPress(1);
            return;
        }
        Application::GetInstance().StopListening();
    }

    void HandleSwitchButtonClick() {
        auto display = GetDisplay();
        if (display->GetCurrentPage() == PAGE_INIT) {
            return;
        }
        if (display->GetCurrentPage() == PAGE_NAOZHONG && suppress_switch_alarm_click_) {
            suppress_switch_alarm_click_ = false;
            return;
        }
        if (display->GetCurrentPage() == PAGE_MAIN) {
            display->Switch_Quick_Page();
        } else if (display->GetCurrentPage() == PAGE_SWITCH) {
            display->next();
        } else if (display->GetCurrentPage() == PAGE_FUN) {
            display->FunPageNext();
        } else if (display->GetCurrentPage() == PAGE_GAME) {
            display->GameMove(1);
        } else if (display->GetCurrentPage() == PAGE_REFLEX) {
            display->ReflexTap();
        } else if (display->GetCurrentPage() == PAGE_MEMORY) {
            display->MemoryTap(2);
        } else if (display->GetCurrentPage() == PAGE_BREATHE) {
            display->BreatheAdjust(1);
        } else if (display->GetCurrentPage() == PAGE_QUICK) {
            display->QuickPageAdjust(10);
        } else if (display->GetCurrentPage() == PAGE_SOUND) {
            display->SoundPageNext();
        } else if (display->GetCurrentPage() == PAGE_TIME_SETTING) {
            display->TimeSettingAdjust(1);
        } else if (display->GetCurrentPage() == PAGE_FLASHLIGHT) {
            CycleFlashlightColor(1);
        } else if (display->GetCurrentPage() == PAGE_MIAOBIAO) {
            display->PauseStopwatch();
            stopwatch_state = 2;
        } else if (display->GetCurrentPage() == PAGE_NAOZHONG) {
            if (alarm_set_state == 0) {
                alarm_hours = (alarm_hours - 1 + 24) % 24;
                char state_str[32];
                sprintf(state_str, "璁剧疆灏忔椂: %02d", alarm_hours);
                display->ShowNotification(state_str, 1000);
                display->SetAlarmTime(alarm_hours, alarm_minutes, alarm_seconds);
            } else if (alarm_set_state == 1) {
                alarm_minutes = (alarm_minutes - 1 + 60) % 60;
                char state_str[32];
                sprintf(state_str, "璁剧疆鍒嗛挓: %02d", alarm_minutes);
                display->ShowNotification(state_str, 1000);
                display->SetAlarmTime(alarm_hours, alarm_minutes, alarm_seconds);
            } else if (alarm_set_state == 2) {
                alarm_seconds = (alarm_seconds - 1 + 60) % 60;
                char state_str[32];
                sprintf(state_str, "璁剧疆绉? %02d", alarm_seconds);
                display->ShowNotification(state_str, 1000);
                display->SetAlarmTime(alarm_hours, alarm_minutes, alarm_seconds);
            } else if (alarm_set_state == 3) {
                display->AdjustAlarmMusic(-1);
            } else if (alarm_set_state == 4) {
                display->StopAlarm();
            }
        } else {
            AdjustOutputVolume(10);
        }
    }

    void HandleBootButtonLongPress() {
        auto display = GetDisplay();
        if (display->GetCurrentPage() == PAGE_NAOZHONG) {
            display->SetAlarmSetState(-1);
            alarm_set_state = -1;
            display->Return_Switch_Page();
        }
    }

    void HandleSwitchButtonDoubleClick() {
        auto display = GetDisplay();
        if (display->GetCurrentPage() == PAGE_SOUND) {
            display->SoundPageToggleAuto();
            return;
        }
        AdjustOutputVolume(-10);
    }

    void HandleSwitchButtonPressDown() {
        auto display = GetDisplay();
        if (display->GetCurrentPage() == PAGE_NAOZHONG) {
            StartAlarmAdjustPress(-1, 2);
        }
    }

    void HandleSwitchButtonPressUp() {
        auto display = GetDisplay();
        if (display->GetCurrentPage() == PAGE_NAOZHONG) {
            StopAlarmAdjustPress(2);
        }
    }

    void HandlePageButtonClick() {
        auto display = GetDisplay();
        if (display->GetCurrentPage() == PAGE_INIT) {
            return;
        }
        if (display->GetCurrentPage() == PAGE_SWITCH) {
            if (display->GetCurrentIndex() == Switch_MAIN) {
                display->Switch_Main_Page();
            } else if (display->GetCurrentIndex() == Switch_CHAT) {
                display->Switch_Dialogue_Page();
            } else if (display->GetCurrentIndex() == Switch_DETE) {
                display->Switch_Health_Check_Page();
            } else if (display->GetCurrentIndex() == Switch_WEATHER) {
                display->Switch_Weather_Page();
            } else if (display->GetCurrentIndex() == Switch_FLASHLIGHT) {
                display->Switch_Flashlight_Page();
            } else if (display->GetCurrentIndex() == Switch_MIAOBIAO) {
                display->Switch_Miaobiao_Page();
            } else if (display->GetCurrentIndex() == Switch_NAOZHONG) {
                alarm_set_state = -1;
                display->SetAlarmSetState(-1);
                display->SetAlarmTime(alarm_hours, alarm_minutes, alarm_seconds);
                display->Switch_Naozhong_Page();
            } else if (display->GetCurrentIndex() == Switch_MPU6050) {
                display->Switch_Mpu6050_Page();
            } else if (display->GetCurrentIndex() == Switch_SPORT) {
                display->Switch_Sport_Page();
            } else if (display->GetCurrentIndex() == Switch_SOUND) {
                display->Switch_Sound_Page();
            } else if (display->GetCurrentIndex() == Switch_QUICK) {
                display->Switch_Quick_Page();
            } else if (display->GetCurrentIndex() == Switch_FUN) {
                display->Switch_Fun_Page();
            } else if (display->GetCurrentIndex() == Switch_TIME_SETTING) {
                display->Switch_Time_Setting_Page();
            } else if (display->GetCurrentIndex() == Switch_RECONFIG) {
                display->Switch_Reconfig_Page();
            } else if (display->GetCurrentIndex() == Switch_ABOUT) {
                display->Switch_About_Page();
            }
        } else if (display->GetCurrentPage() == PAGE_MIAOBIAO) {
            display->ResetStopwatch();
            stopwatch_state = 0;
        } else if (display->GetCurrentPage() == PAGE_NAOZHONG) {
            HandleAlarmSettingClick();
        } else if (display->GetCurrentPage() == PAGE_RECONFIG) {
            // 重新配网
            Board::GetInstance().ResetWifiConfiguration();
        } else if (display->GetCurrentPage() == PAGE_FUN) {
            display->FunPageEnter();
        } else if (display->GetCurrentPage() == PAGE_GAME) {
            display->GameToggle();
        } else if (display->GetCurrentPage() == PAGE_REFLEX) {
            display->ReflexTap();
        } else if (display->GetCurrentPage() == PAGE_MEMORY) {
            display->MemoryTap(1);
        } else if (display->GetCurrentPage() == PAGE_BREATHE) {
            display->BreatheToggle();
        } else if (display->GetCurrentPage() == PAGE_QUICK) {
            display->QuickPageToggleSelection();
        } else if (display->GetCurrentPage() == PAGE_SOUND) {
            display->SoundPagePlay();
        } else if (display->GetCurrentPage() == PAGE_TIME_SETTING) {
            display->TimeSettingNextField();
        }
    }

    void HandlePageButtonLongPress() {
        auto display = GetDisplay();
        if (display->GetCurrentPage() == PAGE_NAOZHONG) {
            HandleAlarmSettingLongPress();
        }
    }

    void HandleGestureAction(Paj7620Gesture gesture) {
        auto display = GetDisplay();
        switch (gesture) {
            case Paj7620Gesture::kLeft:
                // 躲避游戏单独处理，其余走通用逻辑
                if (display->GetCurrentPage() == PAGE_GAME) {
                    display->GameMove(-1);
                } else if (display->GetCurrentPage() == PAGE_MAIN) {
                    AdjustOutputVolume(10);
                } else {
                    HandleSwitchButtonClick();
                }
                break;
            case Paj7620Gesture::kRight:
                if (display->GetCurrentPage() == PAGE_GAME) {
                    display->GameMove(1);
                } else if (display->GetCurrentPage() == PAGE_MAIN) {
                    AdjustOutputVolume(-10);
                } else {
                    HandleTouchButtonClick();
                }
                break;
            case Paj7620Gesture::kUp:
                HandlePageButtonClick();
                break;
            case Paj7620Gesture::kDown:
                HandleBootButtonClick();
                break;
            case Paj7620Gesture::kForward:
                if (display->GetCurrentPage() != PAGE_MAIN) {
                    display->Return_Switch_Page();
                }
                break;
            case Paj7620Gesture::kBackward:
                // 不做任何处理
                break;
            case Paj7620Gesture::kClockwise:
                if (display->GetCurrentPage() != PAGE_MAIN) {
                    display->Switch_Dialogue_Page();
                }
                break;
            case Paj7620Gesture::kCounterClockwise:
                if (display->GetCurrentPage() != PAGE_MAIN) {
                    alarm_set_state = -1;
                    display->SetAlarmSetState(-1);
                    display->SetAlarmTime(alarm_hours, alarm_minutes, alarm_seconds);
                    display->Switch_Naozhong_Page();
                }
                break;
            case Paj7620Gesture::kWave:
                Application::GetInstance().ToggleChatState();
                break;
            case Paj7620Gesture::kNone:
            default:
                break;
        }
    }

    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }

    void InitializeSsd1306Display() {
        // SSD1306 配置
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
            .scl_speed_hz = 400 * 1000,
        };

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(display_i2c_bus_, &io_config, &panel_io_));

        ESP_LOGI(TAG, "正在安装 SSD1306 驱动");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = -1;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

#ifdef SH1106
        ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(panel_io_, &panel_config, &panel_));
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
#endif
        ESP_LOGI(TAG, "SSD1306 驱动安装完成");

        // 复位显示屏
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "初始化显示屏失败");
            display_ = new NoDisplay();
            return;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, false));

        // 打开显示屏
        ESP_LOGI(TAG, "正在打开显示屏");
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y,
            {&font_puhui_14_1, &font_puhui_30_4, &font_awesome_14_1});
        backlight_ = new OledContrastBacklight(panel_io_, panel_);
        backlight_->RestoreBrightness();
    }

    // 闹钟设置状态：-1 = 空闲，0 = 小时，1 = 分钟，2 = 秒，3 = 已启用
    int alarm_set_state = -1;
    int alarm_hours = 0;
    int alarm_minutes = 0;
    int alarm_seconds = 0;
    esp_timer_handle_t alarm_adjust_delay_timer_ = nullptr;
    esp_timer_handle_t alarm_adjust_repeat_timer_ = nullptr;
    int alarm_adjust_delta_ = 0;
    int alarm_adjust_source_ = 0;
    bool alarm_adjust_repeating_ = false;
    bool suppress_touch_alarm_click_ = false;
    bool suppress_switch_alarm_click_ = false;

    bool IsAlarmValueAdjustMode() {
        auto display = GetDisplay();
        return display != nullptr &&
            display->GetCurrentPage() == PAGE_NAOZHONG &&
            alarm_set_state >= 0 && alarm_set_state <= 2;
    }

    void ApplyAlarmValueDelta(int delta, bool show_notification) {
        auto display = GetDisplay();
        if (display == nullptr) {
            return;
        }

        char state_str[32];
        if (alarm_set_state == 0) {
            alarm_hours = (alarm_hours + delta + 24) % 24;
            if (show_notification) {
                sprintf(state_str, "璁剧疆灏忔椂: %02d", alarm_hours);
            }
        } else if (alarm_set_state == 1) {
            alarm_minutes = (alarm_minutes + delta + 60) % 60;
            if (show_notification) {
                sprintf(state_str, "璁剧疆鍒嗛挓: %02d", alarm_minutes);
            }
        } else if (alarm_set_state == 2) {
            alarm_seconds = (alarm_seconds + delta + 60) % 60;
            if (show_notification) {
                sprintf(state_str, "璁剧疆绉? %02d", alarm_seconds);
            }
        } else {
            return;
        }

        display->SetAlarmTime(alarm_hours, alarm_minutes, alarm_seconds);
        if (show_notification) {
            display->ShowNotification(state_str, 1000);
        }
    }

    void EnsureAlarmAdjustTimers() {
        if (alarm_adjust_delay_timer_ == nullptr) {
            esp_timer_create_args_t delay_timer_args = {
                .callback = [](void* arg) {
                    auto self = static_cast<CompactWifiBoard*>(arg);
                    if (!self->IsAlarmValueAdjustMode()) {
                        return;
                    }
                    self->alarm_adjust_repeating_ = true;
                    self->ApplyAlarmValueDelta(self->alarm_adjust_delta_, false);
                    if (self->alarm_adjust_repeat_timer_ != nullptr) {
                        esp_timer_stop(self->alarm_adjust_repeat_timer_);
                        esp_timer_start_periodic(self->alarm_adjust_repeat_timer_, 120 * 1000);
                    }
                },
                .arg = this,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "alarm_adj_delay",
                .skip_unhandled_events = true,
            };
            ESP_ERROR_CHECK(esp_timer_create(&delay_timer_args, &alarm_adjust_delay_timer_));
        }

        if (alarm_adjust_repeat_timer_ == nullptr) {
            esp_timer_create_args_t repeat_timer_args = {
                .callback = [](void* arg) {
                    auto self = static_cast<CompactWifiBoard*>(arg);
                    if (!self->IsAlarmValueAdjustMode()) {
                        return;
                    }
                    self->ApplyAlarmValueDelta(self->alarm_adjust_delta_, false);
                },
                .arg = this,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "alarm_adj_repeat",
                .skip_unhandled_events = true,
            };
            ESP_ERROR_CHECK(esp_timer_create(&repeat_timer_args, &alarm_adjust_repeat_timer_));
        }
    }

    void StartAlarmAdjustPress(int delta, int source) {
        if (!IsAlarmValueAdjustMode()) {
            return;
        }
        EnsureAlarmAdjustTimers();
        alarm_adjust_delta_ = delta;
        alarm_adjust_source_ = source;
        alarm_adjust_repeating_ = false;
        if (alarm_adjust_repeat_timer_ != nullptr) {
            esp_timer_stop(alarm_adjust_repeat_timer_);
        }
        if (alarm_adjust_delay_timer_ != nullptr) {
            esp_timer_stop(alarm_adjust_delay_timer_);
            esp_timer_start_once(alarm_adjust_delay_timer_, 500 * 1000);
        }
    }

    void StopAlarmAdjustPress(int source) {
        if (alarm_adjust_source_ != source) {
            return;
        }
        if (alarm_adjust_delay_timer_ != nullptr) {
            esp_timer_stop(alarm_adjust_delay_timer_);
        }
        if (alarm_adjust_repeat_timer_ != nullptr) {
            esp_timer_stop(alarm_adjust_repeat_timer_);
        }
        if (alarm_adjust_repeating_) {
            if (source == 1) {
                suppress_touch_alarm_click_ = true;
            } else if (source == 2) {
                suppress_switch_alarm_click_ = true;
            }
        }
        alarm_adjust_source_ = 0;
        alarm_adjust_delta_ = 0;
        alarm_adjust_repeating_ = false;
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            HandleBootButtonClick();
        });

        touch_button_.OnClick([this]() {
            HandleTouchButtonClick();
        });

        touch_button_.OnPressDown([this]() {
            HandleTouchButtonPressDown();
        });
        touch_button_.OnPressUp([this]() {
            HandleTouchButtonPressUp();
        });

        switch_button_.OnClick([this]() {
            HandleSwitchButtonClick();
        });
        switch_button_.OnPressDown([this]() {
            HandleSwitchButtonPressDown();
        });
        switch_button_.OnPressUp([this]() {
            HandleSwitchButtonPressUp();
        });

        boot_button_.OnLongPress([this]() {
            HandleBootButtonLongPress();
        });

        switch_button_.OnDoubleClick([this]() {
            HandleSwitchButtonDoubleClick();
        });

        page_button_.OnClick([this]() {
            HandlePageButtonClick();
        });
        page_button_.OnLongPress([this]() {
            HandlePageButtonLongPress();
        });
    }

    // 物联网工具初始化，逐步迁移到 MCP 协议
    void InitializeTools() {
        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool("self.flashlight.get_state",
            "获取手电筒的开关状态。",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                return flashlight_on_ ? "{\"power\": true}" : "{\"power\": false}";
            });

        mcp_server.AddTool("self.flashlight.turn_on",
            "打开手电筒。当用户要求打开手电筒、手灯或照明灯时使用。",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                TurnOnFlashlight();
                return true;
            });

        mcp_server.AddTool("self.flashlight.turn_off",
            "关闭手电筒。当用户要求关闭手电筒、手灯或照明灯时使用。",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                TurnOffFlashlight();
                return true;
            });

        mcp_server.AddTool("self.flashlight.set_color",
            "设置手电筒颜色。支持白色、红色、橙色、黄色、绿色、蓝色、靛色和紫色，也支持中文颜色名称。",
            PropertyList({
                Property("color", kPropertyTypeString)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                auto color = properties["color"].value<std::string>();
                if (!SetFlashlightColorByName(color)) {
                    return std::string("{\"success\": false, \"message\": \"不支持的颜色\"}");
                }
                return std::string("{\"success\": true}");
            });

        mcp_server.AddTool("self.health.get_vitals",
            "检测当前血氧和心率。调用前请提示用户将手指稳定放在 MAX30102 传感器上并保持几秒钟。返回 JSON，包含是否检测到手指、心率和血氧。",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                (void)properties;
                esp_err_t ret = max30102.max30102_config();
                if (ret != ESP_OK) {
                    char buffer[160];
                    snprintf(buffer, sizeof(buffer),
                        "{\"success\":false,\"message\":\"failed to configure max30102\",\"error_code\":%d}",
                        static_cast<int>(ret));
                    return std::string(buffer);
                }

                constexpr int kReadAttempts = 120;
                constexpr int kReadDelayMs = 80;  // 120×80ms=9.6s，在AI超时窗口内
                esp_err_t last_ret = ESP_FAIL;
                max30102_data_t snapshot{};
                for (int i = 0; i < kReadAttempts; ++i) {
                    if (Board::GetInstance().GetDisplay()->GetCurrentPage() != PAGE_CHAT) {
                        last_ret = ESP_ERR_INVALID_STATE;
                        break;
                    }
                    last_ret = max30102.max30102_read_data();
                    snapshot = max30102.Max30102_Get_Data();
                    if (last_ret == ESP_OK && snapshot.hand_detected &&
                        snapshot.heart_rate > 0.0f && snapshot.spo2 > 0.0f) {
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(kReadDelayMs));
                }
                max30102.max30102_deconfig();

                char buffer[256];
                if (last_ret == ESP_OK && snapshot.hand_detected &&
                    snapshot.heart_rate > 0.0f && snapshot.spo2 > 0.0f) {
                    snprintf(buffer, sizeof(buffer),
                        "{\"success\":true,\"hand_detected\":true,\"heart_rate\":%.1f,\"spo2\":%.1f}",
                        snapshot.heart_rate, snapshot.spo2);
                } else {
                    snprintf(buffer, sizeof(buffer),
                        "{\"success\":false,\"hand_detected\":%s,\"heart_rate\":%.1f,\"spo2\":%.1f,"
                        "\"message\":\"no stable vitals measured, please keep finger on the sensor and retry\",\"error_code\":%d}",
                        snapshot.hand_detected ? "true" : "false",
                        snapshot.heart_rate,
                        snapshot.spo2,
                        static_cast<int>(last_ret));
                }
                return std::string(buffer);
            });

        mcp_server.AddTool("self.audio_music.list_local",
            "列出当前固件内置的可播放本地音乐。",
            PropertyList(),
            [](const PropertyList& properties) -> ReturnValue {
                (void)properties;
                return std::string(
                    "{\"success\":true,\"tracks\":["
                    "{\"id\":\"music1\",\"index\":1,\"name\":\"音乐1\"},"
                    "{\"id\":\"music2\",\"index\":2,\"name\":\"音乐2\"},"
                    "{\"id\":\"music3\",\"index\":3,\"name\":\"音乐3\"},"
                    "{\"id\":\"music4\",\"index\":4,\"name\":\"音乐4\"},"
                    "{\"id\":\"music5\",\"index\":5,\"name\":\"音乐5\"},"
                    "{\"id\":\"music6\",\"index\":6,\"name\":\"音乐6\"},"
                    "{\"id\":\"music7\",\"index\":7,\"name\":\"音乐7\"},"
                    "{\"id\":\"music8\",\"index\":8,\"name\":\"音乐8\"},"
                    "{\"id\":\"music9\",\"index\":9,\"name\":\"音乐9\"},"
                    "{\"id\":\"music10\",\"index\":10,\"name\":\"音乐10\"}"
                    "]}");
            });

        mcp_server.AddTool("self.audio_music.play_local",
            "播放固件内置的本地音乐。支持按 index(1-10) 或 name 指定曲目。name 支持 music1..music10、数字字符串 1..10、以及中文名 音乐1..音乐10。",
            PropertyList({
                Property("name", kPropertyTypeString, std::string("")),
                Property("index", kPropertyTypeInteger, 1, 1, 10)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                static const std::array<std::string_view, 10> tracks = {
                    Lang::Sounds::P3_0, Lang::Sounds::P3_1, Lang::Sounds::P3_2, Lang::Sounds::P3_3, Lang::Sounds::P3_4,
                    Lang::Sounds::P3_5, Lang::Sounds::P3_6, Lang::Sounds::P3_7, Lang::Sounds::P3_8, Lang::Sounds::P3_9,
                };

                std::string name = properties["name"].value<std::string>();
                int index = properties["index"].value<int>();
                if (!name.empty()) {
                    std::string normalized = name;
                    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
                        return static_cast<char>(std::tolower(c));
                    });
                    normalized.erase(std::remove_if(normalized.begin(), normalized.end(), [](unsigned char c) {
                        return std::isspace(c) || c == '-' || c == '_';
                    }), normalized.end());

                    if (normalized.rfind("music", 0) == 0 && normalized.size() > 5) {
                        index = atoi(normalized.c_str() + 5);
                    } else if (normalized.rfind("音乐", 0) == 0 && normalized.size() > 6) {
                        index = atoi(normalized.c_str() + 6);
                    } else {
                        int parsed = atoi(normalized.c_str());
                        if (parsed > 0) {
                            index = parsed;
                        }
                    }
                }

                if (index < 1 || index > static_cast<int>(tracks.size())) {
                    return std::string("{\"success\":false,\"message\":\"invalid track, valid range is 1-10\"}");
                }

                auto& audio_service = Application::GetInstance().GetAudioService();
                audio_service.StopPlayback();
                auto codec = GetAudioCodec();
                if (codec != nullptr && !codec->output_enabled()) {
                    codec->EnableOutput(true);
                }
                audio_service.PlaySound(tracks[index - 1]);

                char buffer[96];
                snprintf(buffer, sizeof(buffer),
                    "{\"success\":true,\"track\":{\"id\":\"music%d\",\"index\":%d,\"name\":\"音乐%d\"}}",
                    index, index, index);
                return std::string(buffer);
            });

        mcp_server.AddTool("self.audio_music.stop",
            "停止当前本地音乐或提示音播放。",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                (void)properties;
                auto& audio_service = Application::GetInstance().GetAudioService();
                audio_service.StopPlayback();
                auto codec = GetAudioCodec();
                if (codec != nullptr) {
                    codec->EnableOutput(false);
                }
                return true;
            });

        mcp_server.AddTool("self.sport.get_current_data",
            "读取当前手表运动数据，返回步数、热量和距离。",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                (void)properties;
                const mpu6050_sport_t& sport = mpu6050_.GetSportData();
                char buffer[192];
                snprintf(buffer, sizeof(buffer),
                    "{\"success\":true,\"steps\":%lu,\"calories\":%.1f,\"distance_m\":%.1f}",
                    static_cast<unsigned long>(sport.steps),
                    sport.calories,
                    sport.distance_m);
                return std::string(buffer);
            });

        mcp_server.AddTool("self.pose.get_angles",
            "读取当前手表姿态角，返回 pitch、roll、yaw，单位为度。",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                (void)properties;
                const mpu6050_angle_t& angle = mpu6050_.GetAngle();
                char buffer[192];
                snprintf(buffer, sizeof(buffer),
                    "{\"success\":true,\"pitch\":%.1f,\"roll\":%.1f,\"yaw\":%.1f}",
                    angle.pitch,
                    angle.roll,
                    angle.yaw);
                return std::string(buffer);
            });

        mcp_server.AddTool("self.alarm.set",
            "设置闹钟时间并启用闹钟。参数 hour/minute/second 为 24 小时制。",
            PropertyList({
                Property("hour", kPropertyTypeInteger, 0, 23),
                Property("minute", kPropertyTypeInteger, 0, 59),
                Property("second", kPropertyTypeInteger, 0, 59)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                alarm_hours = properties["hour"].value<int>();
                alarm_minutes = properties["minute"].value<int>();
                alarm_seconds = properties["second"].value<int>();

                auto display = GetDisplay();
                display->SetAlarmTime(alarm_hours, alarm_minutes, alarm_seconds);
                display->StartAlarm();

                char buffer[160];
                snprintf(buffer, sizeof(buffer),
                    "{\"success\":true,\"enabled\":true,\"time\":\"%02d:%02d:%02d\"}",
                    alarm_hours,
                    alarm_minutes,
                    alarm_seconds);
                return std::string(buffer);
            });

        mcp_server.AddTool("self.alarm.get_status",
            "读取当前闹钟设置和启用状态。",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                (void)properties;
                auto display = GetDisplay();
                char buffer[160];
                snprintf(buffer, sizeof(buffer),
                    "{\"success\":true,\"enabled\":%s,\"time\":\"%02d:%02d:%02d\",\"music\":\"%s\"}",
                    display->IsAlarmEnabled() ? "true" : "false",
                    display->GetAlarmHours(),
                    display->GetAlarmMinutes(),
                    display->GetAlarmSeconds(),
                    display->GetAlarmMusic().c_str());
                return std::string(buffer);
            });

        mcp_server.AddTool("self.alarm.disable",
            "关闭当前闹钟。",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                (void)properties;
                auto display = GetDisplay();
                display->StopAlarm();

                char buffer[160];
                snprintf(buffer, sizeof(buffer),
                    "{\"success\":true,\"enabled\":false,\"time\":\"%02d:%02d:%02d\"}",
                    alarm_hours,
                    alarm_minutes,
                    alarm_seconds);
                return std::string(buffer);
            });
    }

    // void PowerInit()
    // {
    //     gpio_config_t io_conf = {
    //         .pin_bit_mask = (1ULL << POWER_GPIO),
    //         .mode         = GPIO_MODE_INPUT,
    //         .pull_up_en   = GPIO_PULLUP_DISABLE,
    //         .pull_down_en = GPIO_PULLDOWN_DISABLE,
    //         .intr_type    = GPIO_INTR_DISABLE,
    //     };
    //     io_conf.intr_type = GPIO_INTR_ANYEDGE;
    //     gpio_config(&io_conf);
    // }



public:
    CompactWifiBoard() :
        boot_button_(BOOT_BUTTON_GPIO, false, 0, 40),
        touch_button_(TOUCH_BUTTON_GPIO, false, 0, 40),
        switch_button_(PAGE_DOWN_BUTTON_GPIO, false, 0, 40),
        page_button_(PAGE_UP_BUTTON_GPIO, false, 0, 40){
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeButtons();
        
        // 鍒濆鍖栨墜鐢电瓛LED
        flashlight_led_ = new SingleLed(LAMP_GPIO);
        flashlight_led_->SetColor(255, 255, 255);
        flashlight_led_->TurnOff();
        InitializeTools();

        if (mpu6050_.Config() == ESP_OK) {
            mpu6050_.StartAngleOutputTask(20); // 20ms 采样，500ms 串口输出
        }

        const bool paj_shares_mpu6050_bus =
            (PAJ7620_SDA_PIN == MPU6050_SDA_PIN) &&
            (PAJ7620_SCL_PIN == MPU6050_SCL_PIN);
        if (paj_shares_mpu6050_bus) {
            ESP_LOGW(TAG, "PAJ7620 pins overlap MPU6050 soft-I2C pins, gesture logging disabled");
        } else {
            ESP_LOGI(TAG, "Initializing PAJ7620 on SDA=%d SCL=%d", PAJ7620_SDA_PIN, PAJ7620_SCL_PIN);
            esp_err_t paj_err = paj7620_.Config();
            if (paj_err == ESP_OK) {
                paj7620_.SetGestureCallback([this](Paj7620Gesture gesture) {
                    Application::GetInstance().Schedule([this, gesture]() {
                        HandleGestureAction(gesture);
                    });
                });
                paj7620_.StartGesturePollingTask(100);
            } else {
                ESP_LOGW(TAG, "PAJ7620 init failed: %s", esp_err_to_name(paj_err));
                ESP_LOGW(TAG, "PAJ7620 gesture logging disabled");
            }
        }
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual Backlight* GetBacklight() override {
        return backlight_;
    }
    
    virtual void TurnOnFlashlight() override {
        if (flashlight_led_) {
            flashlight_on_ = true;
            flashlight_led_->TurnOn();
        }
    }
    
    virtual void TurnOffFlashlight() override {
        if (flashlight_led_) {
            flashlight_on_ = false;
            flashlight_led_->TurnOff();
        }
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
};

DECLARE_BOARD(CompactWifiBoard);
