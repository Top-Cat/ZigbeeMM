#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "main.h"
#include "driver/gpio.h"
#include "nvs_flash.h"

#include "config.h"
#include "sensor.h"
#include "ld2412.h"
#include "zigbee/handlers.h"
#include "zigbee/core.h"

////////////////////////

static const char *TAG = "TC-ZB";

volatile bool button_pressed = false;
bool occupancy_state = false;
bool last_occ = false;
uint8_t led_state = 0;

uint64_t lastHeartbeat = 0;
uint64_t lastMotionUs = 0;
uint64_t manualTimer = 0;

ZigbeeSensor zbOccupancySensor = ZigbeeSensor(OCCUPANCY_SENSOR_ENDPOINT_NUMBER);

////////////////////////

void IRAM_ATTR buttonISR(void* data) {
    button_pressed = true;
}

void setOccupied(bool newVal) {
    occupancy_state = newVal;
    gpio_set_level(LEDA_PIN, newVal);

    zbOccupancySensor.setOccupancy(occupancy_state);
    zbOccupancySensor.report();
}

void mmData(LD2412Data data) {
    /*if (data.hasEnginneringData) {
        ESP_LOGI(
            "TC",
            "[%d] Radar static energy: %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
            data.ed.lux,
            data.ed.staticEnergy[0],
            data.ed.staticEnergy[1],
            data.ed.staticEnergy[2],
            data.ed.staticEnergy[3],
            data.ed.staticEnergy[4],
            data.ed.staticEnergy[5],
            data.ed.staticEnergy[6],
            data.ed.staticEnergy[7],
            data.ed.staticEnergy[8],
            data.ed.staticEnergy[9],
            data.ed.staticEnergy[10],
            data.ed.staticEnergy[11],
            data.ed.staticEnergy[12],
            data.ed.staticEnergy[13]
        );
    }*/
    // ESP_LOGI("TC", "[%d] Basic radar data: s=%d, md=%d, me=%d, sd=%d, se=%d", data.hasEnginneringData, data.state, data.movingDistance, data.movingEnergy, data.stationaryDistance, data.stationaryEnergy);

    bool campaign = (data.state & L_DBCE_CAMPAIGN_TARGET) > 0;
    bool stationary = (data.state & L_DBCE_STATIONARY_TARGET) > 0;
    bool occupied = campaign || stationary;

    if (occupied != last_occ) {
        // State change
        last_occ = occupied;

        if (occupied) {
            setOccupied(true);
        } else {
            lastMotionUs = esp_timer_get_time();
        }
    }
}

static esp_err_t deferred_driver_init(void) {
    // Init mmwave
    mmwave.onData(mmData);
    mmwave.init(MM_TX, MM_RX);
    mmwave.setConfig(true);
    mmwave.setEngineeringMode(false);
    mmwave.setBluetooth(false);
    mmwave.restart();

    /*FirmwareVersion ver = mmwave.getFirmwareVersion();
    ESP_LOGI("TC", "Got firmware version: V%x.%x.%x", ver.majorVersion >> 8, ver.majorVersion & 0xFF, ver.minorVersion);
    uint8_t mac[6];
    if (mmwave.getMac(mac)) {
        ESP_LOGI("TC", "Got mac address: %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    uint8_t ms[14];
    if (mmwave.getMotionSensitivity(ms)) {
        ESP_LOGI("TC", "Got motion sensitivity: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x", ms[0], ms[1], ms[2], ms[3], ms[4], ms[5], ms[6], ms[7], ms[8], ms[9], ms[10], ms[11], ms[12], ms[13]);
    }
    if (mmwave.getStaticSensitivity(ms)) {
        ESP_LOGI("TC", "Got static sensitivity: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x", ms[0], ms[1], ms[2], ms[3], ms[4], ms[5], ms[6], ms[7], ms[8], ms[9], ms[10], ms[11], ms[12], ms[13]);
    }
    LD2412Config conf;
    if (mmwave.getBasic(&conf)) {
        ESP_LOGI("TC", "Got config: min=%d, max=%d, dur=%d, pol=%d", conf.minDistanceGate, conf.maxDistanceGate, conf.unoccupiedDuration, conf.outputPolarity);
    }*/
    // Finish mmwave init

    return ESP_OK;
}

static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask) {
    ESP_RETURN_ON_FALSE(esp_zb_bdb_start_top_level_commissioning(mode_mask) == ESP_OK, , TAG, "Failed to start Zigbee commissioning");
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct) {
    uint32_t *p_sg_p       = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;   
    esp_zb_app_signal_type_t sig_type = (esp_zb_app_signal_type_t)*p_sg_p;
    esp_zb_zdo_signal_leave_params_t *leave_params = NULL;
    esp_zb_zdo_signal_nwk_status_indication_params_s* nlme_params = NULL;

    // Router
    esp_zb_zdo_signal_device_update_params_t *dev_update_params = NULL;

    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initialize Zigbee stack");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            ESP_LOGW(TAG, "Deferred driver initialization %s", deferred_driver_init() ? "failed" : "successful");
            ESP_LOGI(TAG, "Device started up in %sfactory-reset mode", esp_zb_bdb_is_factory_new() ? "" : "non-");
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "Start network steering");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
                zigbeeCore.started = true;
            } else {
                ESP_LOGI(TAG, "Device rebooted");
                zigbeeCore.started = true;
                zigbeeCore.connected = true;
                zigbeeCore.setChannelMask(1 << esp_zb_get_current_channel());
                zigbeeCore.searchBindings();
            }
        } else {
            ESP_LOGW(TAG, "Failed to initialize Zigbee stack (status: %s)", esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_INITIALIZATION, 500);
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            esp_zb_ieee_addr_t extended_pan_id;
            esp_zb_get_extended_pan_id(extended_pan_id);
            ESP_LOGI(TAG, "Joined network successfully (Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: 0x%04hx, Channel:%d, Short Address: 0x%04hx)",
                     extended_pan_id[7], extended_pan_id[6], extended_pan_id[5], extended_pan_id[4],
                     extended_pan_id[3], extended_pan_id[2], extended_pan_id[1], extended_pan_id[0],
                     esp_zb_get_pan_id(), esp_zb_get_current_channel(), esp_zb_get_short_address());
            zigbeeCore.connected = true;
            zigbeeCore.setChannelMask(1 << esp_zb_get_current_channel());
        } else {
            ESP_LOGI(TAG, "Network steering was not successful (status: %s)", esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
    case ESP_ZB_ZDO_SIGNAL_DEVICE_UPDATE:
        dev_update_params = (esp_zb_zdo_signal_device_update_params_t *)esp_zb_app_signal_get_params(p_sg_p);
        ESP_LOGI(TAG, "New device commissioned or rejoined (short: 0x%04hx)", dev_update_params->short_addr);
        zigbeeCore.deviceUpdate(dev_update_params);
        break;
    case ESP_ZB_ZDO_SIGNAL_LEAVE:
        leave_params = (esp_zb_zdo_signal_leave_params_t *)esp_zb_app_signal_get_params(p_sg_p);
        ESP_LOGV(TAG, "Signal to leave the network, leave type: %d", leave_params->leave_type);
        if (leave_params->leave_type == ESP_ZB_NWK_LEAVE_TYPE_RESET) {
            ESP_LOGI(TAG, "Leave without rejoin, factory reset the device");
            esp_zb_factory_reset();
        } else {  // Leave with rejoin -> Rejoin the network, only reboot the device
            ESP_LOGI(TAG, "Leave with rejoin, only reboot the device");
            esp_restart();
        }
        break;
    case ESP_ZB_NLME_STATUS_INDICATION:
        nlme_params = (esp_zb_zdo_signal_nwk_status_indication_params_s *)esp_zb_app_signal_get_params(p_sg_p);
        ESP_LOGV(TAG, "NLME status indication: %02x 0x%04x %02x", nlme_params->status, nlme_params->network_addr, nlme_params->unknown_command_id);
        break;
    default:
        ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s", esp_zb_zdo_signal_to_string(sig_type), sig_type,
                 esp_err_to_name(err_status));
        break;
    }
}

void handleResetButton() {
    if (!button_pressed)
        return;

    button_pressed = false;

    uint64_t pressStart = esp_timer_get_time();
    while (gpio_get_level(BUTTON_PIN) == 0) {
        vTaskDelay(50 / portTICK_PERIOD_MS);
        if (esp_timer_get_time() - pressStart > 3000000) {
            ESP_LOGW(TAG, "Resetting Zigbee to factory and rebooting in 1s.");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            esp_zb_factory_reset();
        }
    }
}

void handleHeartbeat() {
    if (esp_timer_get_time() - lastHeartbeat <= HEARTBEAT_INTERVAL)
        return;

    lastHeartbeat = esp_timer_get_time();

    if (!zigbeeCore.connected) {
        ESP_LOGI(TAG, "Zigbee not connected, attempting reconnect...");
        zigbeeCore.start();
    }
}

void handleOccupancy() {
    uint16_t occupancyTimeoutSec = zbOccupancySensor.getTimeout();
    if (!last_occ && occupancy_state && esp_timer_get_time() - lastMotionUs >= occupancyTimeoutSec * 1000000ULL) {
        setOccupied(false);
    } else if (!last_occ && occupancy_state) {
        led_state++;
        gpio_set_level(LEDA_PIN, (led_state & 0x0F) > 7);
    }
}

static void main_task(void *pvParameters) {
    while (true) {
        handleResetButton();
        handleHeartbeat();
        handleOccupancy();

        // Can't sleep as we're a zigbee router
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

extern "C" void app_main(void) {
    gpio_config_t gpioConfig = {
        .pin_bit_mask = BIT64(BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE
    };
    gpio_config(&gpioConfig);

    gpioConfig.pin_bit_mask = BIT64(LEDA_PIN) | BIT64(LEDB_PIN) | BIT64(TEMP_PIN);
    gpioConfig.intr_type = GPIO_INTR_DISABLE;
    gpioConfig.pull_up_en = GPIO_PULLUP_DISABLE;
    gpioConfig.mode = GPIO_MODE_OUTPUT;
    gpio_config(&gpioConfig);

    gpio_set_level(TEMP_PIN, 1);
    gpio_set_level(LEDA_PIN, 0);
    gpio_set_level(LEDB_PIN, 0);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_PIN, buttonISR, NULL);

    ESP_ERROR_CHECK(nvs_flash_init());
    zbOccupancySensor.init();

    zigbeeCore.registerEndpoint(&zbOccupancySensor);
    zigbeeCore.start();

    while (!zigbeeCore.connected) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    zbOccupancySensor.onConnect();
    zbOccupancySensor.requestOTA();

    xTaskCreate(main_task, "Main", 8192, NULL, 4, NULL);
    xTaskCreate(sensor_task, "Sensor", 8192, NULL, 4, NULL);
}
