#ifndef PAJ7620_H_
#define PAJ7620_H_

#include <driver/gpio.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdint>
#include <functional>
#include <mutex>

enum class Paj7620Gesture {
    kNone = 0,
    kRight,
    kLeft,
    kUp,
    kDown,
    kForward,
    kBackward,
    kClockwise,
    kCounterClockwise,
    kWave,
};

class Paj7620 {
public:
    Paj7620(const Paj7620&) = delete;
    Paj7620& operator=(const Paj7620&) = delete;

    static Paj7620& GetInstance(
        gpio_num_t sda_gpio = GPIO_NUM_NC,
        gpio_num_t scl_gpio = GPIO_NUM_NC,
        gpio_num_t int_gpio = GPIO_NUM_NC
    );

    esp_err_t Config();
    esp_err_t ReadGesture(Paj7620Gesture* gesture);
    void StartGesturePollingTask(uint32_t interval_ms = 100);
    void SetGestureCallback(std::function<void(Paj7620Gesture)> callback);
    Paj7620Gesture last_gesture() const;
    bool initialized() const { return initialized_; }

    static const char* GestureToString(Paj7620Gesture gesture);

private:
    Paj7620(gpio_num_t sda_gpio, gpio_num_t scl_gpio, gpio_num_t int_gpio);
    ~Paj7620() = default;

    enum class RegisterBank : uint8_t {
        kBank0 = 0x00,
        kBank1 = 0x01,
    };

    static constexpr uint8_t kDeviceAddress = 0x73;
    static constexpr uint8_t kRegisterBankSelect = 0xEF;
    static constexpr uint8_t kGestureFlag0 = 0x43;
    static constexpr uint8_t kGestureFlag1 = 0x44;
    static constexpr uint8_t kExpectedId0 = 0x20;
    static constexpr uint8_t kExpectedId1 = 0x76;

    static constexpr uint8_t kGestureRightFlag = 1U << 0;
    static constexpr uint8_t kGestureLeftFlag = 1U << 1;
    static constexpr uint8_t kGestureUpFlag = 1U << 2;
    static constexpr uint8_t kGestureDownFlag = 1U << 3;
    static constexpr uint8_t kGestureForwardFlag = 1U << 4;
    static constexpr uint8_t kGestureBackwardFlag = 1U << 5;
    static constexpr uint8_t kGestureClockwiseFlag = 1U << 6;
    static constexpr uint8_t kGestureCounterClockwiseFlag = 1U << 7;
    static constexpr uint8_t kGestureWaveFlag = 1U << 0;

    static constexpr uint32_t kGestureEntryTimeMs = 800;  // 手势确认等待时间（ms）
    static constexpr uint32_t kGesturePollDelayMs = 100;
    static constexpr uint32_t kLongCooldownMs = 800;
    static constexpr uint32_t kShortCooldownMs = 400;

    static void GestureTaskEntry(void* arg);
    void GestureTask();

    void SoftI2cInit();
    void SoftI2cStart();
    void SoftI2cStop();
    bool SoftI2cWriteByte(uint8_t data);
    uint8_t SoftI2cReadByte(bool nack);
    esp_err_t WriteRegister(uint8_t reg, uint8_t value);
    esp_err_t ReadRegisters(uint8_t start_reg, uint8_t* buf, size_t len);
    esp_err_t SelectBank(RegisterBank bank);
    Paj7620Gesture DecodeGesture(uint8_t gesture0, uint8_t gesture1) const;

    gpio_num_t sda_gpio_;
    gpio_num_t scl_gpio_;
    gpio_num_t int_gpio_;
    bool initialized_ = false;
    bool task_started_ = false;
    uint32_t poll_interval_ms_ = kGesturePollDelayMs;
    TaskHandle_t gesture_task_handle_ = nullptr;
    std::function<void(Paj7620Gesture)> gesture_callback_;
    mutable std::mutex mutex_;
    Paj7620Gesture last_gesture_ = Paj7620Gesture::kNone;
};

#endif
