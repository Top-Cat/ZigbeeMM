#include "esp_log.h"
#include "esp_timer.h"
#include "esp_check.h"
#include "zigbee/esp_delta_ota_ops.h"

#include "handlers.h"

ZigbeeHandlers::ZigbeeHandlers(std::list<ZigbeeDevice *>* list) {
    ep_objects = list;
}

esp_err_t ZigbeeHandlers::handle(esp_zb_core_action_callback_id_t callback_id, const void *message) {
    esp_err_t ret = ESP_OK;
    switch (callback_id) {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
        ret = attributeUpdate((esp_zb_zcl_set_attr_value_message_t*) message);
        break;
    case ESP_ZB_CORE_REPORT_ATTR_CB_ID:
        ret = reporting((esp_zb_zcl_report_attr_message_t*) message);
        break;
    case ESP_ZB_CORE_CMD_READ_ATTR_RESP_CB_ID:
        ret = attributeResponse((esp_zb_zcl_cmd_read_attr_resp_message_t*) message);
        break;
    case ESP_ZB_CORE_OTA_UPGRADE_VALUE_CB_ID:
        ret = upgradeStatus((esp_zb_zcl_ota_upgrade_value_message_t *) message);
        break;
    case ESP_ZB_CORE_OTA_UPGRADE_QUERY_IMAGE_RESP_CB_ID:
        ret = queryImageResponse((esp_zb_zcl_ota_upgrade_query_image_resp_message_t*) message);
        break;
    case ESP_ZB_CORE_CMD_CUSTOM_CLUSTER_REQ_CB_ID:
        ret = customCommand((esp_zb_zcl_custom_cluster_command_message_t*) message);
        break;
    case ESP_ZB_CORE_CMD_DEFAULT_RESP_CB_ID:
        // Gateway responding to our reports
        break;
    default:
        ESP_LOGW(TAG, "Receive Zigbee action(0x%x) callback", callback_id);
        break;
    }
    return ret;
}

esp_err_t ZigbeeHandlers::attributeResponse(const esp_zb_zcl_cmd_read_attr_resp_message_t* message) {
    if (!message) {
        ESP_LOGE(TAG, "Empty message");
        return ESP_FAIL;
    }
    if (message->info.status != ESP_ZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGE(TAG, "Received message: error status(%d)", message->info.status);
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGV(
        TAG, "Read attribute response: from address(0x%x) src endpoint(%d) to dst endpoint(%d) cluster(0x%x)", message->info.src_address.u.short_addr,
        message->info.src_endpoint, message->info.dst_endpoint, message->info.cluster
    );

    for (std::list<ZigbeeDevice *>::iterator it = ep_objects->begin(); it != ep_objects->end(); ++it) {
        if (message->info.dst_endpoint == (*it)->_endpoint) {
            esp_zb_zcl_read_attr_resp_variable_t *variable = message->variables;
            while (variable) {
                ESP_LOGV(
                    TAG, "Read attribute response: status(%d), cluster(0x%x), attribute(0x%x), type(0x%x), value(%d)", variable->status, message->info.cluster,
                    variable->attribute.id, variable->attribute.data.type, variable->attribute.data.value ? *(uint8_t *)variable->attribute.data.value : 0
                );
                if (variable->status == ESP_ZB_ZCL_STATUS_SUCCESS) {
                    if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_TIME) {
                        (*it)->zbReadTimeCluster(&variable->attribute);
                    } else {
                        (*it)->zbAttributeRead(
                            message->info.cluster, &variable->attribute, message->info.src_endpoint, message->info.src_address
                        );
                    }
                }
                variable = variable->next;
            }
        }
    }
    return ESP_OK;
}

esp_err_t ZigbeeHandlers::attributeUpdate(const esp_zb_zcl_set_attr_value_message_t* message) {
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(message, ESP_FAIL, TAG, "Empty message");
    ESP_RETURN_ON_FALSE(message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG, TAG, "Received message: error status(%d)", message->info.status);
    ESP_LOGI(TAG, "Received message: endpoint(%d), cluster(0x%x), attribute(0x%x), data size(%d)", message->info.dst_endpoint, message->info.cluster,
             message->attribute.id, message->attribute.data.size);

    for (std::list<ZigbeeDevice *>::iterator it = ep_objects->begin(); it != ep_objects->end(); ++it) {
        if (message->info.dst_endpoint == (*it)->_endpoint) {
            (*it)->zbAttributeSet(message);
        }
    }

    return ret;
}

void ZigbeeHandlers::handle(const zb_zcl_parsed_hdr_t* cmdInfo, const void* data) {
    if (cmdInfo->cmd_direction != 0) return;

    ESP_LOGV(TAG,
        "Receive Zigbee command endpoint=%d, cluster=%d, cmd=%d, dir=%d",
        cmdInfo->addr_data.common_data.dst_endpoint, cmdInfo->cluster_id, cmdInfo->cmd_id, cmdInfo->cmd_direction
    );

    for (std::list<ZigbeeDevice *>::iterator it = ep_objects->begin(); it != ep_objects->end(); ++it) {
        if (cmdInfo->addr_data.common_data.dst_endpoint == (*it)->_endpoint) {
            (*it)->zbCommand(cmdInfo, data);
        }
    }
}

esp_err_t ZigbeeHandlers::customCommand(const esp_zb_zcl_custom_cluster_command_message_t* message) {
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(message, ESP_FAIL, TAG, "Empty message");
    ESP_RETURN_ON_FALSE(message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG, TAG, "Received command: error status(%d)", message->info.status);
    ESP_LOGI(TAG, "Receive custom command: %d from address 0x%04hx", message->info.command.id, message->info.src_address.u.short_addr);

    for (std::list<ZigbeeDevice *>::iterator it = ep_objects->begin(); it != ep_objects->end(); ++it) {
        if (message->info.dst_endpoint == (*it)->_endpoint) {
            (*it)->zbCustomCommand(message);
        }
    }

    return ret;
}

esp_err_t ZigbeeHandlers::otaData(uint32_t total_size, const void *payload, uint16_t payload_size, void **outbuf, uint16_t *outlen) {
    static uint16_t tagid = 0;
    void *data_buf = NULL;
    uint16_t data_len;

    if (!s_tagid_received) {
        uint32_t length = 0;
        if (!payload || payload_size <= OTA_ELEMENT_HEADER_LEN) {
            ESP_LOGE(TAG, "Invalid element format");
            return ESP_ERR_INVALID_ARG;
        }

        const uint8_t *payload_ptr = (const uint8_t *)payload;
        tagid = *(const uint16_t *)payload_ptr;
        length = *(const uint32_t *)(payload_ptr + sizeof(tagid));
        if ((length + OTA_ELEMENT_HEADER_LEN) != total_size) {
            ESP_LOGE(TAG, "Invalid element length [%ld/%ld]", length, total_size);
            return ESP_ERR_INVALID_ARG;
        }

        s_tagid_received = true;

        data_buf = (void *)(payload_ptr + OTA_ELEMENT_HEADER_LEN);
        data_len = payload_size - OTA_ELEMENT_HEADER_LEN;
    } else {
        data_buf = (void *)payload;
        data_len = payload_size;
    }

    switch (tagid) {
        case UPGRADE_IMAGE:
            *outbuf = data_buf;
            *outlen = data_len;
            break;
        default:
            ESP_LOGE(TAG, "Unsupported element tag identifier %d", tagid);
            return ESP_ERR_INVALID_ARG;
            break;
    }

    return ESP_OK;
}

esp_err_t ZigbeeHandlers::upgradeStatus(const esp_zb_zcl_ota_upgrade_value_message_t *message) {
    static uint32_t total_size = 0;
    static uint32_t offset = 0;
    static int64_t start_time = 0;
    static bool ota_started = false;
    bool delta = message->ota_header.optional.minimum_hardware_version == 2;
    esp_err_t ret = ESP_OK;

    if (message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS) {
        switch (message->upgrade_status) {
        case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_START:
            ESP_LOGD(TAG, "Zigbee - OTA upgrade start");
            start_time = esp_timer_get_time();
            ota_started = false;
            break;
        case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_RECEIVE:
            if (!ota_started) {
                ota_started = true;

                s_ota_partition = esp_ota_get_next_update_partition(NULL);
                assert(s_ota_partition);
                if (delta) {
                    ret = esp_delta_ota_begin(s_ota_partition, 0, &s_ota_handle);
                } else {
                    ret = esp_ota_begin(s_ota_partition, 0, &s_ota_handle);
                }
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Zigbee - Failed to begin OTA partition, status: %s", esp_err_to_name(ret));
                    return ret;
                }
            }

            total_size = message->ota_header.image_size;
            offset += message->payload_size;
            ESP_LOGD(TAG, "Zigbee - OTA Client receives data: progress [%ld/%ld]", offset, total_size);
            if (message->payload_size && message->payload) {
                uint16_t payload_size = 0;
                void *payload = NULL;
                ret = otaData(total_size, message->payload, message->payload_size, &payload, &payload_size);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Zigbee - Failed to element OTA data, status: %s", esp_err_to_name(ret));
                    return ret;
                }
                if (delta) {
                    ret = esp_delta_ota_write(s_ota_handle, (uint8_t*)payload, payload_size);
                } else {
                    ret = esp_ota_write(s_ota_handle, (const void *)payload, payload_size);
                }
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Zigbee - Failed to write OTA data to partition, status: %s", esp_err_to_name(ret));
                    return ret;
                }
            }
            break;
        case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_APPLY:
            ESP_LOGI(TAG, "Zigbee - OTA upgrade apply");
            break;
        case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_CHECK:
            ret = offset == total_size ? ESP_OK : ESP_FAIL;
            offset = 0;
            total_size = 0;
            s_tagid_received = false;
            ESP_LOGI(TAG, "Zigbee - OTA upgrade check status: %s", esp_err_to_name(ret));
            break;
        case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_FINISH:
            ESP_LOGI(TAG, "Zigbee - OTA Finish");
            ESP_LOGI(
                TAG, "Zigbee - OTA Information: version: 0x%lx, manufacturer code: 0x%x, image type: 0x%x, total size: %ld bytes, cost time: %lld ms,",
                message->ota_header.file_version, message->ota_header.manufacturer_code, message->ota_header.image_type, message->ota_header.image_size,
                (esp_timer_get_time() - start_time) / 1000
            );
            if (delta) {
                ret = esp_delta_ota_end(s_ota_handle);
            } else {
                ret = esp_ota_end(s_ota_handle);
            }
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Zigbee - Failed to end OTA partition, status: %s", esp_err_to_name(ret));
                return ret;
            }
            ret = esp_ota_set_boot_partition(s_ota_partition);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Zigbee - Failed to set OTA boot partition, status: %s", esp_err_to_name(ret));
                return ret;
            }
            ESP_LOGW(TAG, "Zigbee - Prepare to restart system");
            esp_restart();
            break;
        default:
            ESP_LOGI(TAG, "Zigbee - OTA status: %d", message->upgrade_status);
            break;
        }
    }
    return ret;
}

esp_err_t ZigbeeHandlers::queryImageResponse(const esp_zb_zcl_ota_upgrade_query_image_resp_message_t *message) {
    if (message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGI(TAG, "Zigbee - Queried OTA image from address: 0x%04hx, endpoint: %d", message->server_addr.u.short_addr, message->server_endpoint);
        ESP_LOGI(TAG, "Zigbee - Image version: 0x%lx, manufacturer code: 0x%x, image size: %ld", message->file_version, message->manufacturer_code, message->image_size);
        if (message->image_size == 0) {
            ESP_LOGI(TAG, "Zigbee - Rejecting OTA image upgrade, image size is 0");
            return ESP_FAIL;
        }
        if (message->file_version == 0) {
            ESP_LOGI(TAG, "Zigbee - Rejecting OTA image upgrade, file version is 0");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Zigbee - Approving OTA image upgrade");
    } else {
        ESP_LOGI(TAG, "Zigbee - OTA image upgrade response status: 0x%x", message->info.status);
    }
    return ESP_OK;
}

esp_err_t ZigbeeHandlers::reporting(const esp_zb_zcl_report_attr_message_t *message) {
    if (!message) {
        ESP_LOGE(TAG, "Empty message");
        return ESP_FAIL;
    }
    if (message->status != ESP_ZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGE(TAG, "Received message: error status(%d)", message->status);
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGV(
        TAG, "Received report from address(0x%x) src endpoint(%d) to dst endpoint(%d) cluster(0x%x)", message->src_address.u.short_addr, message->src_endpoint,
        message->dst_endpoint, message->cluster
    );

    for (std::list<ZigbeeDevice *>::iterator it = ep_objects->begin(); it != ep_objects->end(); ++it) {
        if (message->dst_endpoint == (*it)->_endpoint) {
            (*it)->zbAttributeRead(message->cluster, &message->attribute, message->src_endpoint, message->src_address);
        }
    }
    return ESP_OK;
}
