#include "esp_zigbee_core.h"
#include "esp_zigbee_cluster.h"
#include "esp_zigbee_attribute.h"

#include "esp_log.h"

#include "config.h"
#include "zigbee/helpers.h"
#include "ld2412.h"

#include "sensor.h"

const char* swBuildId = SW_VERSION;
const char* dateCode = DATE_CODE;

void ZigbeeSensor::createBasicCluster(esp_zb_cluster_list_t* cluster_list) {
    esp_zb_attribute_list_t *basic_cluster = esp_zb_basic_cluster_create(&basic_cfg);
    esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    char zigbee_swid[16];
    fill_zcl_string(zigbee_swid, sizeof(zigbee_swid), swBuildId);
    esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_SW_BUILD_ID, (void*) &zigbee_swid);

    char zigbee_datecode[50];
    fill_zcl_string(zigbee_datecode, sizeof(zigbee_datecode), dateCode);
    esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_DATE_CODE_ID, (void*) &zigbee_datecode);

    uint16_t stack_version = 0x30;
    esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_STACK_VERSION_ID, (void*) &stack_version);

    char zb_name[50];
    fill_zcl_string(zb_name, sizeof(zb_name), manufacturer_name);
    esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, (void*) &zb_name);

    char zb_model[50];
    fill_zcl_string(zb_model, sizeof(zb_model), model_identifier);
    esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, (void*) &zb_model);
}


void ZigbeeSensor::createIdentifyCluster(esp_zb_cluster_list_t* cluster_list) {
    esp_zb_cluster_list_add_identify_cluster(cluster_list, esp_zb_identify_cluster_create(&identify_cfg), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
}

void ZigbeeSensor::createOccupancyCluster(esp_zb_cluster_list_t* cluster_list) {
    esp_zb_attribute_list_t *occupancy_cluster = esp_zb_occupancy_sensing_cluster_create(&occupancy_meas_cfg);
    esp_zb_cluster_list_add_occupancy_sensing_cluster(cluster_list, occupancy_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    uint16_t val = 0;
    esp_zb_occupancy_sensing_cluster_add_attr(occupancy_cluster, ESP_ZB_ZCL_ATTR_OCCUPANCY_SENSING_PIR_OCC_TO_UNOCC_DELAY_ID, (void*) &val);
    esp_zb_occupancy_sensing_cluster_add_attr(occupancy_cluster, ESP_ZB_ZCL_ATTR_OCCUPANCY_SENSING_PIR_UNOCC_TO_OCC_DELAY_ID, (void*) &val);
}

void ZigbeeSensor::createTimeCluster(esp_zb_cluster_list_t* cluster_list) {
    time_t utc_time = 0;
    int32_t gmt_offset = 0;

    esp_zb_attribute_list_t *time_cluster_server = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_TIME);
    esp_zb_time_cluster_add_attr(time_cluster_server, ESP_ZB_ZCL_ATTR_TIME_TIME_ZONE_ID, (void *) &gmt_offset);
    esp_zb_time_cluster_add_attr(time_cluster_server, ESP_ZB_ZCL_ATTR_TIME_TIME_ID, (void *) &utc_time);
    esp_zb_time_cluster_add_attr(time_cluster_server, ESP_ZB_ZCL_ATTR_TIME_TIME_STATUS_ID, (void *) &_time_status);

    esp_zb_attribute_list_t *time_cluster_client = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_TIME);
    esp_zb_cluster_list_add_time_cluster(cluster_list, time_cluster_server, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_time_cluster(cluster_list, time_cluster_client, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);
}

void ZigbeeSensor::createTemperatureCluster(esp_zb_cluster_list_t* cluster_list) {
    esp_zb_attribute_list_t *temp_cluster = esp_zb_temperature_meas_cluster_create(&temperature_cfg);
    esp_zb_cluster_list_add_temperature_meas_cluster(cluster_list, temp_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
}

void ZigbeeSensor::createIlluminanceCluster(esp_zb_cluster_list_t* cluster_list) {
    esp_zb_attribute_list_t *lux_cluster = esp_zb_illuminance_meas_cluster_create(&lux_cfg);
    esp_zb_cluster_list_add_illuminance_meas_cluster(cluster_list, lux_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
}

void ZigbeeSensor::createCustomClusters(esp_zb_cluster_list_t* cluster_list) {
    esp_zb_attribute_list_t *mmwave_cluster = esp_zb_zcl_attr_list_create(MS_MMW_CLUSTER_ID);

    uint16_t val = 0;
    esp_zb_cluster_add_manufacturer_attr(
        mmwave_cluster,
        MS_MMW_CLUSTER_ID,
        ATTR_BLUETOOTH_ID,
        MANUFACTURER_CODE,
        ESP_ZB_ZCL_ATTR_TYPE_BOOL,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE,
        &val
    );

    esp_zb_cluster_list_add_custom_cluster(cluster_list, mmwave_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
}

void ZigbeeSensor::createOtaCluster(esp_zb_cluster_list_t* cluster_list) {
    esp_zb_attribute_list_t *ota_cluster = esp_zb_ota_cluster_create(&ota_cluster_cfg);

    esp_zb_zcl_ota_upgrade_client_variable_t variable_config = {};
    variable_config.timer_query = ESP_ZB_ZCL_OTA_UPGRADE_QUERY_TIMER_COUNT_DEF;
    variable_config.hw_version = 3;
    variable_config.max_data_size = 223;

    uint16_t ota_upgrade_server_addr = 0xffff;
    uint8_t ota_upgrade_server_ep = 0xff;

    esp_zb_ota_cluster_add_attr(ota_cluster, ESP_ZB_ZCL_ATTR_OTA_UPGRADE_CLIENT_DATA_ID, (void *)&variable_config);
    esp_zb_ota_cluster_add_attr(ota_cluster, ESP_ZB_ZCL_ATTR_OTA_UPGRADE_SERVER_ADDR_ID, (void *)&ota_upgrade_server_addr);
    esp_zb_ota_cluster_add_attr(ota_cluster, ESP_ZB_ZCL_ATTR_OTA_UPGRADE_SERVER_ENDPOINT_ID, (void *)&ota_upgrade_server_ep);

    esp_zb_cluster_list_add_ota_cluster(cluster_list, ota_cluster, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);
}

static void findOTAServer(esp_zb_zdp_status_t zdo_status, uint16_t addr, uint8_t endpoint, void *user_ctx) {
    if (zdo_status == ESP_ZB_ZDP_STATUS_SUCCESS) {
        esp_zb_ota_upgrade_client_query_interval_set(*((uint8_t *)user_ctx), OTA_UPGRADE_QUERY_INTERVAL);
        esp_zb_ota_upgrade_client_query_image_req(addr, endpoint);
        ESP_LOGI("FIND_OTA", "Query OTA upgrade from server endpoint: %d after %d seconds", endpoint, OTA_UPGRADE_QUERY_INTERVAL);
    } else {
        ESP_LOGW("FIND_OTA", "No OTA Server found");
    }
}

void ZigbeeSensor::requestOTA() {
    esp_zb_zdo_match_desc_req_param_t req;
    uint16_t cluster_list[] = {ESP_ZB_ZCL_CLUSTER_ID_OTA_UPGRADE};

    req.addr_of_interest = 0x0000;
    req.dst_nwk_addr = 0x0000;
    req.num_in_clusters = 1;
    req.num_out_clusters = 0;
    req.profile_id = ESP_ZB_AF_HA_PROFILE_ID;
    req.cluster_list = cluster_list;
    esp_zb_lock_acquire(portMAX_DELAY);
    if (esp_zb_bdb_dev_joined()) {
        esp_zb_zdo_match_cluster(&req, findOTAServer, &_endpoint);
    }
    esp_zb_lock_release();
}

esp_zb_cluster_list_t* ZigbeeSensor::createClusters() {
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();

    createBasicCluster(cluster_list);
    createIdentifyCluster(cluster_list);
    createOccupancyCluster(cluster_list);
    createOtaCluster(cluster_list);
    createTimeCluster(cluster_list);
    createTemperatureCluster(cluster_list);
    createIlluminanceCluster(cluster_list);
    createCustomClusters(cluster_list);

    return cluster_list;
}

QueueHandle_t identifyQueue;
void lightTask(void *pvParameters) {
    identifyQueue = xQueueCreate(4, sizeof(uint8_t));
    uint8_t steps;
    bool res = false;

    while (true) {
        if ((res = xQueueReceive(identifyQueue, &steps, portMAX_DELAY))) {
            while (steps > 0) {
                gpio_set_level(LEDB_PIN, (--steps % 2));
                vTaskDelay(500 / portTICK_PERIOD_MS);
            }
        }
    }
}

void ZigbeeSensor::zbCommand(const zb_zcl_parsed_hdr_t* cmdInfo, const void* data) {
    if (cmdInfo->cluster_id == ESP_ZB_ZCL_CLUSTER_ID_IDENTIFY && cmdInfo->cmd_id == ESP_ZB_ZCL_CMD_IDENTIFY_IDENTIFY_ID) {
        uint16_t steps = *(uint16_t *)data * 2;

        gpio_set_level(LEDB_PIN, 0);
        xQueueSend(identifyQueue, &steps, 0);
    } else if (cmdInfo->cluster_id == ESP_ZB_ZCL_CLUSTER_ID_BASIC && cmdInfo->cmd_id == ESP_ZB_ZCL_CMD_BASIC_RESET_ID) {
        esp_zb_factory_reset();
    }
}

void setBluetooth(bool val) {
    mmwave.setConfig(true);
    mmwave.setBluetooth(val);
    mmwave.restart();

    mmwave.setConfig(true);
    FirmwareVersion ver = mmwave.getFirmwareVersion();
    ESP_LOGI("TC", "Got firmware version: V%x.%x.%x", ver.majorVersion >> 8, ver.majorVersion & 0xFF, ver.minorVersion);
    uint8_t mac[6];
    if (mmwave.getMac(mac)) {
        ESP_LOGI("TC", "Got mac address: %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    mmwave.setEngineeringMode(val);
    mmwave.setConfig(false);

    printf("Set bluetooth to %d\n", val);
}

void ZigbeeSensor::zbAttributeSet(const esp_zb_zcl_set_attr_value_message_t *message) {
    if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING) {
        uint16_t newTimeout = *(uint16_t *)message->attribute.data.value;

        if (newTimeout < 5 || newTimeout > 6 * 3600) {
            ESP_LOGW(TAG, "Invalid occupancy timeout rejected");
            return;
        }

        switch (message->attribute.id) {
            case ESP_ZB_ZCL_ATTR_OCCUPANCY_SENSING_PIR_OCC_TO_UNOCC_DELAY_ID:
                occupancyTimeoutSec = newTimeout;
                prefs.putUShort(NVS_OCC_TIMEOUT, newTimeout);
                break;
            default:
                ESP_LOGW(TAG, "Unknown occupancy cluster update: %d", message->attribute.id);
        }
    } else if (message->info.cluster == MS_MMW_CLUSTER_ID) {
        uint8_t val;

        switch (message->attribute.id) {
            case ATTR_BLUETOOTH_ID:
                val = *(uint8_t *)message->attribute.data.value;
                setBluetooth(val);

                break;
            default:
                ESP_LOGW(TAG, "Unknown mmw attr: %d", message->attribute.id);
        }
    }
}

uint16_t ZigbeeSensor::getTimeout() {
    return occupancyTimeoutSec;
}

void ZigbeeSensor::init() {
    prefs.begin(NVS_NAMESPACE, false);
    occupancyTimeoutSec = prefs.getUShort(NVS_OCC_TIMEOUT, 60);

    xTaskCreate(lightTask, "Identify", 2048, NULL, 2, NULL);
}

ZigbeeSensor::~ZigbeeSensor() {
    prefs.end();
}

void ZigbeeSensor::onConnect() {
    esp_zb_lock_acquire(portMAX_DELAY);
    uint32_t varArr[] = {
        occupancyTimeoutSec
    };
    uint16_t attrIdArr[] = {
        ESP_ZB_ZCL_ATTR_OCCUPANCY_SENSING_PIR_OCC_TO_UNOCC_DELAY_ID
    };
    uint16_t clusterIdArr[] = {
        ESP_ZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING
    };
    uint8_t items = sizeof(attrIdArr) / sizeof(*attrIdArr);

    for (uint8_t i = 0; i < items; i++) {
        if (clusterIdArr[i] >= 0xFC00) {
            esp_zb_zcl_set_manufacturer_attribute_val(
                _endpoint,
                clusterIdArr[i],
                ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                MANUFACTURER_CODE,
                attrIdArr[i],
                &varArr[i],
                false
            );
        } else {
            esp_zb_zcl_set_attribute_val(
                _endpoint,
                clusterIdArr[i],
                ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                attrIdArr[i],
                &varArr[i],
                false
            );
        }
    }
    esp_zb_lock_release();
}

ZigbeeSensor::ZigbeeSensor(uint8_t endpoint) : ZigbeeDevice(ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID, endpoint) {    
    basic_cfg = {
        .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = ESP_ZB_ZCL_BASIC_POWER_SOURCE_DC_SOURCE
    };
    identify_cfg = {
        .identify_time = ESP_ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE
    };
    occupancy_meas_cfg = {
        .occupancy = ESP_ZB_ZCL_OCCUPANCY_SENSING_OCCUPANCY_UNOCCUPIED,
        .sensor_type = ESP_ZB_ZCL_OCCUPANCY_SENSING_OCCUPANCY_SENSOR_TYPE_PIR,
        .sensor_type_bitmap = (1 << ESP_ZB_ZCL_OCCUPANCY_SENSING_OCCUPANCY_SENSOR_TYPE_PIR)
    };
    ota_cluster_cfg = {
        .ota_upgrade_file_version = FW_VERSION,
        .ota_upgrade_manufacturer = 0x1001,
        .ota_upgrade_image_type = 0x1013,
        .ota_min_block_reque = 0,
        .ota_upgrade_file_offset = 0,
        .ota_upgrade_downloaded_file_ver = ESP_ZB_ZCL_OTA_UPGRADE_DOWNLOADED_FILE_VERSION_DEF_VALUE,
        .ota_upgrade_server_id = 0,
        .ota_image_upgrade_status = 0
    };
    temperature_cfg = {
        .measured_value = (short) 0x8000,
        .min_value = -5000,
        .max_value = 10000
    };
    lux_cfg = {
        .measured_value = ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MEASURED_VALUE_INVALID,
        .min_value = 0x0001, // 1
        .max_value = 0xc126 // 88,000
    };

    _cluster_list = createClusters();
}

bool ZigbeeSensor::setOccupancy(bool occupied) {
    esp_zb_zcl_status_t ret = ESP_ZB_ZCL_STATUS_SUCCESS;

    esp_zb_lock_acquire(portMAX_DELAY);
    ret = esp_zb_zcl_set_attribute_val(
        _endpoint,
        ESP_ZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_OCCUPANCY_SENSING_OCCUPANCY_ID,
        &occupied,
        false
    );
    esp_zb_lock_release();

    if (ret != ESP_ZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGE(TAG, "Failed to set occupancy: 0x%x: %s", ret, esp_zb_zcl_status_to_name(ret));
        return false;
    }
    return true;
}

bool ZigbeeSensor::setTemperature(float temperature) {
    int16_t zigbeeTemp = temperature * 100;

    esp_zb_zcl_status_t ret = ESP_ZB_ZCL_STATUS_SUCCESS;

    esp_zb_lock_acquire(portMAX_DELAY);
    ret = esp_zb_zcl_set_attribute_val(
        _endpoint,
        ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID,
        &zigbeeTemp,
        false
    );
    esp_zb_lock_release();

    if (ret != ESP_ZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGE(TAG, "Failed to set temperature: 0x%x: %s", ret, esp_zb_zcl_status_to_name(ret));
        return false;
    }
    return true;
}

bool ZigbeeSensor::setIlluminance(float illuminance) {
    uint16_t zigbeeLux;
    if (illuminance < 1.0) {
        zigbeeLux = ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MEASURED_VALUE_TOO_LOW;
    } else if (illuminance > 3.576e6) {
        zigbeeLux = ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MEASURED_VALUE_INVALID;
    } else {
        zigbeeLux = (10000.0 * log10(illuminance) + 1.0);
    }

    esp_zb_zcl_status_t ret = ESP_ZB_ZCL_STATUS_SUCCESS;

    esp_zb_lock_acquire(portMAX_DELAY);
    ret = esp_zb_zcl_set_attribute_val(
        _endpoint,
        ESP_ZB_ZCL_CLUSTER_ID_ILLUMINANCE_MEASUREMENT,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MEASURED_VALUE_ID,
        &zigbeeLux,
        false
    );
    esp_zb_lock_release();

    if (ret != ESP_ZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGE(TAG, "Failed to set illuminance: 0x%x: %s", ret, esp_zb_zcl_status_to_name(ret));
        return false;
    }
    return true;
}

esp_err_t doReport(uint8_t _endpoint, esp_zb_zcl_cluster_id_t cluster, uint16_t attr) {
    // Must already have zb lock
    esp_zb_zcl_report_attr_cmd_t report_attr_cmd = {
        {
            .dst_addr_u = {},
            .dst_endpoint = 0,
            .src_endpoint = _endpoint
        },
        ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT,
        cluster,
        {0, ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI, 0},
        ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC,
        attr
    };

    return esp_zb_zcl_report_attr_cmd_req(&report_attr_cmd);
}

bool ZigbeeSensor::report() {
    esp_zb_lock_acquire(portMAX_DELAY);
    esp_err_t ret = doReport(_endpoint, ESP_ZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING, ESP_ZB_ZCL_ATTR_OCCUPANCY_SENSING_OCCUPANCY_ID);
    esp_zb_lock_release();

    return ret == ESP_OK;
}
