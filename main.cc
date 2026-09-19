/**
 * @file main.cc
 * @brief 应用程序入口文件
 * 
 * 此文件是整个系统的入口点，负责初始化系统环境并启动应用程序。
 */

#include <esp_log.h>      // ESP32 日志库
#include <esp_err.h>      // ESP32 错误处理库
#include <nvs.h>          // 非易失性存储库
#include <nvs_flash.h>    // 非易失性存储闪存操作
#include <driver/gpio.h>  // GPIO 驱动库
#include <esp_event.h>    // 事件循环库

#include "app/application.h"  // 应用程序主类
#include "system/system_info.h"  // 系统信息库

#define TAG "main"  // 日志标签

/**
 * @brief 应用程序主函数
 * 
 * ESP32 应用程序的标准入口点，由系统自动调用。
 * 负责初始化系统环境并启动应用程序。
 */
extern "C" void app_main(void)
{
    // 记录启动时间
    SystemInfo::RecordBootTime();

    // 初始化默认事件循环
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 初始化 NVS 闪存，用于存储 WiFi 配置等信息
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // 如果 NVS 闪存损坏或版本不匹配，擦除并重新初始化
        ESP_LOGW(TAG, "正在擦除 NVS 闪存以修复损坏问题");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    // 检查初始化结果
    ESP_ERROR_CHECK(ret);

    // 启动应用程序
    // 使用单例模式获取应用实例并调用 Start 方法
    Application::GetInstance().Start();
}
