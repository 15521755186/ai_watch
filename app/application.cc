#include "application.h"
#include "board.h"
#include "display.h"
#include "system/system_info.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "font_awesome_symbols.h"
#include "assets/lang_config.h"
#include "services/mcp_server.h"
#include "services/onenet_uploader.h"

#include <cstring>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <wifi_station.h>
#include "power.h"

#define TAG "Application"


static const char* const STATE_STRINGS[] = {
    "unknown",
    "starting",
    "configuring",
    "idle",
    "connecting",
    "listening",
    "speaking",
    "upgrading",
    "activating",
    "audio_testing",
    "fatal_error",
    "invalid_state",
    "health",
    "stop",
    "switch"
};

Application::Application() {
    event_group_ = xEventGroupCreate();//创建事件组
//回声消除模式配置：消除扬声器播放声音对麦克风采集的影响
#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif
//时钟定时器创建
    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            app->OnClockTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);
}

Application::~Application() {
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    vEventGroupDelete(event_group_);
}
/// @brief 检查新版本
/// @param ota Ota对象
void Application::CheckNewVersion(Ota& ota) {
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10; // 初始重试延迟为10秒

    auto& board = Board::GetInstance();
    while (true) {
        SetDeviceState(kDeviceStateActivating);
        auto display = board.GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        if (!ota.CheckVersion()) {
            retry_count++;
            if (retry_count >= MAX_RETRY) {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }

            char buffer[128];
            snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED, retry_delay, ota.GetCheckVersionUrl().c_str());
            Alert(Lang::Strings::ERROR, buffer, "sad", Lang::Sounds::P3_EXCLAMATION);

            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", retry_delay, retry_count, MAX_RETRY);
            for (int i = 0; i < retry_delay; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (device_state_ == kDeviceStateIdle) {
                    break;
                }
            }
            retry_delay *= 2;
            if (retry_delay > 60) retry_delay = 60; // 上限 60 秒，防止无限增长
            continue;
        }
        retry_count = 0;
        retry_delay = 10; // 重置重试延迟时间

        if (ota.HasNewVersion()) {
        }

        // No new version, mark the current version as valid
        ota.MarkCurrentVersionValid();
        if (!ota.HasActivationCode() && !ota.HasActivationChallenge()) {
            xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
            // Exit the loop if done checking new version
            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        // Activation code is shown to the user and waiting for the user to input
        if (ota.HasActivationCode()) {
            ShowActivationCode(ota.GetActivationCode(), ota.GetActivationMessage());
        }

        // This will block the loop until the activation is done or timeout
        for (int i = 0; i < 10; ++i) {
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            esp_err_t err = ota.Activate();
            if (err == ESP_OK) {
                xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
                break;
            } else if (err == ESP_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (device_state_ == kDeviceStateIdle) {
                break;
            }
        }
    }
}
/// @brief 显示激活码
/// @param code 激活码
/// @param message 激活消息
void Application::ShowActivationCode(const std::string& code, const std::string& message) {
    // This sentence uses 9KB of SRAM, so we need to wait for it to finish
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "happy", Lang::Sounds::P3_ACTIVATION);
}
/// @brief 显示告警信息
/// @param status 状态
/// @param message 消息内容
/// @param emotion 表情
/// @param sound 提示音
void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert %s: %s [%s]", status, message, emotion);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (!sound.empty()) {
        audio_service_.PlaySound(sound);
    }
}

/// @brief 取消显示告警信息	
/// @param status 状态
/// @param message 消息内容
/// @param emotion 表情
/// @param sound 提示音
void Application::DismissAlert() {
    if (device_state_ == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}
/// @brief 切换聊天状态音频通道的管理和模式的设置
void Application::ToggleChatState() {
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    } else if (device_state_ == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            }

            SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {
        Schedule([this]() {
            protocol_->CloseAudioChannel();
        });
    }
}
/// @brief 启动监听
void Application::StartListening() {
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }
    
    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            }

            SetListeningMode(kListeningModeManualStop);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
            SetListeningMode(kListeningModeManualStop);
        });
    }
}

void Application::StopListening() {
    if (device_state_ == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    const std::array<int, 3> valid_states = {
        kDeviceStateListening,
        kDeviceStateSpeaking,
        kDeviceStateIdle,
    };
    // If not valid, do nothing
    if (std::find(valid_states.begin(), valid_states.end(), device_state_) == valid_states.end()) {
        return;
    }

    Schedule([this]() {
        if (device_state_ == kDeviceStateListening) {
            protocol_->SendStopListening();
            SetDeviceState(kDeviceStateIdle);
        }
    });
}

/**
 * @brief 启动应用程序
 * 
 * 此方法是应用程序的核心启动函数，负责初始化和启动各个子系统，
 * 包括显示、音频、网络和通信协议等。
 */
void Application::Start() {
    // 获取板级实例（单例模式）
    auto& board = Board::GetInstance();
    /* 设置显示设备 */
    auto display = board.GetDisplay();
    if (display == nullptr) {
        ESP_LOGE(TAG, "Display is null, cannot start application");
        return;
    }
    display->ShowBootScreen();
    display->UpDateBattery();

    /* 设置音频服务 */
    auto codec = board.GetAudioCodec();
    if (codec == nullptr) {
        ESP_LOGE(TAG, "Audio codec is null, cannot start audio");
        return;
    }
    audio_service_.Initialize(codec);
    // 启动音频服务
    audio_service_.Start();
    
    // 设置音频服务回调
    AudioServiceCallbacks callbacks;
    // 发送队列可用回调
    callbacks.on_send_queue_available = [this, display]() {
        if (display->GetCurrentPage() == PAGE_CHAT)
            xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
    };
    // 唤醒词检测回调
    callbacks.on_wake_word_detected = [this, display](const std::string& wake_word) {
        if (display->GetCurrentPage() == PAGE_CHAT)
            xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
    };
    // 语音活动检测变化回调
    callbacks.on_vad_change = [this, display](bool speaking) {
        if (display->GetCurrentPage() == PAGE_CHAT)
            xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
    };
    // 注册回调
    audio_service_.SetCallbacks(callbacks);

    /* 启动时钟定时器以更新状态栏 */
    esp_timer_start_periodic(clock_timer_handle_, 1000000); // 1秒间隔
    
    /* 等待网络准备就绪 */
    board.StartNetwork();

    // 立即更新状态栏以显示网络状态
    display->UpdateStatusBar(true);

    // 检查网络连接状态（通过 WifiStation 检查）
    bool network_available = false;
    if (board.GetBoardType() == "wifi") {
        auto& wifi_station = WifiStation::GetInstance();
        network_available = wifi_station.IsConnected();
    } else {
        // 对于其他网络类型，假设网络可用
        network_available = true;
    }

    // 根据网络状态设置显示模式
    if (network_available) {
        display->SetOnlineMode(true);
        ESP_LOGI(TAG, "Network available, using online mode");
    } else {
        display->SetOnlineMode(false);
        ESP_LOGI(TAG, "Network not available, using offline mode with default time");
    }

    // 只有在网络可用时才进行版本检查和协议初始化
    if (network_available) {
        // 检查新固件版本或获取MQTT代理地址
        Ota ota;
        CheckNewVersion(ota);

        // 初始化通信协议
        display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

        // 在初始化协议前添加MCP通用工具
        McpServer::GetInstance().AddCommonTools();

        // 根据OTA配置选择通信协议
        if (ota.HasMqttConfig()) {
            protocol_ = std::make_unique<MqttProtocol>();
        } else if (ota.HasWebsocketConfig()) {
            protocol_ = std::make_unique<WebsocketProtocol>();
        } else {
            ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
            protocol_ = std::make_unique<MqttProtocol>();
        }

        // 设置协议回调
        // 网络错误回调
        protocol_->OnNetworkError([this](const std::string& message) {
            last_error_message_ = message;
            xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
        });
        // 接收音频回调
        protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
            if (device_state_ == kDeviceStateSpeaking) {
                audio_service_.PushPacketToDecodeQueue(std::move(packet));
            }
        });
        // 音频通道打开回调
        protocol_->OnAudioChannelOpened([this, codec, &board]() {
            board.SetPowerSaveMode(false);
            // 确保音频输出已启用（本地音乐播放后可能关闭了输出）
            if (!codec->output_enabled()) {
                codec->EnableOutput(true);
            }
            if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
                ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                    protocol_->server_sample_rate(), codec->output_sample_rate());
            }
        });
        // 音频通道关闭回调
        protocol_->OnAudioChannelClosed([this, &board]() {
            board.SetPowerSaveMode(true);
            Schedule([this]() {
                auto display = Board::GetInstance().GetDisplay();
                display->SetChatMessage("system", "");
                SetDeviceState(kDeviceStateIdle);
            });
        });
        // 接收JSON消息回调
        protocol_->OnIncomingJson([this, display](const cJSON* root) {
            // 解析JSON数据
            auto type = cJSON_GetObjectItem(root, "type");
            if (strcmp(type->valuestring, "tts") == 0) {
                // 处理文本转语音消息
                auto state = cJSON_GetObjectItem(root, "state");
                if (strcmp(state->valuestring, "start") == 0) {
                    // TTS开始
                    Schedule([this]() {
                        aborted_ = false;
                        if (device_state_ == kDeviceStateIdle || device_state_ == kDeviceStateListening) {
                            SetDeviceState(kDeviceStateSpeaking);
                        }
                    });
                } else if (strcmp(state->valuestring, "stop") == 0) {
                    // TTS停止
                    Schedule([this]() {
                        if (device_state_ == kDeviceStateSpeaking) {
                            if (listening_mode_ == kListeningModeManualStop) {
                                SetDeviceState(kDeviceStateIdle);
                            } else {
                                SetDeviceState(kDeviceStateListening);
                            }
                        }
                    });
                } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                    // 句子开始
                    auto text = cJSON_GetObjectItem(root, "text");
                    if (cJSON_IsString(text)) {
                        ESP_LOGI(TAG, "<< %s", text->valuestring);
                        Schedule([this, display, message = std::string(text->valuestring)]() {
                            display->SetChatMessage("assistant", message.c_str());
                        });
                    }
                }
            } else if (strcmp(type->valuestring, "stt") == 0) {
                // 处理语音转文本消息
                auto text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    ESP_LOGI(TAG, ">> %s", text->valuestring);
                    Schedule([this, display, message = std::string(text->valuestring)]() {
                        display->SetChatMessage("user", message.c_str());
                    });
                }
            } else if (strcmp(type->valuestring, "llm") == 0) {
                // 处理大语言模型消息
                auto emotion = cJSON_GetObjectItem(root, "emotion");
                if (cJSON_IsString(emotion)) {
                    Schedule([this, display, emotion_str = std::string(emotion->valuestring)]() {
                        display->SetEmotion(emotion_str.c_str());
                    });
                }
            } else if (strcmp(type->valuestring, "mcp") == 0) {
                // 处理MCP设备控制消息
                auto payload = cJSON_GetObjectItem(root, "payload");
                if (cJSON_IsObject(payload)) {
                    McpServer::GetInstance().ParseMessage(payload);
                }
            } else if (strcmp(type->valuestring, "system") == 0) {
                // 处理系统命令
                auto command = cJSON_GetObjectItem(root, "command");
                if (cJSON_IsString(command)) {
                    ESP_LOGI(TAG, "System command: %s", command->valuestring);
                    if (strcmp(command->valuestring, "reboot") == 0) {
                        // 重启设备
                        Schedule([this]() {
                            Reboot();
                        });
                    } else {
                        ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                    }
                }
            } else if (strcmp(type->valuestring, "alert") == 0) {
                // 处理告警消息
                auto status = cJSON_GetObjectItem(root, "status");
                auto message = cJSON_GetObjectItem(root, "message");
                auto emotion = cJSON_GetObjectItem(root, "emotion");
                if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                    Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::P3_VIBRATION);
                } else {
                    ESP_LOGW(TAG, "Alert command requires status, message and emotion");
                }
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
            } else if (strcmp(type->valuestring, "custom") == 0) {
                // 处理自定义消息
                auto payload = cJSON_GetObjectItem(root, "payload");
                ESP_LOGI(TAG, "Received custom message: %s", cJSON_PrintUnformatted(root));
                if (cJSON_IsObject(payload)) {
                    Schedule([this, display, payload_str = std::string(cJSON_PrintUnformatted(payload))]() {
                        display->SetChatMessage("system", payload_str.c_str());
                    });
                } else {
                    ESP_LOGW(TAG, "Invalid custom message format: missing payload");
                }
#endif
            } else {
                // 未知消息类型
                ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
            }
        });

        // 启动协议
        bool protocol_started = protocol_->Start();

        // 检查是否有服务器时间配置
        has_server_time_ = ota.HasServerTime();

        if (protocol_started) {
            // 协议启动成功
            std::string message = std::string(Lang::Strings::VERSION) + ota.GetCurrentVersion();
            // 播放成功提示音
            audio_service_.PlaySound(Lang::Sounds::P3_SUCCESS);
        }
    } else {
        ESP_LOGW(TAG, "Network not available, skipping protocol initialization");
        display->SetStatus(Lang::Strings::STANDBY);
    }
    
    // 更新显示信息
    display->UpdateDate();     // 更新日期
    display->UpdateTime();     // 更新时间
    display->UpdateWeather();  // 更新天气
    display->UpDateNetWork();  // 更新网络状态
    display->UpDateBattery();  // 更新电池状态
    display->Switch_Main_Page();  // 切换到主页
    OneNetUploader::GetInstance().Start();

    // 打印堆内存状态
    SystemInfo::PrintHeapStats();

    // 进入主事件循环
    MainEventLoop();
}
/// @brief 时钟定时器回调函数
void Application::OnClockTimer() {
    clock_ticks_++;

    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar();
    display->CheckAlarm();  // 闹钟检查合并到主时钟，省掉独立 1s 定时器
    if (display->GetCurrentPage() == PAGE_INIT)
        return ;
    if (display->GetCurrentPage() == PAGE_MAIN)
    {
        display->UpdateTime();
        if (clock_ticks_ % 600 == 0) // Update date every 10 minutes
            display->UpdateWeather();
    }
    // 每 10 秒更新电池状态（降低 UI 刷新频率）
    if (clock_ticks_ % 10 == 0) {
        display->UpDateBattery();
    }
    // 每 60 秒打印一次堆内存统计（减少日志/NVS 开销）
    if (clock_ticks_ % 60 == 0) {
        SystemInfo::PrintHeapStats();
    }

    // 低功耗：空闲 30 秒自动降低背光
    auto backlight = Board::GetInstance().GetBacklight();
    if (backlight != nullptr) {
        if (device_state_ == kDeviceStateIdle) {
            idle_ticks_++;
            if (idle_ticks_ == 30) {
                backlight->SetBrightness(10, false);  // 降到 10% 但不保存
            }
        } else {
            if (idle_ticks_ >= 30) {
                backlight->RestoreBrightness();  // 恢复用户设置的亮度
            }
            idle_ticks_ = 0;
        }
    }
}
/// @brief 添加异步任务到主循环
// Add a async task to MainLoop
void Application::Schedule(std::function<void()> callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

/// @brief 主事件循环
void Application::MainEventLoop() {

    vTaskPrioritySet(NULL, 3);

    while (true) {

        auto bits = xEventGroupWaitBits(event_group_, MAIN_EVENT_SCHEDULE |
            MAIN_EVENT_SEND_AUDIO |
            MAIN_EVENT_WAKE_WORD_DETECTED |
            MAIN_EVENT_VAD_CHANGE |
            MAIN_EVENT_ERROR, pdTRUE, pdFALSE, portMAX_DELAY);
        if (bits & MAIN_EVENT_ERROR) {
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "sad", Lang::Sounds::P3_EXCLAMATION);
        }

        if (bits & MAIN_EVENT_SEND_AUDIO) {
            while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                if (!protocol_->SendAudio(std::move(packet))) {
                    break;
                }
            }
        }

        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            OnWakeWordDetected();
        }

        if (bits & MAIN_EVENT_VAD_CHANGE) {
            if (device_state_ == kDeviceStateListening) {
                // auto led = Board::GetInstance().GetLed();
                // led->OnStateChanged();
            }
        }

        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }
    }
}
/// @brief 处理唤醒词检测事件
void Application::OnWakeWordDetected() {
	//先检查是不是聊天界面
    auto display = Board::GetInstance().GetDisplay();
    if (display->GetCurrentPage() != PAGE_CHAT)
    { 
        return;
    }
    if (!protocol_) {
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                audio_service_.EnableWakeWordDetection(true);
                return;
            }
        }

        auto wake_word = audio_service_.GetLastWakeWord();
        ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_USE_AFE_WAKE_WORD || CONFIG_USE_CUSTOM_WAKE_WORD
        // Encode and send the wake word data to the server
        while (auto packet = audio_service_.PopWakeWordPacket()) {
            protocol_->SendAudio(std::move(packet));
        }
        // Set the chat state to wake word detected
        protocol_->SendWakeWordDetected(wake_word);
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#else
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        // Play the pop up sound to indicate the wake word is detected
        audio_service_.PlaySound(Lang::Sounds::P3_POPUP);
#endif
    } else if (device_state_ == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonWakeWordDetected);
    } else if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
    }
}
/// @brief 中止正在播放的语音
/// @param reason 中止原因
void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    protocol_->SendAbortSpeaking(reason);
}
/// @brief 设置监听模式
/// @param mode 监听模式
void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}
/// @brief 设置设备状态
/// @param state 设备状态
void Application::SetDeviceState(DeviceState state) {
    if (device_state_ == state) {
        return;
    }
    
    clock_ticks_ = 0;
    auto previous_state = device_state_;
    device_state_ = state;
    ESP_LOGI(TAG, "STATE: %s", STATE_STRINGS[device_state_]);

    // Send the state change event
    DeviceStateEventManager::GetInstance().PostStateChangeEvent(previous_state, state);
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    switch (state) {
        case kDeviceStateSwitch:
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(false);
            break;
        case kDeviceStateStop:
            if (protocol_) {  
                protocol_->CloseAudioChannel();
            } 
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(false);
            break;
        case kDeviceStateHealth:
            if (healthTaskHandle != nullptr) {
                vTaskResume(healthTaskHandle);  // 已存在则恢复，避免重复创建
            } else {
                xTaskCreatePinnedToCore([](void* arg) {
                    auto display = Board::GetInstance().GetDisplay();
                    auto max30102 = Board::GetInstance().GetMAX30102();
                    while(1)
                    {
                        if (display->GetCurrentPage() != PAGE_DETE)
                        {
                            max30102->max30102_deconfig();
                            vTaskSuspend(NULL);
                        }
                        else if (max30102->max30102_read_data() == ESP_OK)
                        {
                            display->UpDateHealth();
                        }
                        vTaskDelay(pdMS_TO_TICKS(200));
                    }
                }, "health", 4096, NULL, 2, &healthTaskHandle, 0);
            }
            break;
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
                display->SetStatus(Lang::Strings::STANDBY);
                display->SetEmotion("neutral");
                audio_service_.EnableVoiceProcessing(false);
                audio_service_.EnableWakeWordDetection(true);
            break;
        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening:
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("neutral");

            // Make sure the audio processor is running
            if (!audio_service_.IsAudioProcessorRunning()) {
                // Send the start listening command
                protocol_->SendStartListening(listening_mode_);
                audio_service_.EnableVoiceProcessing(true);
                audio_service_.EnableWakeWordDetection(false);
            }
            break;
        case kDeviceStateSpeaking:
            display->SetStatus(Lang::Strings::SPEAKING);

            if (listening_mode_ != kListeningModeRealtime) {
                audio_service_.EnableVoiceProcessing(false);
                // Only AFE wake word can be detected in speaking mode
#if CONFIG_USE_AFE_WAKE_WORD
                audio_service_.EnableWakeWordDetection(true);
#else
                audio_service_.EnableWakeWordDetection(false);
#endif
            }
            audio_service_.ResetDecoder();
            break;
        default:
            // Do nothing
            break;
    }
}
/// @brief 重启设备	
void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    esp_restart();
}
/// @brief 唤醒词触发
void Application::WakeWordInvoke(const std::string& wake_word) {
    if (device_state_ == kDeviceStateIdle) {
        ToggleChatState();
        Schedule([this, wake_word]() {
            if (protocol_) {
                protocol_->SendWakeWordDetected(wake_word); 
            }
        }); 
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {   
        Schedule([this]() {
            if (protocol_) {
                protocol_->CloseAudioChannel();
            }
        });
    }
}
/// @brief 检查是否可以进入睡眠模式
/// @return 是否可以进入睡眠模式
bool Application::CanEnterSleepMode() {
    if (device_state_ != kDeviceStateIdle) {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;
    }

    if (!audio_service_.IsIdle()) {
        return false;
    }

    // Now it is safe to enter sleep mode
    return true;
}
/// @brief 发送MCP消息
/// @param payload MCP消息负载
void Application::SendMcpMessage(const std::string& payload) {
    Schedule([this, payload]() {
        if (protocol_) {
            protocol_->SendMcpMessage(payload);
        }
    });
}
/// @brief 设置AEC模式
/// @param mode AEC模式
void Application::SetAecMode(AecMode mode) {
    aec_mode_ = mode;
    Schedule([this]() {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        switch (aec_mode_) {
        case kAecOff:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
            break;
        case kAecOnServerSide:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        case kAecOnDeviceSide:
            audio_service_.EnableDeviceAec(true);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        }

        // If the AEC mode is changed, close the audio channel
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
    });
}
/// @brief 播放声音
/// @param sound 声音名称
void Application::PlaySound(const std::string_view& sound) {
    audio_service_.PlaySound(sound);
}
