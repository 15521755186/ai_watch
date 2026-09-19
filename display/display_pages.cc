/**
 * @file display_pages.cc
 * @brief 页面切换函数（从 display.cc 拆分）
 */
#include "display.h"
#include "board.h"
#include "app/application.h"
#include "sensors/max30102/max30102.h"

#include <esp_log.h>

#define TAG "Display"

void Display::Switch_Dialogue_Page(void)
{
    DisplayLockGuard lock(this);
    ESP_LOGI(TAG, "Switching to dialogue page");
    auto& audio_service = Application::GetInstance().GetAudioService();
    audio_service.StopAudioImmediately();
    if (current_page_!= PAGE_INIT)
    {
        if (current_page_ == PAGE_DETE) {
            auto max30102 = Board::GetInstance().GetMAX30102();
            max30102->max30102_deconfig();
            Application::GetInstance().SetDeviceState(kDeviceStateIdle);
        }
        if (current_page_ == PAGE_MAIN) {
            dialogue_return_page_ = PAGE_MAIN;
            lv_obj_add_flag(main_screen_, LV_OBJ_FLAG_HIDDEN);
        } else {
            dialogue_return_page_ = PAGE_SWITCH;
            lv_obj_add_flag(pages[current_index_].page, LV_OBJ_FLAG_HIDDEN);
        }
        current_page_ = PAGE_CHAT;
        Application::GetInstance().SetDeviceState(kDeviceStateIdle);
    }
    lv_obj_clear_flag(dialogue_screen_, LV_OBJ_FLAG_HIDDEN);  // 显示对话页面
    
    // 关闭手电筒LED
    Board::GetInstance().TurnOffFlashlight();
}

void Display::Return_Switch_Page(void)
{
    DisplayLockGuard lock(this);
    ESP_LOGI(TAG, "Switching to switch page");
    bool return_to_switch = true;
    auto& audio_service = Application::GetInstance().GetAudioService();

    if (current_page_== PAGE_MAIN)
            lv_obj_add_flag(main_screen_, LV_OBJ_FLAG_HIDDEN);
    else if ((current_page_== PAGE_CHAT) || (current_page_== PAGE_INIT))
    {
            lv_obj_add_flag(dialogue_screen_, LV_OBJ_FLAG_HIDDEN);
            audio_service.StopAudioImmediately();
            if (current_page_ == PAGE_CHAT && dialogue_return_page_ == PAGE_MAIN) {
                lv_obj_clear_flag(main_screen_, LV_OBJ_FLAG_HIDDEN);
                current_page_ = PAGE_MAIN;
                Application::GetInstance().SetDeviceState(kDeviceStateIdle);
                return_to_switch = false;
            } else {
                Application::GetInstance().SetDeviceState(kDeviceStateStop);
            }
    }
    else if (current_page_== PAGE_DETE) {
            lv_obj_add_flag(health_screen_, LV_OBJ_FLAG_HIDDEN);
            auto max30102 = Board::GetInstance().GetMAX30102();
            max30102->max30102_deconfig();
            Application::GetInstance().SetDeviceState(kDeviceStateIdle);
    }
    else if (current_page_== PAGE_WEATHER) 
            lv_obj_add_flag(weather_screen_, LV_OBJ_FLAG_HIDDEN);
    else if (current_page_== PAGE_FLASHLIGHT) 
    {
        lv_obj_add_flag(flashlight_screen_, LV_OBJ_FLAG_HIDDEN);
        // 关闭手电筒LED
        Board::GetInstance().TurnOffFlashlight();
    }
    else if (current_page_== PAGE_ABOUT) 
            lv_obj_add_flag(about_screen_, LV_OBJ_FLAG_HIDDEN);
    else if (current_page_== PAGE_RECONFIG) 
            lv_obj_add_flag(reconfig_screen_, LV_OBJ_FLAG_HIDDEN);
    else if (current_page_== PAGE_MIAOBIAO) 
            lv_obj_add_flag(miaobiao_screen_, LV_OBJ_FLAG_HIDDEN);
    else if (current_page_== PAGE_NAOZHONG)
    {
            lv_obj_add_flag(naozhong_screen_, LV_OBJ_FLAG_HIDDEN);
            // Stop any currently playing alarm audio immediately
            Application::GetInstance().GetAudioService().StopAudioImmediately();
    }
    else if (current_page_== PAGE_MPU6050) {
            lv_obj_add_flag(mpu6050_screen_, LV_OBJ_FLAG_HIDDEN);
            if (mpu6050_update_timer_) {
                esp_timer_stop(mpu6050_update_timer_);
                esp_timer_delete(mpu6050_update_timer_);
                mpu6050_update_timer_ = nullptr;
            }
    }
    else if (current_page_== PAGE_SPORT) {
            lv_obj_add_flag(sport_screen_, LV_OBJ_FLAG_HIDDEN);
            if (sport_update_timer_) {
                esp_timer_stop(sport_update_timer_);
                esp_timer_delete(sport_update_timer_);
                sport_update_timer_ = nullptr;
            }
    }
    else if (current_page_ == PAGE_FUN) {
            lv_obj_add_flag(fun_screen_, LV_OBJ_FLAG_HIDDEN);
    }
    else if (current_page_ == PAGE_GAME) {
            lv_obj_add_flag(game_screen_, LV_OBJ_FLAG_HIDDEN);
            game_running_ = false;
            StopGameTimer();
    }
    else if (current_page_ == PAGE_REFLEX) {
            lv_obj_add_flag(reflex_screen_, LV_OBJ_FLAG_HIDDEN);
            StopReflexTimer();
            reflex_state_ = kReflexIdle;
    }
    else if (current_page_ == PAGE_MEMORY) {
            lv_obj_add_flag(memory_screen_, LV_OBJ_FLAG_HIDDEN);
            StopMemoryTimer();
            memory_state_ = kMemoryIdle;
            memory_active_lane_ = -1;
            memory_input_flash_pending_ = false;
    }
    else if (current_page_ == PAGE_BREATHE) {
            lv_obj_add_flag(breathe_screen_, LV_OBJ_FLAG_HIDDEN);
            breathe_running_ = false;
            StopBreatheTimer();
    }
    else if (current_page_ == PAGE_QUICK) {
            lv_obj_add_flag(quick_screen_, LV_OBJ_FLAG_HIDDEN);
            if (quick_return_page_ == PAGE_MAIN) {
                lv_obj_clear_flag(main_screen_, LV_OBJ_FLAG_HIDDEN);
                current_page_ = PAGE_MAIN;
                Application::GetInstance().SetDeviceState(kDeviceStateIdle);
                return_to_switch = false;
            } else if (quick_return_page_ == PAGE_SOUND) {
                lv_obj_clear_flag(sound_screen_, LV_OBJ_FLAG_HIDDEN);
                current_page_ = PAGE_SOUND;
                EnsureSoundTimer();
                StopSoundTimer();
                esp_timer_start_periodic(sound_timer_, kSoundTickMs * 1000);
                UpdateSoundPageUi();
                Application::GetInstance().SetDeviceState(kDeviceStateIdle);
                return_to_switch = false;
            } else if (quick_return_page_ == PAGE_FUN) {
                lv_obj_clear_flag(fun_screen_, LV_OBJ_FLAG_HIDDEN);
                current_page_ = PAGE_FUN;
                UpdateFunPageUi();
                Application::GetInstance().SetDeviceState(kDeviceStateIdle);
                return_to_switch = false;
            }
    }
    else if (current_page_ == PAGE_SOUND) {
            lv_obj_add_flag(sound_screen_, LV_OBJ_FLAG_HIDDEN);
            StopSoundTimer();
    }
    else if (current_page_ == PAGE_TIME_SETTING) {
            lv_obj_add_flag(time_setting_screen_, LV_OBJ_FLAG_HIDDEN);
    }
    if (!return_to_switch) {
        return;
    }

    Application::GetInstance().SetDeviceState(kDeviceStateSwitch);
    lv_obj_clear_flag(pages[current_index_].page, LV_OBJ_FLAG_HIDDEN);
    current_page_ = PAGE_SWITCH;
    OnSwitchPageShown();
}

void Display::Switch_Main_Page(void)
{
    DisplayLockGuard lock(this);
    ESP_LOGI(TAG, "Switching to main page");
    auto& audio_service = Application::GetInstance().GetAudioService();
    audio_service.StopAudioImmediately();
    if (current_page_ == PAGE_DETE) {
        auto max30102 = Board::GetInstance().GetMAX30102();
        max30102->max30102_deconfig();
    }
    if (current_page_ == PAGE_INIT)
        lv_obj_add_flag(dialogue_screen_, LV_OBJ_FLAG_HIDDEN);  
    else 
        lv_obj_add_flag(pages[current_index_].page, LV_OBJ_FLAG_HIDDEN);  
    lv_obj_clear_flag(main_screen_, LV_OBJ_FLAG_HIDDEN);  // 显示主页面
    current_page_ = PAGE_MAIN;
    current_page_ = PAGE_MAIN;
    Application::GetInstance().SetDeviceState(kDeviceStateIdle);
    
    // 关闭手电筒LED
    Board::GetInstance().TurnOffFlashlight();
}

void Display::Switch_Health_Check_Page(void)
{
    
    auto max30102 = Board::GetInstance().GetMAX30102();
    ESP_LOGI(TAG, "Switching to Health_Check page");
    DisplayLockGuard lock(this);
    Application::GetInstance().GetAudioService().StopAudioImmediately();
    lv_label_set_text_fmt(heart_value_, "%d", max30102->Max30102_Get_Heart());
    lv_label_set_text_fmt(spo2_value_, "%d", max30102->Max30102_Get_SpO2());
    lv_obj_add_flag(pages[current_index_].page, LV_OBJ_FLAG_HIDDEN);  
    current_page_ = PAGE_DETE;
    lv_obj_clear_flag(health_screen_, LV_OBJ_FLAG_HIDDEN);  // 显示健康检测页面
    current_page_ = PAGE_DETE;
    max30102->max30102_config();
    Application::GetInstance().SetDeviceState(kDeviceStateHealth);
    
    // 关闭手电筒LED
    Board::GetInstance().TurnOffFlashlight();
}

void Display::Switch_Weather_Page(void)
{
    ESP_LOGI(TAG, "Switching to weather page");
    DisplayLockGuard lock(this);
    lv_obj_add_flag(pages[current_index_].page, LV_OBJ_FLAG_HIDDEN);  
    lv_obj_clear_flag(weather_screen_, LV_OBJ_FLAG_HIDDEN);  // 先显示页面
    
    // 先显示缓存的天气数据
    SetWeather();
    
    // 在后台线程中更新天气数据
    xTaskCreate([](void* arg) {
        auto display = static_cast<Display*>(arg);
        display->UpdateWeather();
        vTaskDelete(nullptr);
    }, "update_weather", 4096 * 2, this, 5, nullptr);  // 8KB，HTTPS/TLS 握手需要更多栈
    
    current_page_ = PAGE_WEATHER;
    Application::GetInstance().SetDeviceState(kDeviceStateIdle);
    
    // 关闭手电筒LED
    Board::GetInstance().TurnOffFlashlight();
}

void Display::Switch_About_Page(void)
{
    ESP_LOGI(TAG, "Switching to about page");
    DisplayLockGuard lock(this);
    
    if (current_page_!= PAGE_INIT)
    {
        current_page_ = PAGE_ABOUT;
        // 检查 current_index_ 是否在有效范围内
        if (current_index_ >= 0 && current_index_ < Switch_Count) {
            lv_obj_add_flag(pages[current_index_].page, LV_OBJ_FLAG_HIDDEN);
        } else {
            ESP_LOGE(TAG, "Invalid current_index_: %d", current_index_);
        }
        Application::GetInstance().SetDeviceState(kDeviceStateIdle);
    }
    lv_obj_clear_flag(about_screen_, LV_OBJ_FLAG_HIDDEN);
    UpdateAboutStats();  // 刷新系统实时数据

    Board::GetInstance().TurnOffFlashlight();
}

void Display::Switch_Reconfig_Page(void)
{
    ESP_LOGI(TAG, "Switching to reconfig page");
    DisplayLockGuard lock(this);
    
    if (current_page_!= PAGE_INIT)
    {
        current_page_ = PAGE_RECONFIG;
        // 检查 current_index_ 是否在有效范围内
        if (current_index_ >= 0 && current_index_ < Switch_Count) {
            lv_obj_add_flag(pages[current_index_].page, LV_OBJ_FLAG_HIDDEN);
        } else {
            ESP_LOGE(TAG, "Invalid current_index_: %d", current_index_);
        }
        Application::GetInstance().SetDeviceState(kDeviceStateIdle);
    }
    lv_obj_clear_flag(reconfig_screen_, LV_OBJ_FLAG_HIDDEN);  // 显示重新配网页面
    
    // 关闭手电筒LED
    Board::GetInstance().TurnOffFlashlight();
}

void Display::Switch_Flashlight_Page(void)
{
    ESP_LOGI(TAG, "Switching to flashlight page");
    DisplayLockGuard lock(this);
    
    if (current_page_!= PAGE_INIT)
    {
        current_page_ = PAGE_FLASHLIGHT;
        // 检查 current_index_ 是否在有效范围内
        if (current_index_ >= 0 && current_index_ < Switch_Count) {
            lv_obj_add_flag(pages[current_index_].page, LV_OBJ_FLAG_HIDDEN);
        } else {
            ESP_LOGE(TAG, "Invalid current_index_: %d", current_index_);
        }
        Application::GetInstance().SetDeviceState(kDeviceStateIdle);
    }
    lv_obj_clear_flag(flashlight_screen_, LV_OBJ_FLAG_HIDDEN);  // 显示手电筒页面
    
    // 打开手电筒LED
    Board::GetInstance().TurnOnFlashlight();
}

void Display::Switch_Miaobiao_Page(void)
{
    ESP_LOGI(TAG, "Switching to miaobiao page");
    DisplayLockGuard lock(this);
    
    if (current_page_!= PAGE_INIT)
    {
        current_page_ = PAGE_MIAOBIAO;
        // 检查 current_index_ 是否在有效范围内
        if (current_index_ >= 0 && current_index_ < Switch_Count) {
            lv_obj_add_flag(pages[current_index_].page, LV_OBJ_FLAG_HIDDEN);
        } else {
            ESP_LOGE(TAG, "Invalid current_index_: %d", current_index_);
        }
        Application::GetInstance().SetDeviceState(kDeviceStateIdle);
    }
    lv_obj_clear_flag(miaobiao_screen_, LV_OBJ_FLAG_HIDDEN);  // 显示秒表页面
    
    // 关闭手电筒LED
    Board::GetInstance().TurnOffFlashlight();
}


void Display::Switch_Naozhong_Page(void)
{
    ESP_LOGI(TAG, "Switching to naozhong page");
    DisplayLockGuard lock(this);

    if (current_page_ != PAGE_INIT) {
        if (current_page_ == PAGE_MAIN) {
            lv_obj_add_flag(main_screen_, LV_OBJ_FLAG_HIDDEN);
        } else if (current_page_ == PAGE_CHAT) {
            lv_obj_add_flag(dialogue_screen_, LV_OBJ_FLAG_HIDDEN);
        } else if (current_index_ >= 0 && current_index_ < Switch_Count) {
            lv_obj_add_flag(pages[current_index_].page, LV_OBJ_FLAG_HIDDEN);
        } else {
            ESP_LOGE(TAG, "Invalid current_index_: %d", current_index_);
        }
    }

    // Ensure we always set the current page to NAOZHONG so input handlers
    // (back/boot button) can correctly detect and exit the alarm page even
    // if the alarm fired during initialization (PAGE_INIT).
    current_page_ = PAGE_NAOZHONG;
    Application::GetInstance().SetDeviceState(kDeviceStateIdle);

    lv_obj_clear_flag(naozhong_screen_, LV_OBJ_FLAG_HIDDEN);
    Board::GetInstance().TurnOffFlashlight();
}

void Display::Switch_Mpu6050_Page(void)
{
    ESP_LOGI(TAG, "Switching to mpu6050 page");
    DisplayLockGuard lock(this);

    if (current_page_ != PAGE_INIT)
    {
        current_page_ = PAGE_MPU6050;
        if (current_index_ >= 0 && current_index_ < Switch_Count) {
            lv_obj_add_flag(pages[current_index_].page, LV_OBJ_FLAG_HIDDEN);
        } else {
            ESP_LOGE(TAG, "Invalid current_index_: %d", current_index_);
        }
        Application::GetInstance().SetDeviceState(kDeviceStateIdle);
    }
    lv_obj_clear_flag(mpu6050_screen_, LV_OBJ_FLAG_HIDDEN);

    // 启动200ms定时器更新角度数据
    if (mpu6050_update_timer_ == nullptr) {
        esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) {
                Display* self = static_cast<Display*>(arg);
                self->UpdateMpu6050Display();
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "mpu6050_display",
            .skip_unhandled_events = true,
        };
        esp_timer_create(&timer_args, &mpu6050_update_timer_);
    }
    esp_timer_start_periodic(mpu6050_update_timer_, 200 * 1000); // 200ms

    Board::GetInstance().TurnOffFlashlight();
}

void Display::Switch_Sport_Page(void)
{
    ESP_LOGI(TAG, "Switching to sport page");
    DisplayLockGuard lock(this);

    if (current_page_ != PAGE_INIT)
    {
        current_page_ = PAGE_SPORT;
        if (current_index_ >= 0 && current_index_ < Switch_Count) {
            lv_obj_add_flag(pages[current_index_].page, LV_OBJ_FLAG_HIDDEN);
        } else {
            ESP_LOGE(TAG, "Invalid current_index_: %d", current_index_);
        }
        Application::GetInstance().SetDeviceState(kDeviceStateIdle);
    }
    lv_obj_clear_flag(sport_screen_, LV_OBJ_FLAG_HIDDEN);

    // 启动500ms定时器更新运动数据
    if (sport_update_timer_ == nullptr) {
        esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) {
                Display* self = static_cast<Display*>(arg);
                self->UpdateSportDisplay();
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "sport_display",
            .skip_unhandled_events = true,
        };
        esp_timer_create(&timer_args, &sport_update_timer_);
    }
    esp_timer_start_periodic(sport_update_timer_, 500 * 1000); // 500ms

    Board::GetInstance().TurnOffFlashlight();
}

void Display::Switch_Fun_Page(void)
{
    ESP_LOGI(TAG, "Switching to fun page");
    DisplayLockGuard lock(this);

    if (current_page_ == PAGE_SWITCH) {
        if (current_index_ >= 0 && current_index_ < Switch_Count) {
            lv_obj_add_flag(pages[current_index_].page, LV_OBJ_FLAG_HIDDEN);
        }
    } else if (current_page_ == PAGE_GAME) {
        lv_obj_add_flag(game_screen_, LV_OBJ_FLAG_HIDDEN);
        game_running_ = false;
        StopGameTimer();
    } else if (current_page_ == PAGE_REFLEX) {
        lv_obj_add_flag(reflex_screen_, LV_OBJ_FLAG_HIDDEN);
        StopReflexTimer();
        reflex_state_ = kReflexIdle;
    } else if (current_page_ == PAGE_MEMORY) {
        lv_obj_add_flag(memory_screen_, LV_OBJ_FLAG_HIDDEN);
        StopMemoryTimer();
        memory_state_ = kMemoryIdle;
        memory_active_lane_ = -1;
    } else if (current_page_ == PAGE_BREATHE) {
        lv_obj_add_flag(breathe_screen_, LV_OBJ_FLAG_HIDDEN);
        breathe_running_ = false;
        StopBreatheTimer();
    } else if (current_page_ == PAGE_QUICK) {
        lv_obj_add_flag(quick_screen_, LV_OBJ_FLAG_HIDDEN);
    } else if (current_page_ == PAGE_SOUND) {
        lv_obj_add_flag(sound_screen_, LV_OBJ_FLAG_HIDDEN);
        sound_autoplay_ = false;
        sound_preview_playing_ = false;
        sound_stop_requested_ = true;
        sound_idle_ticks_ = 0;
        sound_requested_index_ = -1;
        StopSoundTimer();
        Application::GetInstance().GetAudioService().ResetDecoder();
    } else if (current_page_ == PAGE_INIT) {
        lv_obj_add_flag(dialogue_screen_, LV_OBJ_FLAG_HIDDEN);
    }

    current_page_ = PAGE_FUN;
    if (fun_selected_index_ >= kVisibleFunItemCount) {
        fun_selected_index_ = 0;
    }
    lv_obj_clear_flag(fun_screen_, LV_OBJ_FLAG_HIDDEN);
    UpdateFunPageUi();
    Application::GetInstance().SetDeviceState(kDeviceStateIdle);
    Board::GetInstance().TurnOffFlashlight();
}

void Display::Switch_Game_Page(void)
{
    ESP_LOGI(TAG, "Switching to game page");
    DisplayLockGuard lock(this);

    if (current_page_ == PAGE_FUN) {
        lv_obj_add_flag(fun_screen_, LV_OBJ_FLAG_HIDDEN);
    } else if (current_page_ == PAGE_QUICK) {
        lv_obj_add_flag(quick_screen_, LV_OBJ_FLAG_HIDDEN);
    }

    current_page_ = PAGE_GAME;
    lv_obj_clear_flag(game_screen_, LV_OBJ_FLAG_HIDDEN);
    UpdateGamePageUi();
    Application::GetInstance().SetDeviceState(kDeviceStateIdle);
    Board::GetInstance().TurnOffFlashlight();
}

void Display::Switch_Reflex_Page(void)
{
    ESP_LOGI(TAG, "Switching to reflex page");
    DisplayLockGuard lock(this);

    if (current_page_ == PAGE_FUN) {
        lv_obj_add_flag(fun_screen_, LV_OBJ_FLAG_HIDDEN);
    } else if (current_page_ == PAGE_BREATHE) {
        lv_obj_add_flag(breathe_screen_, LV_OBJ_FLAG_HIDDEN);
        breathe_running_ = false;
        StopBreatheTimer();
    } else if (current_page_ == PAGE_SOUND) {
        lv_obj_add_flag(sound_screen_, LV_OBJ_FLAG_HIDDEN);
        sound_autoplay_ = false;
        sound_preview_playing_ = false;
        sound_stop_requested_ = true;
        sound_idle_ticks_ = 0;
        sound_requested_index_ = -1;
        StopSoundTimer();
        Application::GetInstance().GetAudioService().ResetDecoder();
    } else if (current_page_ == PAGE_QUICK) {
        lv_obj_add_flag(quick_screen_, LV_OBJ_FLAG_HIDDEN);
    }

    current_page_ = PAGE_REFLEX;
    reflex_state_ = kReflexIdle;
    reflex_last_ms_ = -1;
    lv_obj_clear_flag(reflex_screen_, LV_OBJ_FLAG_HIDDEN);
    UpdateReflexPageUi();
    Application::GetInstance().SetDeviceState(kDeviceStateIdle);
    Board::GetInstance().TurnOffFlashlight();
}

void Display::Switch_Breathe_Page(void)
{
    ESP_LOGI(TAG, "Switching to breathe page");
    DisplayLockGuard lock(this);

    if (current_page_ == PAGE_FUN) {
        lv_obj_add_flag(fun_screen_, LV_OBJ_FLAG_HIDDEN);
    } else if (current_page_ == PAGE_REFLEX) {
        lv_obj_add_flag(reflex_screen_, LV_OBJ_FLAG_HIDDEN);
        StopReflexTimer();
        reflex_state_ = kReflexIdle;
    } else if (current_page_ == PAGE_QUICK) {
        lv_obj_add_flag(quick_screen_, LV_OBJ_FLAG_HIDDEN);
    } else if (current_page_ == PAGE_SOUND) {
        lv_obj_add_flag(sound_screen_, LV_OBJ_FLAG_HIDDEN);
        sound_requested_index_ = -1;
    }

    current_page_ = PAGE_BREATHE;
    breathe_running_ = false;
    StopBreatheTimer();
    breathe_phase_ = 0;
    breathe_phase_tick_ = 0;
    breathe_cycles_ = 0;
    lv_obj_clear_flag(breathe_screen_, LV_OBJ_FLAG_HIDDEN);
    UpdateBreathePageUi();
    Application::GetInstance().SetDeviceState(kDeviceStateIdle);
    Board::GetInstance().TurnOffFlashlight();
}

void Display::Switch_Memory_Page(void)
{
    ESP_LOGI(TAG, "Switching to memory page");
    DisplayLockGuard lock(this);

    if (current_page_ == PAGE_FUN) {
        lv_obj_add_flag(fun_screen_, LV_OBJ_FLAG_HIDDEN);
    } else if (current_page_ == PAGE_REFLEX) {
        lv_obj_add_flag(reflex_screen_, LV_OBJ_FLAG_HIDDEN);
        StopReflexTimer();
        reflex_state_ = kReflexIdle;
    } else if (current_page_ == PAGE_BREATHE) {
        lv_obj_add_flag(breathe_screen_, LV_OBJ_FLAG_HIDDEN);
        breathe_running_ = false;
        StopBreatheTimer();
    } else if (current_page_ == PAGE_QUICK) {
        lv_obj_add_flag(quick_screen_, LV_OBJ_FLAG_HIDDEN);
    } else if (current_page_ == PAGE_SOUND) {
        lv_obj_add_flag(sound_screen_, LV_OBJ_FLAG_HIDDEN);
        sound_autoplay_ = false;
        sound_preview_playing_ = false;
        sound_stop_requested_ = true;
        sound_idle_ticks_ = 0;
        sound_requested_index_ = -1;
        StopSoundTimer();
        Application::GetInstance().GetAudioService().ResetDecoder();
    }

    current_page_ = PAGE_MEMORY;
    memory_state_ = kMemoryIdle;
    memory_active_lane_ = -1;
    memory_input_flash_pending_ = false;
    StopMemoryTimer();
    lv_obj_clear_flag(memory_screen_, LV_OBJ_FLAG_HIDDEN);
    UpdateMemoryPageUi();
    Application::GetInstance().SetDeviceState(kDeviceStateIdle);
    Board::GetInstance().TurnOffFlashlight();
}

void Display::Switch_Quick_Page(void)
{
    ESP_LOGI(TAG, "Switching to quick page");
    DisplayLockGuard lock(this);

    if (current_page_ == PAGE_MAIN) {
        quick_return_page_ = PAGE_MAIN;
        lv_obj_add_flag(main_screen_, LV_OBJ_FLAG_HIDDEN);
    } else if (current_page_ == PAGE_SWITCH) {
        quick_return_page_ = PAGE_SWITCH;
        if (current_index_ >= 0 && current_index_ < Switch_Count) {
            lv_obj_add_flag(pages[current_index_].page, LV_OBJ_FLAG_HIDDEN);
        } else {
            ESP_LOGE(TAG, "Invalid current_index_: %d", current_index_);
        }
    } else if (current_page_ == PAGE_FUN) {
        quick_return_page_ = PAGE_FUN;
        lv_obj_add_flag(fun_screen_, LV_OBJ_FLAG_HIDDEN);
    } else if (current_page_ == PAGE_GAME) {
        quick_return_page_ = PAGE_FUN;
        lv_obj_add_flag(game_screen_, LV_OBJ_FLAG_HIDDEN);
        game_running_ = false;
        StopGameTimer();
    } else if (current_page_ == PAGE_REFLEX) {
        quick_return_page_ = PAGE_FUN;
        lv_obj_add_flag(reflex_screen_, LV_OBJ_FLAG_HIDDEN);
        StopReflexTimer();
        reflex_state_ = kReflexIdle;
    } else if (current_page_ == PAGE_BREATHE) {
        quick_return_page_ = PAGE_FUN;
        lv_obj_add_flag(breathe_screen_, LV_OBJ_FLAG_HIDDEN);
        breathe_running_ = false;
        StopBreatheTimer();
    } else if (current_page_ == PAGE_MEMORY) {
        quick_return_page_ = PAGE_FUN;
        lv_obj_add_flag(memory_screen_, LV_OBJ_FLAG_HIDDEN);
        StopMemoryTimer();
        memory_state_ = kMemoryIdle;
        memory_active_lane_ = -1;
        memory_input_flash_pending_ = false;
    } else if (current_page_ == PAGE_SOUND) {
        quick_return_page_ = PAGE_SOUND;
        lv_obj_add_flag(sound_screen_, LV_OBJ_FLAG_HIDDEN);
        sound_autoplay_ = false;
        sound_preview_playing_ = false;
        sound_stop_requested_ = true;
        sound_idle_ticks_ = 0;
        sound_requested_index_ = -1;
        StopSoundTimer();
        Application::GetInstance().GetAudioService().ResetDecoder();
    } else {
        quick_return_page_ = PAGE_SWITCH;
    }

    current_page_ = PAGE_QUICK;
    lv_obj_clear_flag(quick_screen_, LV_OBJ_FLAG_HIDDEN);
    UpdateQuickPageUi();
    Application::GetInstance().SetDeviceState(kDeviceStateIdle);
    Board::GetInstance().TurnOffFlashlight();
}

void Display::Switch_Sound_Page(void)
{
    ESP_LOGI(TAG, "Switching to sound page");
    DisplayLockGuard lock(this);

    if (current_page_ == PAGE_SWITCH) {
        if (current_index_ >= 0 && current_index_ < Switch_Count) {
            lv_obj_add_flag(pages[current_index_].page, LV_OBJ_FLAG_HIDDEN);
        }
    } else if (current_page_ == PAGE_FUN) {
        lv_obj_add_flag(fun_screen_, LV_OBJ_FLAG_HIDDEN);
    } else if (current_page_ == PAGE_GAME) {
        lv_obj_add_flag(game_screen_, LV_OBJ_FLAG_HIDDEN);
        game_running_ = false;
        StopGameTimer();
    } else if (current_page_ == PAGE_REFLEX) {
        lv_obj_add_flag(reflex_screen_, LV_OBJ_FLAG_HIDDEN);
        StopReflexTimer();
        reflex_state_ = kReflexIdle;
    } else if (current_page_ == PAGE_BREATHE) {
        lv_obj_add_flag(breathe_screen_, LV_OBJ_FLAG_HIDDEN);
        breathe_running_ = false;
        StopBreatheTimer();
    } else if (current_page_ == PAGE_MEMORY) {
        lv_obj_add_flag(memory_screen_, LV_OBJ_FLAG_HIDDEN);
        StopMemoryTimer();
        memory_state_ = kMemoryIdle;
        memory_active_lane_ = -1;
    } else if (current_page_ == PAGE_QUICK) {
        lv_obj_add_flag(quick_screen_, LV_OBJ_FLAG_HIDDEN);
    }

    current_page_ = PAGE_SOUND;
    sound_autoplay_ = false;
    sound_preview_playing_ = false;
    sound_stop_requested_ = false;
    sound_idle_ticks_ = 0;
    EnsureSoundTimer();
    StopSoundTimer();
    esp_timer_start_periodic(sound_timer_, kSoundTickMs * 1000);
    lv_obj_clear_flag(sound_screen_, LV_OBJ_FLAG_HIDDEN);
    UpdateSoundPageUi();
    Application::GetInstance().SetDeviceState(kDeviceStateIdle);
    Board::GetInstance().TurnOffFlashlight();
}

void Display::Switch_Time_Setting_Page(void)
{
    ESP_LOGI(TAG, "Switching to time setting page");
    DisplayLockGuard lock(this);

    if (current_page_ == PAGE_SWITCH) {
        if (current_index_ >= 0 && current_index_ < Switch_Count) {
            lv_obj_add_flag(pages[current_index_].page, LV_OBJ_FLAG_HIDDEN);
        }
    }

    current_page_ = PAGE_TIME_SETTING;
    time_setting_field_ = 0;

    // 初始化为当前时间
    time_t now;
    struct tm* tm;
    if (offline_mode_) {
        uint64_t elapsed_us = esp_timer_get_time() - offline_time_start_us_;
        now = offline_time_base_ + (elapsed_us / 1000000);
        tm = localtime(&now);
    } else {
        now = time(NULL);
        tm = localtime(&now);
    }

    time_setting_year_ = tm->tm_year + 1900;
    time_setting_month_ = tm->tm_mon + 1;
    time_setting_day_ = tm->tm_mday;
    time_setting_hour_ = tm->tm_hour;
    time_setting_minute_ = tm->tm_min;
    time_setting_second_ = tm->tm_sec;

    if (time_setting_screen_ != nullptr) {
        lv_obj_clear_flag(time_setting_screen_, LV_OBJ_FLAG_HIDDEN);
    }

    TimeSettingAdjust(0);  // 更新UI显示和高亮

    Application::GetInstance().SetDeviceState(kDeviceStateIdle);
    Board::GetInstance().TurnOffFlashlight();
}

