/**
 * @file application.h
 * @brief 应用程序主类定义
 * 
 * 此类是整个系统的核心，负责协调各个子系统的工作，
 * 管理设备状态、网络连接、语音交互等功能。
 * 采用单例模式实现，确保整个应用程序只有一个实例。
 */

#ifndef _APPLICATION_H_ 
#define _APPLICATION_H_ 

// FreeRTOS 相关头文件
#include <freertos/FreeRTOS.h>     // FreeRTOS 核心功能
#include <freertos/event_groups.h> // 事件组
#include <freertos/task.h>         // 任务管理
#include <esp_timer.h>             // ESP32 定时器

// C++ 标准库
#include <string>                  // 字符串
#include <mutex>                   // 互斥锁
#include <deque>                   // 双端队列
#include <vector>                  // 向量
#include <memory>                  // 智能指针

// 项目头文件
#include "protocol.h"              // 网络通信协议
#include "services/ota.h"          // 固件升级
#include "audio_service.h"         // 音频服务
#include "device_state_event.h"    // 设备状态事件

// 事件定义
#define MAIN_EVENT_SCHEDULE (1 << 0)                // 调度事件
#define MAIN_EVENT_SEND_AUDIO (1 << 1)              // 发送音频事件
#define MAIN_EVENT_WAKE_WORD_DETECTED (1 << 2)       // 唤醒词检测事件
#define MAIN_EVENT_VAD_CHANGE (1 << 3)              // 语音活动检测变化事件
#define MAIN_EVENT_ERROR (1 << 4)                   // 错误事件
#define MAIN_EVENT_CHECK_NEW_VERSION_DONE (1 << 5)   // 检查新版本完成事件

/**
 * @enum AecMode
 * @brief 回声消除模式枚举
 * 
 * 定义了系统支持的回声消除模式。
 */
enum AecMode { 
    kAecOff,              // 关闭回声消除
    kAecOnDeviceSide,     // 设备端回声消除
    kAecOnServerSide,     // 服务端回声消除
}; 

/**
 * @class Application
 * @brief 应用程序主类
 * 
 * 采用单例模式实现，管理整个应用程序的生命周期和各个子系统的协调。
 */
class Application { 
public: 
    /**
     * @brief 获取应用实例
     * 
     * 单例模式的核心方法，返回应用的唯一实例。
     * @return Application& 应用实例引用
     */
    static Application& GetInstance() { 
        static Application instance; 
        return instance; 
    } 
    
    // 删除拷贝构造函数和赋值运算符，确保单例模式
    Application(const Application&) = delete; 
    Application& operator=(const Application&) = delete; 
    
    /**
     * @brief 启动应用
     * 
     * 初始化各个子系统并启动应用程序。
     */
    void Start(); 
    
    /**
     * @brief 获取设备状态
     * @return DeviceState 当前设备状态
     */
    DeviceState GetDeviceState() const { return device_state_; } 
    
    /**
     * @brief 检查是否检测到语音
     * @return bool 是否检测到语音
     */
    bool IsVoiceDetected() const { return audio_service_.IsVoiceDetected(); } 
    
    /**
     * @brief 调度任务到主事件循环
     * @param callback 要执行的回调函数
     */
    void Schedule(std::function<void()> callback); 
    
    /**
     * @brief 设置设备状态
     * @param state 新的设备状态
     */
    void SetDeviceState(DeviceState state); 
    
    /**
     * @brief 显示告警信息
     * @param status 状态
     * @param message 消息内容
     * @param emotion 表情
     * @param sound 提示音
     */
    void Alert(const char* status, const char* message, const char* emotion = "", const std::string_view& sound = ""); 
    
    /**
     * @brief  dismiss 告警信息
     */
    void DismissAlert(); 
    
    /**
     * @brief 中止说话
     * @param reason 中止原因
     */
    void AbortSpeaking(AbortReason reason); 
    
    /**
     * @brief 切换聊天状态
     */
    void ToggleChatState(); 
    
    /**
     * @brief 开始监听
     */
    void StartListening(); 
    
    /**
     * @brief 停止监听
     */
    void StopListening(); 
    
    /**
     * @brief 重启设备
     */
    void Reboot(); 
    
    /**
     * @brief 唤醒词调用
     * @param wake_word 唤醒词
     */
    void WakeWordInvoke(const std::string& wake_word); 
    
    /**
     * @brief 检查是否可以进入睡眠模式
     * @return bool 是否可以进入睡眠模式
     */
    bool CanEnterSleepMode(); 
    
    /**
     * @brief 发送 MCP 消息
     * @param payload 消息内容
     */
    void SendMcpMessage(const std::string& payload); 
    
    /**
     * @brief 设置回声消除模式
     * @param mode 回声消除模式
     */
    void SetAecMode(AecMode mode); 
    
    /**
     * @brief 获取回声消除模式
     * @return AecMode 当前回声消除模式
     */
    AecMode GetAecMode() const { return aec_mode_; } 
    
    /**
     * @brief 播放提示音
     * @param sound 提示音
     */
    void PlaySound(const std::string_view& sound); 
    
    /**
     * @brief 获取音频服务
     * @return AudioService& 音频服务引用
     */
    AudioService& GetAudioService() { return audio_service_; } 

private: 
    /**
     * @brief 私有构造函数
     * 
     * 防止外部直接创建实例，确保单例模式的实现。
     */
    Application(); 
    
    /**
     * @brief 析构函数
     */
    ~Application(); 

    // 成员变量
    std::mutex mutex_;                          // 互斥锁，保护任务队列
    std::deque<std::function<void()>> main_tasks_; // 主任务队列
    std::unique_ptr<Protocol> protocol_;        // 网络通信协议
    EventGroupHandle_t event_group_;            // 事件组
    esp_timer_handle_t clock_timer_handle_;     // 时钟定时器
    volatile DeviceState device_state_;         // 当前设备状态
    ListeningMode listening_mode_;              // 监听模式
    AecMode aec_mode_;                          // 回声消除模式
    std::string last_error_message_;            // 最后错误消息
    AudioService audio_service_;                // 音频服务
    
    bool has_server_time_;                      // 是否有服务器时间
    bool aborted_;                              // 是否中止
    int clock_ticks_ = 0;                       // 时钟 ticks
    int idle_ticks_ = 0;                        // 空闲秒数（用于低功耗背光控制）
    TaskHandle_t check_new_version_task_handle_ = nullptr; // 检查新版本任务
    TaskHandle_t healthTaskHandle = nullptr;              // 健康检测任务

    // 私有方法
    void MainEventLoop();                       // 主事件循环
    void OnWakeWordDetected();                  // 唤醒词检测回调
    void CheckNewVersion(Ota& ota);             // 检查新版本
    void ShowActivationCode(const std::string& code, const std::string& message); // 显示激活码
    void OnClockTimer();                        // 时钟定时器回调
    void SetListeningMode(ListeningMode mode);  // 设置监听模式
}; 

#endif // _APPLICATION_H_
