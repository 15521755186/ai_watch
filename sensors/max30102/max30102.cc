#include "max30102.h"

#include "esp_check.h"
#include "esp_log.h"

#include <cstring>

#define TAG "MAX30102"

MAX30102& MAX30102::GetInstance(
    gpio_num_t sda_gpio,
    gpio_num_t scl_gpio,
    gpio_num_t int_gpio,
    i2c_port_t i2c_port,
    uint8_t addr
) {
    static MAX30102 instance(sda_gpio, scl_gpio, int_gpio, i2c_port, addr);
    return instance;
}

MAX30102::MAX30102(gpio_num_t sda_gpio, gpio_num_t scl_gpio, gpio_num_t int_gpio,
                   i2c_port_t i2c_port, uint8_t addr)
    : sda_gpio_(sda_gpio),
      scl_gpio_(scl_gpio),
      int_gpio_(int_gpio),
      i2c_port_(i2c_port),
      addr_(addr) {
    i2c_master_bus_handle_t i2c_bus_handle = NULL;
    i2c_master_bus_config_t i2c_conf = {
        .i2c_port = i2c_port_,
        .sda_io_num = sda_gpio_,
        .scl_io_num = scl_gpio_,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_conf, &i2c_bus_handle));

    i2c_master_dev_handle_t max30102_i2c_dev;
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr_,
        .scl_speed_hz = 100 * 1000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_handle, &dev_config, &max30102_i2c_dev));

    ESP_LOGI(TAG, "I2C device initialized");

    max30102_dev = static_cast<max30102_dev_t*>(calloc(1, sizeof(max30102_dev_t)));
    if (max30102_dev == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate sensor state");
        return;
    }
    max30102_dev->dev_addr = addr_;
    max30102_dev->i2c_dev = max30102_i2c_dev;
    max30102_dev->i2c_bus = i2c_bus_handle;

    data = static_cast<max30102_data_t*>(calloc(1, sizeof(max30102_data_t)));
    if (data == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate sensor data");
        return;
    }
}

MAX30102::~MAX30102() {
    if (max30102_dev != nullptr) {
        free(max30102_dev);
    }
    if (data != nullptr) {
        free(data);
    }
}

esp_err_t MAX30102::max30102_read(max30102_dev_t *dev, uint8_t reg_addr, uint8_t *data_buf, const uint8_t len) {
    ESP_RETURN_ON_FALSE(dev != NULL && data_buf != NULL && len > 0,
                        ESP_ERR_INVALID_ARG, TAG, "Invalid arguments");

    esp_err_t ret = i2c_master_transmit(
        dev->i2c_dev,
        &reg_addr, 1,
        500 / portTICK_PERIOD_MS
    );
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write register address 0x%02x: 0x%x", reg_addr, ret);
        return ret;
    }

    ret = i2c_master_receive(
        dev->i2c_dev,
        data_buf, len,
        500 / portTICK_PERIOD_MS
    );
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read register 0x%02x: 0x%x", reg_addr, ret);
    }
    return ret;
}

esp_err_t MAX30102::max30102_write(max30102_dev_t *dev, const uint8_t reg_addr, const uint8_t *const data_buf, const uint8_t len) {
    ESP_RETURN_ON_FALSE(dev != NULL && data_buf != NULL && len > 0,
                        ESP_ERR_INVALID_ARG, TAG, "Invalid arguments");

    uint8_t tx_buf[1 + len];
    tx_buf[0] = reg_addr;
    memcpy(tx_buf + 1, data_buf, len);

    esp_err_t ret = i2c_master_transmit(
        dev->i2c_dev,
        tx_buf, 1 + len,
        500 / portTICK_PERIOD_MS
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write register 0x%02x: 0x%x", reg_addr, ret);
    }
    return ret;
}

esp_err_t MAX30102::max30102_config(void) {
    std::lock_guard<std::mutex> lock(mutex_);

    max30102_dev_t *dev = max30102_dev;
    ESP_RETURN_ON_FALSE(dev != nullptr, ESP_ERR_INVALID_STATE, TAG, "Sensor not initialized");

    esp_err_t ret;
    uint8_t reg_value;

    reg_value = (0x2 << 5) | (1 << 2);
    ret = max30102_write(dev, MAX30102_REG_FIFO_CONFIG, &reg_value, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    reg_value = 0x03;
    ret = max30102_write(dev, MAX30102_REG_MODE_CONFIG, &reg_value, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    reg_value = (0x3 << 5) + (0x3 << 2) + 0x3;
    ret = max30102_write(dev, MAX30102_REG_SPO2_CONFIG, &reg_value, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    reg_value = 0xd0;
    ret = max30102_write(dev, MAX30102_REG_LED1_PA, &reg_value, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    reg_value = 0xa0;
    ret = max30102_write(dev, MAX30102_REG_LED2_PA, &reg_value, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "MAX30102 configured");
    return ESP_OK;
}

esp_err_t MAX30102::max30102_deconfig(void) {
    std::lock_guard<std::mutex> lock(mutex_);

    max30102_dev_t *dev = max30102_dev;
    if (dev == nullptr) {
        ESP_LOGE(TAG, "Sensor not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret;
    uint8_t reg_value;

    reg_value = 0x00;
    ret = max30102_write(dev, MAX30102_REG_MODE_CONFIG, &reg_value, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enter standby mode");
        return ret;
    }

    reg_value = 0x00;
    ret = max30102_write(dev, MAX30102_REG_LED1_PA, &reg_value, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable IR LED");
        return ret;
    }

    reg_value = 0x00;
    ret = max30102_write(dev, MAX30102_REG_LED2_PA, &reg_value, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable RED LED");
        return ret;
    }

    reg_value = 0x00;
    ret = max30102_write(dev, MAX30102_REG_FIFO_CONFIG, &reg_value, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset FIFO config");
        return ret;
    }

    ESP_LOGI(TAG, "MAX30102 deconfigured");
    return ESP_OK;
}

esp_err_t MAX30102::max30102_deinit(max30102_handle_t sensor) {
    ESP_RETURN_ON_FALSE(sensor != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid sensor handle");
    max30102_dev_t *dev = static_cast<max30102_dev_t*>(sensor);
    free(dev);
    return ESP_OK;
}

esp_err_t MAX30102::max30102_read_data(void) {
    std::lock_guard<std::mutex> lock(mutex_);

    ESP_RETURN_ON_FALSE(max30102_dev != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid sensor handle");
    ESP_RETURN_ON_FALSE(data != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid output data");

    max30102_dev_t *dev = max30102_dev;
    static int average_count = 0;

    esp_err_t ret = ESP_OK;
    uint8_t rptr = 0;
    uint8_t wptr = 0;
    uint8_t reg_data[256] = {0};

    ret = max30102_read(dev, MAX30102_REG_FIFO_WRITE_POINTER, &wptr, 1);
    if (ret != ESP_OK) {
        data->hand_detected = false;
        data->heart_rate = 0;
        data->spo2 = 0;
        return ret;
    }

    ret = max30102_read(dev, MAX30102_REG_FIFO_READ_POINTER, &rptr, 1);
    if (ret != ESP_OK) {
        data->hand_detected = false;
        data->heart_rate = 0;
        data->spo2 = 0;
        return ret;
    }

    int samples = ((32 + wptr) - rptr) % 32;
    if (samples <= 0) {
        ESP_LOGD(TAG, "No fresh FIFO samples");
        data->hand_detected = false;
        data->heart_rate = 0;
        data->spo2 = 0;
        return ESP_FAIL;
    }

    ret = max30102_read(dev, MAX30102_REG_FIFO_DATA_REGISTER, reg_data, 6 * samples);
    if (ret != ESP_OK) {
        data->hand_detected = false;
        data->heart_rate = 0;
        data->spo2 = 0;
        return ret;
    }

    for (int i = 0; i < samples; i++) {
        dev->meastime += 0.01f;

        dev->firxv[0] = dev->firxv[1];
        dev->firxv[1] = dev->firxv[2];
        dev->firxv[2] = dev->firxv[3];
        dev->firxv[3] = dev->firxv[4];
        dev->firxv[4] = (1.0f / 3.48311f) *
            (256 * 256 * (reg_data[6 * i + 3] % 4) + 256 * reg_data[6 * i + 4] + reg_data[6 * i + 5]);

        dev->firyv[0] = dev->firyv[1];
        dev->firyv[1] = dev->firyv[2];
        dev->firyv[2] = dev->firyv[3];
        dev->firyv[3] = dev->firyv[4];
        dev->firyv[4] =
            (dev->firxv[0] + dev->firxv[4]) - 2 * dev->firxv[2] +
            (-0.1718123813f * dev->firyv[0]) + (0.3686645260f * dev->firyv[1]) +
            (-1.1718123813f * dev->firyv[2]) + (1.9738037992f * dev->firyv[3]);

        dev->fredxv[0] = dev->fredxv[1];
        dev->fredxv[1] = dev->fredxv[2];
        dev->fredxv[2] = dev->fredxv[3];
        dev->fredxv[3] = dev->fredxv[4];
        dev->fredxv[4] = (1.0f / 3.48311f) *
            (256 * 256 * (reg_data[6 * i + 0] % 4) + 256 * reg_data[6 * i + 1] + reg_data[6 * i + 2]);

        dev->fredyv[0] = dev->fredyv[1];
        dev->fredyv[1] = dev->fredyv[2];
        dev->fredyv[2] = dev->fredyv[3];
        dev->fredyv[3] = dev->fredyv[4];
        dev->fredyv[4] =
            (dev->fredxv[0] + dev->fredxv[4]) - 2 * dev->fredxv[2] +
            (-0.1718123813f * dev->fredyv[0]) + (0.3686645260f * dev->fredyv[1]) +
            (-1.1718123813f * dev->fredyv[2]) + (1.9738037992f * dev->fredyv[3]);

        if (-1.0f * dev->firyv[4] >= 100 &&
            -1.0f * dev->firyv[2] > -1.0f * dev->firyv[0] &&
            -1.0f * dev->firyv[2] > -1.0f * dev->firyv[4] &&
            dev->meastime - dev->lastmeastime > 0.5f) {
            dev->hrarray[dev->hrarraycnt % 5] = 60.0f / (dev->meastime - dev->lastmeastime);
            dev->spo2array[dev->hrarraycnt % 5] =
                110.0f - 25.0f * ((dev->fredyv[4] / dev->fredxv[4]) / (dev->firyv[4] / dev->firxv[4]));

            if (dev->spo2array[dev->hrarraycnt % 5] > 100.0f) {
                dev->spo2array[dev->hrarraycnt % 5] = 99.9f;
            }

            dev->lastmeastime = dev->meastime;
            dev->hrarraycnt++;

            data->heart_rate =
                (dev->hrarray[0] + dev->hrarray[1] + dev->hrarray[2] + dev->hrarray[3] + dev->hrarray[4]) / 5.0f;
            if (data->heart_rate < 40.0f || data->heart_rate > 150.0f) {
                data->heart_rate = 0;
            }

            data->spo2 =
                (dev->spo2array[0] + dev->spo2array[1] + dev->spo2array[2] + dev->spo2array[3] + dev->spo2array[4]) / 5.0f;
            if (data->spo2 < 50.0f || data->spo2 > 101.0f) {
                data->spo2 = 0;
            }

            data->hand_detected = true;
            if (average_count == 10) {
                data->heart_rate = data->heart_s / 10.0f;
                data->spo2 = data->spo2_s / 10.0f;
                average_count = 0;
                data->heart_s = 0;
                data->spo2_s = 0;
                return ESP_OK;
            }

            average_count++;
            data->heart_s += data->heart_rate;
            data->spo2_s += data->spo2;
            return ESP_FAIL;
        } else {
            data->hand_detected = false;
            data->heart_rate = 0;
            data->spo2 = 0;
        }
    }

    return ESP_FAIL;
}

max30102_data_t MAX30102::Max30102_Get_Data(void) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (data == nullptr) {
        return max30102_data_t{};
    }
    return *data;
}

int MAX30102::Max30102_Get_Heart(void) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (data == nullptr) {
        return 0;
    }
    return static_cast<int>(data->heart_rate);
}

int MAX30102::Max30102_Get_SpO2(void) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (data == nullptr) {
        return 0;
    }
    return static_cast<int>(data->spo2);
}
