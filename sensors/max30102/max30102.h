#ifndef MAX30102_H
#define MAX30102_H
#include <mutex>

#include "driver/i2c.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"


 
/**
 * @brief 芯片寄存器定义
 */
#define MAX30102_REG_INTERRUPT_STATUS_1          0x00        /**< 中断状态寄存器 1 */
#define MAX30102_REG_INTERRUPT_STATUS_2          0x01        /**< 中断状态寄存器 2 */
#define MAX30102_REG_INTERRUPT_ENABLE_1          0x02        /**< 中断使能寄存器 1 */
#define MAX30102_REG_INTERRUPT_ENABLE_2          0x03        /**< 中断使能寄存器 2 */
#define MAX30102_REG_FIFO_WRITE_POINTER          0x04        /**< FIFO 写指针寄存器 */
#define MAX30102_REG_OVERFLOW_COUNTER            0x05        /**< 溢出计数器寄存器 */
#define MAX30102_REG_FIFO_READ_POINTER           0x06        /**< FIFO 读指针寄存器 */
#define MAX30102_REG_FIFO_DATA_REGISTER          0x07        /**< FIFO 数据寄存器 */
#define MAX30102_REG_FIFO_CONFIG                 0x08        /**< FIFO 配置寄存器 */
#define MAX30102_REG_MODE_CONFIG                 0x09        /**< 模式配置寄存器 */
#define MAX30102_REG_SPO2_CONFIG                 0x0A        /**< SPO2 配置寄存器 */
#define MAX30102_REG_LED1_PA                     0x0C        /**< LED1 脉冲幅度寄存器 */
#define MAX30102_REG_LED2_PA                     0x0D        /**< LED2 脉冲幅度寄存器 */
#define MAX30102_REG_MULTI_LED_MODE_CONTROL_1    0x11        /**< 多LED模式控制寄存器 1 */
#define MAX30102_REG_MULTI_LED_MODE_CONTROL_2    0x12        /**< 多LED模式控制寄存器 2 */
#define MAX30102_REG_DIE_TEMP_INTEGER            0x1F        /**< 晶圆温度整数部分寄存器 */
#define MAX30102_REG_DIE_TEMP_FRACTION           0x20        /**< 晶圆温度小数部分寄存器 */
#define MAX30102_REG_DIE_TEMP_CONFIG             0x21        /**< 晶圆温度配置寄存器 */
#define MAX30102_REG_REVISION_ID                 0xFE        /**< 修订ID寄存器 */
#define MAX30102_REG_PART_ID                     0xFF        /**< 部件ID寄存器 */
 
/**
 * @brief MAX30102 设备结构体
 */
typedef struct
{
    i2c_master_bus_handle_t i2c_bus;
    i2c_master_dev_handle_t i2c_dev; // I2C 设备句柄
    uint16_t dev_addr;   ///< 设备地址
    float meastime;      ///< 测量时间
    float lastmeastime;  ///< 上一次测量时间
    float firxv[5];      ///< FIR滤波器输入值
    float firyv[5];      ///< FIR滤波器输出值
    float fredxv[5];     ///< 红光滤波器输入值
    float fredyv[5];     ///< 红光滤波器输出值
    float hrarray[10];   ///< 心率数组
    float spo2array[10]; ///< 血氧数组
    int hrarraycnt;      ///< 心率数组计数器
} max30102_dev_t;

typedef void *max30102_handle_t;
 
typedef struct {
    bool hand_detected;
    float heart_rate;
    float spo2;
    float heart_s;
    float spo2_s;
} max30102_data_t;

class MAX30102
{
private:
    MAX30102(gpio_num_t sda_gpio, gpio_num_t scl_gpio, gpio_num_t int_gpio, i2c_port_t i2c_port, uint8_t addr);
    ~MAX30102();
  /* data */
    gpio_num_t  sda_gpio_;
    gpio_num_t  scl_gpio_;
    gpio_num_t  int_gpio_;
    i2c_port_t  i2c_port_;
    uint8_t     addr_;
    max30102_dev_t *max30102_dev = nullptr;
    max30102_data_t *data = nullptr;
    mutable std::mutex mutex_;
    
    esp_err_t max30102_deinit(max30102_handle_t sensor);
    esp_err_t max30102_read(max30102_dev_t *dev, uint8_t reg_addr, uint8_t *data_buf, const uint8_t len);
    esp_err_t max30102_write(max30102_dev_t *dev, const uint8_t reg_addr, const uint8_t *const data_buf, const uint8_t len);

public:
  MAX30102& operator=(const MAX30102&) = delete; // 禁用赋值操作
  MAX30102(const MAX30102 &other) = delete;;
  static MAX30102& GetInstance(
        gpio_num_t sda_gpio = GPIO_NUM_NC,
        gpio_num_t scl_gpio = GPIO_NUM_NC,
        gpio_num_t int_gpio = GPIO_NUM_NC,
        i2c_port_t i2c_port = I2C_NUM_0,
        uint8_t addr = 0x57
    );
  esp_err_t max30102_config(void);
  esp_err_t max30102_deconfig(void);
  esp_err_t max30102_read_data(void);
  max30102_data_t Max30102_Get_Data(void);
  int Max30102_Get_Heart(void);
  int Max30102_Get_SpO2(void);
};





#endif // MAX30102_H
