#include "sensor.h"

#include "envs.h"

// Defined in main.cpp
static const char *TAG = "TC-ENV";
extern ZigbeeSensor zbOccupancySensor;

onewire_bus_handle_t bus = NULL;
ds18b20_device_handle_t temp[2];
uint8_t sensorCount = 0;

float threshold = 0;
float lux = 0;

tsl2591_t light;
bool lightFound = false;

void handleTemperature() {
    ESP_ERROR_CHECK(ds18b20_trigger_temperature_conversion_for_all(bus));

    float averageTemp = 0, currentTemp = 0;
    for (uint8_t i = 0; i < sensorCount; i++) {
        ESP_ERROR_CHECK(ds18b20_get_temperature(temp[i], &currentTemp));
        averageTemp += (currentTemp / sensorCount);
    }
    zbOccupancySensor.setTemperature(averageTemp);
}

void setupTemp() {
    onewire_bus_config_t busConfig = {
        .bus_gpio_num = TEMP_PIN,
        .flags = {
            .en_pull_up = false
        }
    };
    onewire_bus_rmt_config_t rmtConfig = {
        .max_rx_bytes = 10
    };

    ESP_ERROR_CHECK(onewire_new_bus_rmt(&busConfig, &rmtConfig, &bus));

    onewire_device_iter_handle_t iter = NULL;
    ESP_ERROR_CHECK(onewire_new_device_iter(bus, &iter));

    onewire_device_t next;
    esp_err_t result = ESP_OK;
    do {
        result = onewire_device_iter_get_next(iter, &next);
        if (result == ESP_OK) {
            ds18b20_config_t dsCfg = {};
            onewire_device_address_t addr;

            if (ds18b20_new_device_from_enumeration(&next, &dsCfg, &temp[sensorCount]) == ESP_OK) {
                ds18b20_get_device_address(temp[sensorCount], &addr);
                ESP_LOGD(TAG, "Found sensor[%d], address: %016llX\n", sensorCount, addr);
                sensorCount++;
            } else {
                ESP_LOGD(TAG, "Found unknown device, address: %016llX\n", next.address);
            }
        }
    } while (result != ESP_ERR_NOT_FOUND);
    ESP_ERROR_CHECK(onewire_del_device_iter(iter));
    ESP_LOGD(TAG, "Found %d temperature sensors\n", sensorCount);
}

void setupLight() {
    ESP_ERROR_CHECK(i2cdev_init());

    ESP_ERROR_CHECK(tsl2591_init_desc(&light, I2C_NUM_0, SDA_PIN, SCL_PIN));
    esp_err_t res = tsl2591_init(&light);

    if (res == ESP_OK) {
        lightFound = true;

        ESP_ERROR_CHECK(tsl2591_set_power_status(&light, TSL2591_POWER_ON));
        ESP_ERROR_CHECK(tsl2591_set_als_status(&light, TSL2591_ALS_ON));
        ESP_ERROR_CHECK(tsl2591_set_gain(&light, TSL2591_GAIN_HIGH));
        ESP_ERROR_CHECK(tsl2591_set_integration_time(&light, TSL2591_INTEGRATION_300MS));
    }
}

void handleLight() {
    if (!lightFound) return;

    float luxLocal;

    // Get tsl2591 state
    esp_err_t res;
    uint16_t channel0, channel1;
    tsl2591_get_channel_data(&light, &channel0, &channel1);

    if (channel0 < channel1 || channel0 == 0) {
        luxLocal = 1;
        res = ESP_OK;
    } else {
        res = tsl2591_calculate_lux(&light, channel0, channel1, &luxLocal);
    }

    if (res == ESP_OK) {
        lux = (lux * (LUX_SAMPLES - 1) + luxLocal) / LUX_SAMPLES;
        ESP_LOGD(TAG, "Light value: %.2f, Smoothed: %.2f\n", luxLocal, lux);

        zbOccupancySensor.setIlluminance(lux * 10);
    }
}

void sensor_task(void *pvParameters) {
    setupTemp();
    setupLight();

    while (true) {
        handleTemperature();
        handleLight();
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
