#include "paj7620.h"

#include <esp_check.h>
#include <esp_log.h>
#include <rom/ets_sys.h>

namespace {

struct PajInitRegister {
    uint8_t reg;
    uint8_t value;
};

constexpr PajInitRegister kInitRegisters[] = {
    {0xEF, 0x00}, {0x32, 0x29}, {0x33, 0x01}, {0x34, 0x00}, {0x35, 0x01},
    {0x36, 0x00}, {0x37, 0x07}, {0x38, 0x17}, {0x39, 0x06}, {0x3A, 0x12},
    {0x3F, 0x00}, {0x40, 0x02}, {0x41, 0xFF}, {0x42, 0x01}, {0x46, 0x2D},
    {0x47, 0x0F}, {0x48, 0x3C}, {0x49, 0x00}, {0x4A, 0x1E}, {0x4B, 0x00},
    {0x4C, 0x20}, {0x4D, 0x00}, {0x4E, 0x1A}, {0x4F, 0x14}, {0x50, 0x00},
    {0x51, 0x10}, {0x52, 0x00}, {0x5C, 0x02}, {0x5D, 0x00}, {0x5E, 0x10},
    {0x5F, 0x3F}, {0x60, 0x27}, {0x61, 0x28}, {0x62, 0x00}, {0x63, 0x03},
    {0x64, 0xF7}, {0x65, 0x03}, {0x66, 0xD9}, {0x67, 0x03}, {0x68, 0x01},
    {0x69, 0xC8}, {0x6A, 0x40}, {0x6D, 0x04}, {0x6E, 0x00}, {0x6F, 0x00},
    {0x70, 0x80}, {0x71, 0x00}, {0x72, 0x00}, {0x73, 0x00}, {0x74, 0xF0},
    {0x75, 0x00}, {0x80, 0x42}, {0x81, 0x44}, {0x82, 0x04}, {0x83, 0x20},
    {0x84, 0x20}, {0x85, 0x00}, {0x86, 0x10}, {0x87, 0x00}, {0x88, 0x05},
    {0x89, 0x18}, {0x8A, 0x10}, {0x8B, 0x01}, {0x8C, 0x37}, {0x8D, 0x00},
    {0x8E, 0xF0}, {0x8F, 0x81}, {0x90, 0x06}, {0x91, 0x06}, {0x92, 0x1E},
    {0x93, 0x0D}, {0x94, 0x0A}, {0x95, 0x0A}, {0x96, 0x0C}, {0x97, 0x05},
    {0x98, 0x0A}, {0x99, 0x41}, {0x9A, 0x14}, {0x9B, 0x0A}, {0x9C, 0x3F},
    {0x9D, 0x33}, {0x9E, 0xAE}, {0x9F, 0xF9}, {0xA0, 0x48}, {0xA1, 0x13},
    {0xA2, 0x10}, {0xA3, 0x08}, {0xA4, 0x30}, {0xA5, 0x19}, {0xA6, 0x10},
    {0xA7, 0x08}, {0xA8, 0x24}, {0xA9, 0x04}, {0xAA, 0x1E}, {0xAB, 0x1E},
    {0xCC, 0x19}, {0xCD, 0x0B}, {0xCE, 0x13}, {0xCF, 0x64}, {0xD0, 0x21},
    {0xD1, 0x0F}, {0xD2, 0x88}, {0xE0, 0x01}, {0xE1, 0x04}, {0xE2, 0x41},
    {0xE3, 0xD6}, {0xE4, 0x00}, {0xE5, 0x0C}, {0xE6, 0x0A}, {0xE7, 0x00},
    {0xE8, 0x00}, {0xE9, 0x00}, {0xEE, 0x07}, {0xEF, 0x01}, {0x00, 0x1E},
    {0x01, 0x1E}, {0x02, 0x0F}, {0x03, 0x10}, {0x04, 0x02}, {0x05, 0x00},
    {0x06, 0xB0}, {0x07, 0x04}, {0x08, 0x0D}, {0x09, 0x0E}, {0x0A, 0x9C},
    {0x0B, 0x04}, {0x0C, 0x05}, {0x0D, 0x0F}, {0x0E, 0x02}, {0x0F, 0x12},
    {0x10, 0x02}, {0x11, 0x02}, {0x12, 0x00}, {0x13, 0x01}, {0x14, 0x05},
    {0x15, 0x07}, {0x16, 0x05}, {0x17, 0x07}, {0x18, 0x01}, {0x19, 0x04},
    {0x1A, 0x05}, {0x1B, 0x0C}, {0x1C, 0x2A}, {0x1D, 0x01}, {0x1E, 0x00},
    {0x21, 0x00}, {0x22, 0x00}, {0x23, 0x00}, {0x25, 0x01}, {0x26, 0x00},
    {0x27, 0x39}, {0x28, 0x7F}, {0x29, 0x08}, {0x30, 0x03}, {0x31, 0x00},
    {0x32, 0x1A}, {0x33, 0x1A}, {0x34, 0x07}, {0x35, 0x07}, {0x36, 0x01},
    {0x37, 0xFF}, {0x38, 0x36}, {0x39, 0x07}, {0x3A, 0x00}, {0x3E, 0xFF},
    {0x3F, 0x00}, {0x40, 0x77}, {0x41, 0x40}, {0x42, 0x00}, {0x43, 0x30},
    {0x44, 0xA0}, {0x45, 0x5C}, {0x46, 0x00}, {0x47, 0x00}, {0x48, 0x58},
    {0x4A, 0x1E}, {0x4B, 0x1E}, {0x4C, 0x00}, {0x4D, 0x00}, {0x4E, 0xA0},
    {0x4F, 0x80}, {0x50, 0x00}, {0x51, 0x00}, {0x52, 0x00}, {0x53, 0x00},
    {0x54, 0x00}, {0x57, 0x80}, {0x59, 0x10}, {0x5A, 0x08}, {0x5B, 0x94},
    {0x5C, 0xE8}, {0x5D, 0x08}, {0x5E, 0x3D}, {0x5F, 0x99}, {0x60, 0x45},
    {0x61, 0x40}, {0x63, 0x2D}, {0x64, 0x02}, {0x65, 0x96}, {0x66, 0x00},
    {0x67, 0x97}, {0x68, 0x01}, {0x69, 0xCD}, {0x6A, 0x01}, {0x6B, 0xB0},
    {0x6C, 0x04}, {0x6D, 0x2C}, {0x6E, 0x01}, {0x6F, 0x32}, {0x71, 0x00},
    {0x72, 0x01}, {0x73, 0x35}, {0x74, 0x00}, {0x75, 0x33}, {0x76, 0x31},
    {0x77, 0x01}, {0x7C, 0x84}, {0x7D, 0x03}, {0x7E, 0x01},
};

constexpr uint32_t kI2cDelayUs = 10;
constexpr uint32_t kInitReadyDelayMs = 20;
constexpr uint32_t kInitRetryDelayMs = 10;
constexpr int kInitRetryCount = 3;
constexpr int kBusRecoveryClockPulses = 9;
constexpr const char* kTag = "PAJ7620";

inline void SoftI2cDelay() {
    ets_delay_us(kI2cDelayUs);
}

}  // namespace

Paj7620& Paj7620::GetInstance(gpio_num_t sda_gpio, gpio_num_t scl_gpio, gpio_num_t int_gpio) {
    static Paj7620 instance(sda_gpio, scl_gpio, int_gpio);
    return instance;
}

Paj7620::Paj7620(gpio_num_t sda_gpio, gpio_num_t scl_gpio, gpio_num_t int_gpio)
    : sda_gpio_(sda_gpio), scl_gpio_(scl_gpio), int_gpio_(int_gpio) {
}

esp_err_t Paj7620::Config() {
    if (initialized_) {
        return ESP_OK;
    }

    if (sda_gpio_ == GPIO_NUM_NC || scl_gpio_ == GPIO_NUM_NC) {
        ESP_LOGE(kTag, "PAJ7620 pins are not configured");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t last_err = ESP_FAIL;
    for (int attempt = 1; attempt <= kInitRetryCount; ++attempt) {
        SoftI2cInit();
        vTaskDelay(pdMS_TO_TICKS(kInitReadyDelayMs));

        last_err = SelectBank(RegisterBank::kBank0);
        if (last_err != ESP_OK) {
            ESP_LOGW(kTag, "Init attempt %d/%d: failed to select bank 0", attempt, kInitRetryCount);
        } else {
            last_err = SelectBank(RegisterBank::kBank0);
            if (last_err != ESP_OK) {
                ESP_LOGW(kTag, "Init attempt %d/%d: failed to re-select bank 0", attempt, kInitRetryCount);
            } else {
                uint8_t id[2] = {0};
                last_err = ReadRegisters(0x00, id, sizeof(id));
                if (last_err != ESP_OK) {
                    ESP_LOGW(kTag, "Init attempt %d/%d: failed to read device id", attempt, kInitRetryCount);
                } else {
                    ESP_LOGI(kTag, "Init attempt %d/%d: device ID 0x%02X 0x%02X", attempt, kInitRetryCount, id[0], id[1]);
                    if (id[0] != kExpectedId0 || id[1] != kExpectedId1) {
                        last_err = ESP_ERR_NOT_FOUND;
                        ESP_LOGW(
                            kTag,
                            "Init attempt %d/%d: unexpected device ID, expected 0x%02X 0x%02X",
                            attempt,
                            kInitRetryCount,
                            kExpectedId0,
                            kExpectedId1
                        );
                    } else {
                        bool init_failed = false;
                        for (const auto& item : kInitRegisters) {
                            last_err = WriteRegister(item.reg, item.value);
                            if (last_err != ESP_OK) {
                                ESP_LOGW(
                                    kTag,
                                    "Init attempt %d/%d: init register 0x%02X failed",
                                    attempt,
                                    kInitRetryCount,
                                    item.reg
                                );
                                init_failed = true;
                                break;
                            }
                        }

                        if (!init_failed) {
                            last_err = SelectBank(RegisterBank::kBank0);
                            if (last_err != ESP_OK) {
                                ESP_LOGW(kTag, "Init attempt %d/%d: failed to return to bank 0", attempt, kInitRetryCount);
                            } else {
                                initialized_ = true;
                                ESP_LOGI(
                                    kTag,
                                    "PAJ7620 initialized on SDA=%d SCL=%d after %d attempt(s)",
                                    sda_gpio_,
                                    scl_gpio_,
                                    attempt
                                );
                                return ESP_OK;
                            }
                        }
                    }
                }
            }
        }

        SoftI2cStop();
        if (attempt < kInitRetryCount) {
            vTaskDelay(pdMS_TO_TICKS(kInitRetryDelayMs));
        }
    }

    return last_err;
}

esp_err_t Paj7620::ReadGesture(Paj7620Gesture* gesture) {
    if (gesture == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t gesture0 = 0;
    ESP_RETURN_ON_ERROR(ReadRegisters(kGestureFlag0, &gesture0, 1), kTag, "Failed to read gesture flag 0");

    Paj7620Gesture decoded = Paj7620Gesture::kNone;
    switch (gesture0) {
        case kGestureRightFlag:
        case kGestureLeftFlag:
        case kGestureUpFlag:
        case kGestureDownFlag: {
            const uint8_t primary = gesture0;
            vTaskDelay(pdMS_TO_TICKS(kGestureEntryTimeMs));

            uint8_t gesture_follow = 0;
            ESP_RETURN_ON_ERROR(ReadRegisters(kGestureFlag0, &gesture_follow, 1), kTag, "Failed to read gesture follow-up");
            if (gesture_follow == kGestureForwardFlag) {
                decoded = Paj7620Gesture::kForward;
            } else if (gesture_follow == kGestureBackwardFlag) {
                decoded = Paj7620Gesture::kBackward;
            } else {
                decoded = DecodeGesture(primary, 0);
            }
            break;
        }
        case kGestureForwardFlag:
        case kGestureBackwardFlag:
        case kGestureClockwiseFlag:
        case kGestureCounterClockwiseFlag:
            decoded = DecodeGesture(gesture0, 0);
            break;
        default: {
            uint8_t gesture1 = 0;
            ESP_RETURN_ON_ERROR(ReadRegisters(kGestureFlag1, &gesture1, 1), kTag, "Failed to read gesture flag 1");
            decoded = DecodeGesture(gesture0, gesture1);
            break;
        }
    }

    *gesture = decoded;
    return ESP_OK;
}

void Paj7620::StartGesturePollingTask(uint32_t interval_ms) {
    if (task_started_) {
        return;
    }
    if (!initialized_) {
        ESP_LOGW(kTag, "PAJ7620 is not initialized, polling task not started");
        return;
    }
    poll_interval_ms_ = interval_ms == 0 ? kGesturePollDelayMs : interval_ms;
    if (xTaskCreate(
        &Paj7620::GestureTaskEntry,
        "paj7620_poll",
        4096,
        this,
        4,
        &gesture_task_handle_) != pdPASS) {
        ESP_LOGE(kTag, "Failed to create PAJ7620 polling task");
        return;
    }
    task_started_ = true;
}

void Paj7620::SetGestureCallback(std::function<void(Paj7620Gesture)> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    gesture_callback_ = std::move(callback);
}

Paj7620Gesture Paj7620::last_gesture() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_gesture_;
}

const char* Paj7620::GestureToString(Paj7620Gesture gesture) {
    switch (gesture) {
        case Paj7620Gesture::kRight:
            return "Right";
        case Paj7620Gesture::kLeft:
            return "Left";
        case Paj7620Gesture::kUp:
            return "Up";
        case Paj7620Gesture::kDown:
            return "Down";
        case Paj7620Gesture::kForward:
            return "Forward";
        case Paj7620Gesture::kBackward:
            return "Backward";
        case Paj7620Gesture::kClockwise:
            return "Clockwise";
        case Paj7620Gesture::kCounterClockwise:
            return "Anti-clockwise";
        case Paj7620Gesture::kWave:
            return "Wave";
        case Paj7620Gesture::kNone:
        default:
            return "None";
    }
}

void Paj7620::GestureTaskEntry(void* arg) {
    auto* self = static_cast<Paj7620*>(arg);
    self->GestureTask();
    vTaskDelete(nullptr);
}

void Paj7620::GestureTask() {
    constexpr uint32_t kCooldownMs = 500;  // 手势触发后冷却时间

    while (true) {
        Paj7620Gesture gesture = Paj7620Gesture::kNone;
        const esp_err_t err = ReadGesture(&gesture);
        if (err != ESP_OK) {
            ESP_LOGW(kTag, "Gesture read failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        if (gesture == Paj7620Gesture::kNone) {
            vTaskDelay(pdMS_TO_TICKS(poll_interval_ms_));
            continue;
        }

        std::function<void(Paj7620Gesture)> callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            last_gesture_ = gesture;
            callback = gesture_callback_;
        }

        ESP_LOGI(kTag, "Gesture detected: %s", GestureToString(gesture));
        if (callback) {
            callback(gesture);
        }

        // 触发后冷却，防止同一手势被多次读取
        vTaskDelay(pdMS_TO_TICKS(kCooldownMs));
    }
}

void Paj7620::SoftI2cInit() {
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << sda_gpio_) | (1ULL << scl_gpio_);
    io_conf.mode = GPIO_MODE_OUTPUT_OD;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    gpio_set_level(sda_gpio_, 1);
    gpio_set_level(scl_gpio_, 1);
    SoftI2cDelay();

    // Recover the bus in case the sensor kept SDA low during a previous boot.
    gpio_set_direction(sda_gpio_, GPIO_MODE_INPUT);
    for (int i = 0; i < kBusRecoveryClockPulses; ++i) {
        gpio_set_level(scl_gpio_, 0);
        SoftI2cDelay();
        gpio_set_level(scl_gpio_, 1);
        SoftI2cDelay();
    }
    gpio_set_direction(sda_gpio_, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(sda_gpio_, 1);
    gpio_set_level(scl_gpio_, 1);
    SoftI2cDelay();
}

void Paj7620::SoftI2cStart() {
    gpio_set_direction(sda_gpio_, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(sda_gpio_, 1);
    gpio_set_level(scl_gpio_, 1);
    SoftI2cDelay();
    gpio_set_level(sda_gpio_, 0);
    SoftI2cDelay();
    gpio_set_level(scl_gpio_, 0);
}

void Paj7620::SoftI2cStop() {
    gpio_set_direction(sda_gpio_, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(sda_gpio_, 0);
    SoftI2cDelay();
    gpio_set_level(scl_gpio_, 1);
    SoftI2cDelay();
    gpio_set_level(sda_gpio_, 1);
    SoftI2cDelay();
}

bool Paj7620::SoftI2cWriteByte(uint8_t data) {
    gpio_set_direction(sda_gpio_, GPIO_MODE_OUTPUT_OD);
    for (int i = 0; i < 8; ++i) {
        gpio_set_level(sda_gpio_, (data & 0x80U) ? 1 : 0);
        SoftI2cDelay();
        gpio_set_level(scl_gpio_, 1);
        SoftI2cDelay();
        gpio_set_level(scl_gpio_, 0);
        SoftI2cDelay();
        data <<= 1;
    }

    gpio_set_direction(sda_gpio_, GPIO_MODE_INPUT);
    SoftI2cDelay();
    gpio_set_level(scl_gpio_, 1);
    SoftI2cDelay();
    const bool ack = gpio_get_level(sda_gpio_) == 0;
    gpio_set_level(scl_gpio_, 0);
    gpio_set_direction(sda_gpio_, GPIO_MODE_OUTPUT_OD);
    return ack;
}

uint8_t Paj7620::SoftI2cReadByte(bool nack) {
    uint8_t data = 0;
    gpio_set_direction(sda_gpio_, GPIO_MODE_INPUT);
    for (int i = 0; i < 8; ++i) {
        data <<= 1;
        gpio_set_level(scl_gpio_, 0);
        SoftI2cDelay();
        gpio_set_level(scl_gpio_, 1);
        if (gpio_get_level(sda_gpio_)) {
            data |= 1U;
        }
        SoftI2cDelay();
    }
    gpio_set_level(scl_gpio_, 0);

    gpio_set_direction(sda_gpio_, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(sda_gpio_, nack ? 1 : 0);
    SoftI2cDelay();
    gpio_set_level(scl_gpio_, 1);
    SoftI2cDelay();
    gpio_set_level(scl_gpio_, 0);
    gpio_set_level(sda_gpio_, 1);
    return data;
}

esp_err_t Paj7620::WriteRegister(uint8_t reg, uint8_t value) {
    SoftI2cStart();
    if (!SoftI2cWriteByte(static_cast<uint8_t>(kDeviceAddress << 1))) {
        ESP_LOGW(kTag, "I2C NACK while writing device address 0x%02X", static_cast<uint8_t>(kDeviceAddress << 1));
        SoftI2cStop();
        return ESP_FAIL;
    }
    if (!SoftI2cWriteByte(reg)) {
        ESP_LOGW(kTag, "I2C NACK while writing register 0x%02X", reg);
        SoftI2cStop();
        return ESP_FAIL;
    }
    if (!SoftI2cWriteByte(value)) {
        ESP_LOGW(kTag, "I2C NACK while writing value 0x%02X to register 0x%02X", value, reg);
        SoftI2cStop();
        return ESP_FAIL;
    }
    SoftI2cStop();
    return ESP_OK;
}

esp_err_t Paj7620::ReadRegisters(uint8_t start_reg, uint8_t* buf, size_t len) {
    if (buf == nullptr || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    SoftI2cStart();
    if (!SoftI2cWriteByte(static_cast<uint8_t>(kDeviceAddress << 1))) {
        ESP_LOGW(kTag, "I2C NACK while addressing device 0x%02X for register read", static_cast<uint8_t>(kDeviceAddress << 1));
        SoftI2cStop();
        return ESP_FAIL;
    }
    if (!SoftI2cWriteByte(start_reg)) {
        ESP_LOGW(kTag, "I2C NACK while selecting register 0x%02X for read", start_reg);
        SoftI2cStop();
        return ESP_FAIL;
    }

    SoftI2cStart();
    if (!SoftI2cWriteByte(static_cast<uint8_t>((kDeviceAddress << 1) | 1U))) {
        ESP_LOGW(kTag, "I2C NACK while switching to read address 0x%02X", static_cast<uint8_t>((kDeviceAddress << 1) | 1U));
        SoftI2cStop();
        return ESP_FAIL;
    }

    for (size_t i = 0; i < len; ++i) {
        buf[i] = SoftI2cReadByte(i == (len - 1));
    }
    SoftI2cStop();
    return ESP_OK;
}

esp_err_t Paj7620::SelectBank(RegisterBank bank) {
    return WriteRegister(kRegisterBankSelect, static_cast<uint8_t>(bank));
}

Paj7620Gesture Paj7620::DecodeGesture(uint8_t gesture0, uint8_t gesture1) const {
    switch (gesture0) {
        case kGestureRightFlag:
            return Paj7620Gesture::kRight;
        case kGestureLeftFlag:
            return Paj7620Gesture::kLeft;
        case kGestureUpFlag:
            return Paj7620Gesture::kUp;
        case kGestureDownFlag:
            return Paj7620Gesture::kDown;
        case kGestureForwardFlag:
            return Paj7620Gesture::kForward;
        case kGestureBackwardFlag:
            return Paj7620Gesture::kBackward;
        case kGestureClockwiseFlag:
            return Paj7620Gesture::kClockwise;
        case kGestureCounterClockwiseFlag:
            return Paj7620Gesture::kCounterClockwise;
        default:
            break;
    }

    if (gesture1 == kGestureWaveFlag) {
        return Paj7620Gesture::kWave;
    }
    return Paj7620Gesture::kNone;
}
