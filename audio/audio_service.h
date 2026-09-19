// ==================== 音频服务头文件 ====================
// 此文件定义了音频服务的核心类,负责音频采集、编码、解码、播放等功能
// 
// 音频数据流说明:
// 1. 麦克风音频流: (MIC) -> [处理器] -> {编码队列} -> [Opus编码器] -> {发送队列} -> (服务器)
// 2. 扬声器音频流: (服务器) -> {解码队列} -> [Opus解码器] -> {播放队列} -> (扬声器)
//
// 任务架构:
// - AudioInputTask: 负责从麦克风采集音频数据
// - AudioOutputTask: 负责向扬声器播放音频数据
// - OpusCodecTask: 负责音频编码和解码

#ifndef AUDIO_SERVICE_H
#define AUDIO_SERVICE_H

// C++标准库
#include <memory>               // 智能指针
#include <deque>                // 双端队列
#include <condition_variable>   // 条件变量
#include <chrono>               // 时间处理
#include <mutex>                // 互斥锁

// FreeRTOS库
#include <freertos/FreeRTOS.h>     // FreeRTOS核心功能
#include <freertos/task.h>         // 任务管理
#include <freertos/event_groups.h> // 事件组
#include <esp_timer.h>             // ESP32定时器

// Opus音频编解码库
#include <opus_encoder.h>  // Opus编码器
#include <opus_decoder.h>  // Opus解码器
#include <opus_resampler.h> // Opus重采样器

// 项目头文件
#include "audio_codec.h"        // 音频编解码器(硬件抽象)
#include "audio_processor.h"    // 音频处理器(回声消除、降噪等)
#include "processors/audio_debugger.h" // 音频调试器
#include "wake_word.h"          // 唤醒词检测
#include "protocol.h"           // 网络通信协议


/*
 * ==================== 音频数据流架构说明 ====================
 * 
 * 本系统有两种音频数据流:
 * 
 * 1. 麦克风音频流(上传):
 *    (麦克风) -> [音频处理器] -> {编码队列} -> [Opus编码器] -> {发送队列} -> (服务器)
 *    
 * 2. 扬声器音频流(下载):
 *    (服务器) -> {解码队列} -> [Opus解码器] -> {播放队列} -> (扬声器)
 *
 * ==================== 任务架构 ====================
 * 
 * 我们使用3个主要任务:
 * - AudioInputTask: 负责麦克风采集和音频处理
 * - AudioOutputTask: 负责扬声器播放
 * - OpusCodecTask: 负责Opus编码和解码
 * 
 * ==================== 队列说明 ====================
 * 
 * 解码队列和发送队列是主要的队列,因为:
 * - Opus数据包比PCM数据包小得多
 * - 使用较小的队列可以节省内存
 * - PCM数据包在编码前会先经过处理,不需要长期存储
 * 
 */

// ==================== 音频参数配置 ====================

// Opus音频帧时长(毫秒)
// 每个Opus帧包含60毫秒的音频数据
#define OPUS_FRAME_DURATION_MS 60

// 编码任务队列最大容量
// 限制编码队列的大小,防止内存占用过高
#define MAX_ENCODE_TASKS_IN_QUEUE 2

// 播放任务队列最大容量
// 限制播放队列的大小,防止延迟过大
#define MAX_PLAYBACK_TASKS_IN_QUEUE 2

// 解码队列最大容量(以Opus包为单位)
// 可以存储2400毫秒(2.4秒)的音频数据
#define MAX_DECODE_PACKETS_IN_QUEUE (2400 / OPUS_FRAME_DURATION_MS)

// 发送队列最大容量(以Opus包为单位)
// 可以存储2400毫秒(2.4秒)的音频数据
#define MAX_SEND_PACKETS_IN_QUEUE (2400 / OPUS_FRAME_DURATION_MS)

// 音频测试最大时长(毫秒)
// 音频测试模式最多运行10秒
#define AUDIO_TESTING_MAX_DURATION_MS 10000

// 时间戳队列最大容量
// 用于服务端回声消除的时间戳同步
#define MAX_TIMESTAMPS_IN_QUEUE 3

// 音频超时时间(毫秒)
// 如果15秒内没有音频输入或输出,认为音频系统异常
#define AUDIO_POWER_TIMEOUT_MS 15000

// 音频状态检查间隔(毫秒)
// 每秒检查一次音频系统的状态
#define AUDIO_POWER_CHECK_INTERVAL_MS 1000


// ==================== 音频服务事件定义 ====================
// 使用事件组来同步不同的音频功能状态

// 音频测试正在运行
// 当设备处于音频测试模式时,此事件被设置
#define AS_EVENT_AUDIO_TESTING_RUNNING      (1 << 0)

// 唤醒词检测正在运行
// 当唤醒词检测功能启用时,此事件被设置
#define AS_EVENT_WAKE_WORD_RUNNING          (1 << 1)

// 音频处理器正在运行
// 当音频处理器(回声消除、降噪等)启用时,此事件被设置
#define AS_EVENT_AUDIO_PROCESSOR_RUNNING    (1 << 2)

// 播放队列不为空
// 当有音频数据正在播放时,此事件被设置
#define AS_EVENT_PLAYBACK_NOT_EMPTY         (1 << 3)

// ==================== 音频服务回调函数结构 ====================
// 此结构定义了音频服务向应用程序报告事件的回调函数
struct AudioServiceCallbacks {
    // 回调1:发送队列可用
    // 当音频发送队列有空位时调用,表示可以发送更多音频数据到服务器
    std::function<void(void)> on_send_queue_available;
    
    // 回调2:检测到唤醒词
    // 当检测到唤醒词(如"小智小智")时调用,参数是唤醒词字符串
    std::function<void(const std::string&)> on_wake_word_detected;
    
    // 回调3:语音活动检测(VAD)变化
    // 当检测到用户开始说话或停止说话时调用,参数为true表示正在说话
    std::function<void(bool)> on_vad_change;
    
    // 回调4:音频测试队列已满
    // 当音频测试队列已满时调用,表示不能再接收更多测试音频
    std::function<void(void)> on_audio_testing_queue_full;
};


// ==================== 音频任务类型枚举 ====================
// 定义了不同类型的音频任务
enum AudioTaskType {
    kAudioTaskTypeEncodeToSendQueue,    // 编码并发送到服务器
    kAudioTaskTypeEncodeToTestingQueue, // 编码并发送到测试队列
    kAudioTaskTypeDecodeToPlaybackQueue, // 解码并发送到播放队列
};

// ==================== 音频任务结构 ====================
// 表示一个待处理的音频任务
struct AudioTask {
    AudioTaskType type;           // 任务类型
    std::vector<int16_t> pcm;     // PCM音频数据(原始音频数据)
    uint32_t timestamp;           // 时间戳(用于同步)
};

// ==================== 调试统计信息结构 ====================
// 用于记录音频系统的运行统计信息
struct DebugStatistics {
    uint32_t input_count = 0;    // 输入音频帧计数
    uint32_t decode_count = 0;   // 解码音频帧计数
    uint32_t encode_count = 0;   // 编码音频帧计数
    uint32_t playback_count = 0; // 播放音频帧计数
};

// ==================== 音频服务类 ====================
// 此类是音频系统的核心,负责管理音频采集、编码、解码、播放等功能
class AudioService {
public:
    // 构造函数
    AudioService();
    
    // 析构函数
    ~AudioService();

    // ==================== 初始化和控制方法 ====================
    
    // 初始化音频服务
    // @param codec 音频编解码器指针(负责硬件音频输入输出)
    void Initialize(AudioCodec* codec);
    
    // 启动音频服务
    // 创建并启动音频采集、播放、编码解码任务
    void Start();
    
    // 停止音频服务
    // 停止所有音频任务并释放资源
    void Stop();
    
    // ==================== 唤醒词相关方法 ====================
    
    // 编码唤醒词音频
    // 将检测到的唤醒词音频编码并发送到服务器
    void EncodeWakeWord();
    
    // 从队列中取出唤醒词数据包
    // @return 唤醒词音频数据包
    std::unique_ptr<AudioStreamPacket> PopWakeWordPacket();
    
    // 获取最后检测到的唤醒词
    // @return 唤醒词字符串
    const std::string& GetLastWakeWord() const;
    
    // ==================== 状态查询方法 ====================
    
    // 检查是否检测到语音
    // @return true表示检测到语音活动
    bool IsVoiceDetected() const { return voice_detected_; }
    
    // 检查音频服务是否空闲
    // @return true表示没有音频活动
    bool IsIdle();
    
    // 检查唤醒词检测是否正在运行
    // @return true表示唤醒词检测已启用
    bool IsWakeWordRunning() const { return xEventGroupGetBits(event_group_) & AS_EVENT_WAKE_WORD_RUNNING; }
    
    // 检查音频处理器是否正在运行
    // @return true表示音频处理器(回声消除等)已启用
    bool IsAudioProcessorRunning() const { return xEventGroupGetBits(event_group_) & AS_EVENT_AUDIO_PROCESSOR_RUNNING; }

    // ==================== 功能控制方法 ====================
    
    // 启用或禁用唤醒词检测
    // @param enable true表示启用,false表示禁用
    void EnableWakeWordDetection(bool enable);
    
    // 启用或禁用语音处理(回声消除、降噪等)
    // @param enable true表示启用,false表示禁用
    void EnableVoiceProcessing(bool enable);
    
    // 启用或禁用音频测试模式
    // @param enable true表示启用,false表示禁用
    void EnableAudioTesting(bool enable);
    
    // 启用或禁用设备端回声消除
    // @param enable true表示启用,false表示禁用
    void EnableDeviceAec(bool enable);

    // ==================== 回调函数设置 ====================
    
    // 设置音频服务回调函数
    // @param callbacks 回调函数结构体
    void SetCallbacks(AudioServiceCallbacks& callbacks);

    // ==================== 音频数据队列操作 ====================
    
    // 将音频数据包推送到解码队列
    // @param packet 音频数据包
    // @param wait 是否等待队列有空位
    // @return true表示成功,false表示失败
    bool PushPacketToDecodeQueue(std::unique_ptr<AudioStreamPacket> packet, bool wait = false);
    
    // 从发送队列中取出音频数据包
    // @return 音频数据包
    std::unique_ptr<AudioStreamPacket> PopPacketFromSendQueue();
    
    // ==================== 音频播放方法 ====================
    
    // 播放提示音
    // @param sound 音频文件路径
    void PlaySound(const std::string_view& sound);
    
    // 读取音频数据
    // @param data 输出缓冲区
    // @param sample_rate 采样率
    // @param samples 采样点数
    // @return true表示成功,false表示失败
    bool ReadAudioData(std::vector<int16_t>& data, int sample_rate, int samples);
    
    // 重置解码器
    // 清除解码队列并重置解码器状态
    void ResetDecoder();

    // 停止当前播放
    // 清空播放相关队列并终止当前提示音/铃声继续排队
    void StopPlayback();
    
    // 立即停止所有音频输出
    // 用于紧急停止(如闹钟铃声)  
    void StopAudioImmediately();

private:
    // ==================== 音频硬件和处理器 ====================
    
    // 音频编解码器指针
    // 负责与硬件音频设备(麦克风、扬声器)交互
    AudioCodec* codec_ = nullptr;
    
    // 音频服务回调函数
    // 用于向应用程序报告音频事件
    AudioServiceCallbacks callbacks_;
    
    // 音频处理器指针
    // 负责回声消除、降噪等音频处理
    std::unique_ptr<AudioProcessor> audio_processor_;
    
    // 唤醒词检测器指针
    // 负责检测唤醒词(如"小智小智")
    std::unique_ptr<WakeWord> wake_word_;
    
    // 音频调试器指针
    // 用于调试音频系统
    std::unique_ptr<AudioDebugger> audio_debugger_;
    
    // Opus编码器指针
    // 负责将PCM音频数据编码为Opus格式
    std::unique_ptr<OpusEncoderWrapper> opus_encoder_;
    
    // Opus解码器指针
    // 负责将Opus格式音频数据解码为PCM
    std::unique_ptr<OpusDecoderWrapper> opus_decoder_;
    
    // 输入重采样器
    // 用于将麦克风采集的音频重采样到目标采样率
    OpusResampler input_resampler_;
    
    // 参考重采样器
    // 用于服务端回声消除,重采样参考音频
    OpusResampler reference_resampler_;
    
    // 输出重采样器
    // 用于将解码后的音频重采样到扬声器支持的采样率
    OpusResampler output_resampler_;
    
    // 调试统计信息
    // 记录音频系统的运行统计
    DebugStatistics debug_statistics_;
    
    // 立即停止音频标志
    // 当置为true时,AudioOutputTask会立即停止输出
    std::atomic<bool> stop_audio_immediately_{false};
    std::atomic<uint32_t> playback_sequence_{0};

    // ==================== 事件和任务管理 ====================
    
    // 事件组句柄
    // 用于同步不同的音频功能状态
    EventGroupHandle_t event_group_;

    // ==================== 音频任务句柄 ====================
    
    // 音频输入任务句柄
    // 负责从麦克风采集音频数据
    TaskHandle_t audio_input_task_handle_ = nullptr;
    
    // 音频输出任务句柄
    // 负责向扬声器播放音频数据
    TaskHandle_t audio_output_task_handle_ = nullptr;
    
    // Opus编解码任务句柄
    // 负责音频编码和解码
    TaskHandle_t opus_codec_task_handle_ = nullptr;
    
    // ==================== 音频队列和同步 ====================
    
    // 音频队列互斥锁
    // 保护音频队列的线程安全访问
    std::mutex audio_queue_mutex_;
    
    // 音频队列条件变量
    // 用于在队列状态变化时通知等待的线程
    std::condition_variable audio_queue_cv_;
    
    // 音频解码队列
    // 存储从服务器接收到的待解码音频数据包
    std::deque<std::unique_ptr<AudioStreamPacket>> audio_decode_queue_;
    
    // 音频发送队列
    // 存储待发送到服务器的已编码音频数据包
    std::deque<std::unique_ptr<AudioStreamPacket>> audio_send_queue_;
    
    // 音频测试队列
    // 存储音频测试模式的音频数据包
    std::deque<std::unique_ptr<AudioStreamPacket>> audio_testing_queue_;
    
    // 音频编码队列
    // 存储待编码的PCM音频任务
    std::deque<std::unique_ptr<AudioTask>> audio_encode_queue_;
    
    // 音频播放队列
    // 存储待播放的PCM音频任务
    std::deque<std::unique_ptr<AudioTask>> audio_playback_queue_;

    // ==================== 服务端回声消除相关 ====================
    
    // 时间戳队列
    // 用于服务端回声消除的时间戳同步
    std::deque<uint32_t> timestamp_queue_;
    
    // 时间戳队列互斥锁
    // 保护时间戳队列的线程安全访问
    std::mutex timestamp_mutex_;

    // ==================== 状态标志 ====================
    
    // 唤醒词检测器是否已初始化
    bool wake_word_initialized_ = false;
    
    // 音频处理器是否已初始化
    bool audio_processor_initialized_ = false;
    
    // 是否检测到语音活动
    bool voice_detected_ = false;
    
    // 音频服务是否已停止
    bool service_stopped_ = true;
    
    // 音频输入是否需要预热
    // 麦克风刚启动时可能需要预热一段时间才能正常工作
    bool audio_input_need_warmup_ = false;

    // ==================== 音频状态监控 ====================
    
    // 音频状态检查定时器句柄
    // 用于定期检查音频系统的状态
    esp_timer_handle_t audio_power_timer_ = nullptr;
    
    // 最后输入音频的时间点
    // 用于检测音频输入是否超时
    std::chrono::steady_clock::time_point last_input_time_;
    
    // 最后输出音频的时间点
    // 用于检测音频输出是否超时
    std::chrono::steady_clock::time_point last_output_time_;

    // ==================== 私有方法 ====================
    
    // 音频输入任务函数
    // 负责从麦克风采集音频数据
    void AudioInputTask();
    
    // 音频输出任务函数
    // 负责向扬声器播放音频数据
    void AudioOutputTask();
    
    // Opus编解码任务函数
    // 负责音频编码和解码
    void OpusCodecTask();
    
    // 将音频任务推送到编码队列
    // @param type 任务类型
    // @param pcm PCM音频数据
    void PushTaskToEncodeQueue(AudioTaskType type, std::vector<int16_t>&& pcm);
    
    // 设置解码器的采样率和帧时长
    // @param sample_rate 采样率
    // @param frame_duration 帧时长(毫秒)
    void SetDecodeSampleRate(int sample_rate, int frame_duration);
    
    // 检查并更新音频状态
    // 用于监控音频系统的健康状态
    void CheckAndUpdateAudioPowerState();
};

#endif // AUDIO_SERVICE_H
