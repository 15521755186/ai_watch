// ==================== 网络通信协议头文件 ====================
// 此文件定义了网络通信协议的抽象基类,用于与服务器进行通信
// 
// 协议架构:
// - Protocol是抽象基类,定义了统一的通信接口
// - MqttProtocol和WebsocketProtocol是具体实现
// - 支持音频流传输和JSON消息传输
// - 支持双向通信(上传和下载)

#ifndef PROTOCOL_H
#define PROTOCOL_H

// JSON解析库
#include <cJSON.h>

// C++标准库
#include <string>       // 字符串处理
#include <functional>   // 函数对象
#include <chrono>       // 时间处理
#include <vector>       // 向量容器

// ==================== 音频流数据包结构 ====================
// 用于封装音频数据,包含采样率、帧时长、时间戳等信息
struct AudioStreamPacket {
    int sample_rate = 0;           // 采样率(如16000、24000等)
    int frame_duration = 0;        // 帧时长(毫秒,如60)
    uint32_t timestamp = 0;         // 时间戳(毫秒,用于同步)
    std::vector<uint8_t> payload;  // 音频数据负载(Opus编码后的数据)
};

// ==================== 二进制协议版本2 ====================
// 用于传输音频和JSON数据的二进制协议格式
struct BinaryProtocol2 {
    uint16_t version;       // 协议版本号
    uint16_t type;         // 消息类型: 0=OPUS音频, 1=JSON消息
    uint32_t reserved;     // 保留字段,用于未来扩展
    uint32_t timestamp;    // 时间戳(毫秒),用于服务端回声消除
    uint32_t payload_size; // 负载数据大小(字节)
    uint8_t payload[];     // 负载数据(可变长度)
} __attribute__((packed)); // 紧凑打包,不进行字节对齐

// ==================== 二进制协议版本3 ====================
// 简化版的二进制协议格式
struct BinaryProtocol3 {
    uint8_t type;          // 消息类型
    uint8_t reserved;      // 保留字段
    uint16_t payload_size; // 负载数据大小(字节)
    uint8_t payload[];     // 负载数据(可变长度)
} __attribute__((packed)); // 紧凑打包,不进行字节对齐

// ==================== 中止说话原因枚举 ====================
// 定义中止语音播放的原因
enum AbortReason {
    kAbortReasonNone,              // 无特殊原因
    kAbortReasonWakeWordDetected   // 检测到唤醒词,需要中止当前播放
};

// ==================== 监听模式枚举 ====================
// 定义语音监听的不同模式
enum ListeningMode {
    kListeningModeAutoStop,   // 自动停止模式:检测到说话停止后自动停止监听
    kListeningModeManualStop, // 手动停止模式:需要手动调用StopListening才能停止
    kListeningModeRealtime    // 实时模式:持续监听,需要回声消除(AEC)支持
};

// ==================== Protocol类定义 ====================
// Protocol是网络通信协议的抽象基类,定义了与服务器通信的统一接口
class Protocol {
public:
    // 析构函数
    virtual ~Protocol() = default;

    // ==================== 服务器参数查询 ====================
    
    // 获取服务器采样率
    // @return 服务器使用的音频采样率
    inline int server_sample_rate() const {
        return server_sample_rate_;
    }
    
    // 获取服务器帧时长
    // @return 服务器使用的音频帧时长(毫秒)
    inline int server_frame_duration() const {
        return server_frame_duration_;
    }
    
    // 获取会话ID
    // @return 当前会话的唯一标识符
    inline const std::string& session_id() const {
        return session_id_;
    }

    // ==================== 回调函数注册 ====================
    
    // 注册接收音频数据的回调函数
    // 当从服务器接收到音频数据时调用
    // @param callback 回调函数,参数为音频数据包
    void OnIncomingAudio(std::function<void(std::unique_ptr<AudioStreamPacket> packet)> callback);
    
    // 注册接收JSON消息的回调函数
    // 当从服务器接收到JSON消息时调用
    // @param callback 回调函数,参数为JSON对象
    void OnIncomingJson(std::function<void(const cJSON* root)> callback);
    
    // 注册音频通道打开的回调函数
    // 当音频通道成功打开时调用
    // @param callback 回调函数
    void OnAudioChannelOpened(std::function<void()> callback);
    
    // 注册音频通道关闭的回调函数
    // 当音频通道关闭时调用
    // @param callback 回调函数
    void OnAudioChannelClosed(std::function<void()> callback);
    
    // 注册网络错误的回调函数
    // 当网络连接出现错误时调用
    // @param callback 回调函数,参数为错误消息
    void OnNetworkError(std::function<void(const std::string& message)> callback);

    // ==================== 纯虚函数(子类必须实现) ====================
    
    // 启动协议
    // 初始化并启动网络连接
    // @return true表示成功,false表示失败
    virtual bool Start() = 0;
    
    // 打开音频通道
    // 建立与服务器的音频数据传输通道
    // @return true表示成功,false表示失败
    virtual bool OpenAudioChannel() = 0;
    
    // 关闭音频通道
    // 关闭与服务器的音频数据传输通道
    virtual void CloseAudioChannel() = 0;
    
    // 检查音频通道是否已打开
    // @return true表示已打开,false表示未打开
    virtual bool IsAudioChannelOpened() const = 0;
    
    // 发送音频数据
    // 将音频数据包发送到服务器
    // @param packet 音频数据包
    // @return true表示成功,false表示失败
    virtual bool SendAudio(std::unique_ptr<AudioStreamPacket> packet) = 0;
    
    // ==================== 消息发送方法(子类可重写) ====================
    
    // 发送唤醒词检测消息
    // 通知服务器检测到唤醒词
    // @param wake_word 唤醒词字符串
    virtual void SendWakeWordDetected(const std::string& wake_word);
    
    // 发送开始监听消息
    // 通知服务器开始监听用户语音
    // @param mode 监听模式
    virtual void SendStartListening(ListeningMode mode);
    
    // 发送停止监听消息
    // 通知服务器停止监听用户语音
    virtual void SendStopListening();
    
    // 发送中止说话消息
    // 通知服务器中止当前的语音播放
    // @param reason 中止原因
    virtual void SendAbortSpeaking(AbortReason reason);
    
    // 发送MCP(设备控制)消息
    // 发送设备控制命令到服务器
    // @param message MCP消息内容
    virtual void SendMcpMessage(const std::string& message);

protected:
    // ==================== 回调函数成员变量 ====================
    
    // 接收JSON消息的回调函数
    std::function<void(const cJSON* root)> on_incoming_json_;
    
    // 接收音频数据的回调函数
    std::function<void(std::unique_ptr<AudioStreamPacket> packet)> on_incoming_audio_;
    
    // 音频通道打开的回调函数
    std::function<void()> on_audio_channel_opened_;
    
    // 音频通道关闭的回调函数
    std::function<void()> on_audio_channel_closed_;
    
    // 网络错误的回调函数
    std::function<void(const std::string& message)> on_network_error_;

    // ==================== 服务器参数 ====================
    
    // 服务器采样率,默认24000Hz
    int server_sample_rate_ = 24000;
    
    // 服务器帧时长,默认60毫秒
    int server_frame_duration_ = 60;
    
    // 是否发生错误
    bool error_occurred_ = false;
    
    // 会话ID
    std::string session_id_;
    
    // 最后接收数据的时间点
    // 用于检测连接超时
    std::chrono::time_point<std::chrono::steady_clock> last_incoming_time_;

    // ==================== 私有方法(子类可使用) ====================
    
    // 发送文本消息
    // @param text 文本内容
    // @return true表示成功,false表示失败
    virtual bool SendText(const std::string& text) = 0;
    
    // 设置错误状态
    // @param message 错误消息
    virtual void SetError(const std::string& message);
    
    // 检查是否超时
    // @return true表示超时,false表示未超时
    virtual bool IsTimeout() const;
};

#endif // PROTOCOL_H // PROTOCOL_H