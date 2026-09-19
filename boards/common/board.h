// ==================== 硬件抽象层头文件 ====================
// 此文件定义了硬件抽象层(Board类),用于屏蔽不同硬件平台的差异
// 
// 设计理念:
// - 使用抽象基类定义统一的硬件接口
// - 具体硬件平台继承基类并实现接口
// - 应用程序通过基类接口访问硬件,不关心具体硬件实现
// - 便于移植到不同的硬件平台

#ifndef BOARD_H
#define BOARD_H

// 网络相关头文件
#include <http.h>             // HTTP客户端
#include <web_socket.h>       // WebSocket客户端
#include <mqtt.h>             // MQTT客户端
#include <udp.h>              // UDP客户端
#include <string>             // 字符串处理
#include <network_interface.h> // 网络接口抽象

// 硬件组件头文件
#include "led/led.h"         // LED控制
#include "backlight.h"       // 背光控制
#include "camera.h"          // 摄像头控制
#include "weather/weather.h" // 天气功能
#include "power.h"           // 电源管理
#include "sensors/max30102/max30102.h"        // 心率血氧传感器(MAX30102)
#include "display.h"         // 显示屏控制

// 板级创建函数声明
// 每个硬件平台需要实现此函数来创建对应的Board实例
void* create_board();
// ==================== 前置声明 ====================
class AudioCodec;  // 音频编解码器
class Display;     // 显示屏

// ==================== Board类定义 ====================
// Board类是硬件抽象层的核心,定义了所有硬件组件的统一接口
// 采用单例模式,确保整个应用程序只有一个Board实例
class Board {
private:
    // 禁用拷贝构造函数
    // 防止创建Board的副本,确保单例模式
    Board(const Board&) = delete;
    
    // 禁用赋值操作
    // 防止将一个Board实例赋值给另一个
    Board& operator=(const Board&) = delete;

protected:
    // 构造函数
    // 声明为protected,防止直接创建实例
    Board();
    
    // 生成设备唯一标识符(UUID)
    // 使用硬件信息生成一个唯一的设备ID
    std::string GenerateUuid();

    // 设备唯一标识符(UUID)
    // 每个设备都有一个唯一的UUID,用于在服务器端识别设备
    std::string uuid_;

public:
    // ==================== 单例模式 ====================
    // 获取Board实例(单例)
    // 使用静态局部变量实现单例模式,线程安全
    // @return Board实例的引用
    static Board& GetInstance() {
        static Board* instance = static_cast<Board*>(create_board());
        return *instance;
    }

    // 析构函数
    virtual ~Board() = default;
    
    // ==================== 硬件信息查询 ====================
    
    // 获取板卡类型
    // @return 板卡类型字符串(例如:"esp32s3")
    virtual std::string GetBoardType() = 0;
    
    // 获取设备UUID
    // @return 设备唯一标识符
    virtual std::string GetUuid() { return uuid_; }
    
    // ==================== 硬件组件获取 ====================
    
    // 获取背光控制器
    // @return 背光控制器指针,如果设备不支持则返回nullptr
    virtual Backlight* GetBacklight() { return nullptr; }
    
    // 获取LED控制器
    // @return LED控制器指针
    virtual Led* GetLed();
    
    // 获取音频编解码器
    // @return 音频编解码器指针(必须实现)
    virtual AudioCodec* GetAudioCodec() = 0;
    
    // 获取ESP32芯片温度
    // @param esp32temp 输出参数,存储温度值
    // @return true表示成功,false表示失败
    virtual bool GetTemperature(float& esp32temp);
    
    // 获取显示屏
    // @return 显示屏指针
    virtual Display* GetDisplay();
    
    // 获取天气功能
    // @return 天气功能指针
    virtual Weather* GetWeather();
    
    // 获取心率血氧传感器
    // @return MAX30102传感器指针
    virtual MAX30102* GetMAX30102();
    
    // 获取摄像头
    // @return 摄像头指针
    virtual Camera* GetCamera();
    
    // 获取电池监控器
    // @return 电池监控器指针
    virtual BatteryMonitor * GetBatteryMonitor();
    
    // ==================== 网络相关 ====================
    
    // 获取网络接口
    // @return 网络接口指针(必须实现)
    virtual NetworkInterface* GetNetwork() = 0;
    
    // 启动网络连接
    // 初始化并启动WiFi或蜂窝网络连接
    virtual void StartNetwork() = 0;
    
    // 获取网络状态图标
    // @return 网络状态图标字符串(必须实现)
    virtual const char* GetNetworkStateIcon() = 0;
    
    // ==================== 电源管理 ====================
    
    // 获取电池电量
    // @param level 输出参数,电池电量百分比(0-100)
    // @param charging 输出参数,是否正在充电
    // @param discharging 输出参数,是否正在放电
    // @return true表示成功,false表示失败
    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging);
    
    // 设置省电模式
    // @param enabled true表示启用省电模式,false表示禁用
    virtual void SetPowerSaveMode(bool enabled) = 0;
    
    // 重置WiFi配置
    // 用于重新进入WiFi配置模式
    virtual void ResetWifiConfiguration() {}
    
    // ==================== JSON数据 ====================
    
    // 获取设备JSON信息
    // @return 设备信息的JSON字符串
    virtual std::string GetJson();
    
    // 获取板卡JSON信息
    // @return 板卡信息的JSON字符串(必须实现)
    virtual std::string GetBoardJson() = 0;
    
    // 获取设备状态JSON信息
    // @return 设备状态的JSON字符串(必须实现)
    virtual std::string GetDeviceStatusJson() = 0;
    
    // ==================== 手电筒控制 ====================
    
    // 打开手电筒
    // 如果设备支持手电筒功能,则打开手电筒
    virtual void TurnOnFlashlight() {}
    
    // 关闭手电筒
    // 如果设备支持手电筒功能,则关闭手电筒
    virtual void TurnOffFlashlight() {}
};

// ==================== 板卡注册宏 ====================
// 此宏用于简化板卡类的注册
// 每个具体的板卡类都需要使用此宏来注册
// 
// 使用示例:
// class MyBoard : public Board { ... };
// DECLARE_BOARD(MyBoard);
//
// 参数说明:
// - BOARD_CLASS_NAME: 板卡类名
#define DECLARE_BOARD(BOARD_CLASS_NAME) \
void* create_board() { \
    return new BOARD_CLASS_NAME(); \
}

#endif // BOARD_H