#include "nvs_flash.h"
#include "esp_log.h"

#include "core.h"

const char* ZigbeeCore::TAG = "TC-ZC";
ZigbeeCore zigbeeCore;

static void esp_zb_task(void *pvParameters) {
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
}

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message) {
    return zigbeeCore.handle(callback_id, message);
}

static bool zb_command_handler(uint8_t bufid) {
    uint8_t buf[zb_buf_len(bufid)];
    zb_zcl_parsed_hdr_t *parsed = ZB_BUF_GET_PARAM(bufid, zb_zcl_parsed_hdr_t);
    memcpy(buf, zb_buf_begin(bufid), sizeof(buf));

    zigbeeCore.handle(parsed, &buf);
    return false;
}

ZigbeeCore::ZigbeeCore() {
    handlers = new ZigbeeHandlers(&ep_objects);
    _primary_channel_mask = ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK;
    _zb_ep_list = esp_zb_ep_list_create();
}

esp_err_t ZigbeeCore::handle(esp_zb_core_action_callback_id_t callback_id, const void *message) {
    return handlers->handle(callback_id, message);
}

void ZigbeeCore::handle(const zb_zcl_parsed_hdr_t* cmdInfo, const void* data) {
    return handlers->handle(cmdInfo, data);
}

const char *APSDE_TAG = "APSDE";
bool zb_apsde_data_indication_handler(esp_zb_apsde_data_ind_t ind) {
    ESP_LOGD(APSDE_TAG, "APSDE INDICATION - Received APSDE-DATA indication, status: %d", ind.status);
    ESP_LOGD(APSDE_TAG,
        "APSDE INDICATION - dst_endpoint: %d, src_endpoint: %d, dst_addr_mode: %d, src_addr_mode: %d, cluster_id: 0x%04x, asdu_length: %d", ind.dst_endpoint,
        ind.src_endpoint, ind.dst_addr_mode, ind.src_addr_mode, ind.cluster_id, ind.asdu_length
    );
    ESP_LOGD(APSDE_TAG,
        "APSDE INDICATION - dst_short_addr: 0x%04x, src_short_addr: 0x%04x, profile_id: 0x%04x, security_status: %d, lqi: %d, rx_time: %d", ind.dst_short_addr,
        ind.src_short_addr, ind.profile_id, ind.security_status, ind.lqi, ind.rx_time
    );

    if (ind.dst_endpoint == 0x00 && ind.cluster_id == 0x8006) {
        // These are invalid and will cause assertion errors
        return true;
    }

    if (ind.status == 0x00) {
        // Catch bind/unbind requests to update the bound devices list
        if (ind.cluster_id == 0x21 || ind.cluster_id == 0x22) {
            zigbeeCore.searchBindings();
        }
    } else {
        ESP_LOGE(APSDE_TAG, "APSDE INDICATION - Invalid status of APSDE-DATA indication, error code: %d", ind.status);
    }
    return false;  //False to let the stack process the message as usual
}

void ZigbeeCore::start() {
    ESP_ERROR_CHECK(nvs_flash_init());

    esp_zb_platform_config_t config = {
        .radio_config = {
            .radio_mode = ZB_RADIO_MODE_NATIVE,
            .radio_uart_config = {}
        },
        .host_config = {
            .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,
            .host_uart_config = {}
        }
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));

    esp_zb_cfg_t zb_nwk_cfg = {
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ROUTER,
        .install_code_policy = INSTALLCODE_POLICY_ENABLE,
        .nwk_cfg = {
            .zczr_cfg = {
                .max_children = 10
            }
        }
    };
    esp_zb_init(&zb_nwk_cfg);

    ESP_ERROR_CHECK(esp_zb_device_register(_zb_ep_list));
    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_raw_command_handler_register(zb_command_handler);
    ESP_ERROR_CHECK(esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK));
    esp_zb_aps_data_indication_handler_register(zb_apsde_data_indication_handler);

    xTaskCreate(esp_zb_task, "Zigbee", 8192, NULL, 5, NULL);
}

void ZigbeeCore::registerEndpoint(ZigbeeDevice* device) {
    ep_objects.push_back(device);

    ESP_ERROR_CHECK(esp_zb_ep_list_add_ep(_zb_ep_list, device->_cluster_list, device->_ep_config));
}

void ZigbeeCore::setChannelMask(uint32_t mask) {
    _primary_channel_mask = mask;
    esp_zb_set_channel_mask(_primary_channel_mask);
    zb_nvram_write_dataset(ZB_NVRAM_COMMON_DATA);
    ESP_LOGV(TAG, "Channel mask set to 0x%08x", mask);
}

struct DeviceIdentifier {
    uint8_t endpoint;
    uint16_t short_addr;
    esp_zb_ieee_addr_t ieee_addr;
    bool is_ieee;

    bool operator<(const DeviceIdentifier &other) const {
        if (endpoint != other.endpoint) {
            return endpoint < other.endpoint;
        }
        if (is_ieee != other.is_ieee) {
            return is_ieee < other.is_ieee;
        }
        if (is_ieee) {
            return memcmp(ieee_addr, other.ieee_addr, sizeof(esp_zb_ieee_addr_t)) < 0;
        }
        return short_addr < other.short_addr;
    }
};

void ZigbeeCore::bindingTableCb(const esp_zb_zdo_binding_table_info_t *table_info, void *user_ctx) {
    esp_zb_zdo_mgmt_bind_param_t *req = (esp_zb_zdo_mgmt_bind_param_t *)user_ctx;
    esp_zb_zdp_status_t zdo_status = (esp_zb_zdp_status_t)table_info->status;
    ESP_LOGV(TAG, "Binding table callback for address 0x%04x with status %d", req->dst_addr, zdo_status);

    if (zdo_status == ESP_ZB_ZDP_STATUS_SUCCESS) {
        // Print binding table log simple
        ESP_LOGV(TAG, "Binding table info: total %d, index %d, count %d", table_info->total, table_info->index, table_info->count);

        if (table_info->total == 0) {
            ESP_LOGV(TAG, "No binding table entries found");
            // Clear all bound devices since there are no entries
            for (std::list<ZigbeeDevice *>::iterator it = zigbeeCore.getDevices()->begin(); it != zigbeeCore.getDevices()->end(); ++it) {
                ESP_LOGV(TAG, "Clearing bound devices for EP %d", (*it)->_endpoint);
                (*it)->clearBoundDevices();
            }
            free(req);
            return;
        }

        // Create a set to track found devices using both short and IEEE addresses
        static std::set<DeviceIdentifier> found_devices;
        static std::vector<esp_zb_zdo_binding_table_record_t> all_records;

        // If this is the first chunk (index 0), clear the previous data
        if (table_info->index == 0) {
            found_devices.clear();
            all_records.clear();
        }

        // Add current records to our collection
        esp_zb_zdo_binding_table_record_t *record = table_info->record;
        for (int i = 0; i < table_info->count; i++) {
            ESP_LOGV(
                TAG,
                "Processing record %d: src_endp %d, dst_endp %d, cluster_id 0x%04x, dst_addr_mode %d", i, record->src_endp, record->dst_endp, record->cluster_id,
                record->dst_addr_mode
            );
            all_records.push_back(*record);
            record = record->next;
        }

        // If this is not the last chunk, request the next one
        if (table_info->index + table_info->count < table_info->total) {
            ESP_LOGV(TAG, "Requesting next chunk of binding table (current index: %d, count: %d, total: %d)", table_info->index, table_info->count, table_info->total);
            req->start_index = table_info->index + table_info->count;
            esp_zb_zdo_binding_table_req(req, bindingTableCb, req);
        } else {
            // This is the last chunk, process all records
            ESP_LOGV(TAG, "Processing final chunk of binding table, total records: %d", all_records.size());
            for (const auto &record : all_records) {
                DeviceIdentifier dev_id;
                dev_id.endpoint = record.src_endp;
                dev_id.is_ieee = (record.dst_addr_mode == ESP_ZB_APS_ADDR_MODE_64_ENDP_PRESENT);

                if (dev_id.is_ieee) {
                    memcpy(dev_id.ieee_addr, record.dst_address.addr_long, sizeof(esp_zb_ieee_addr_t));
                    dev_id.short_addr = 0xFFFF;  // Invalid short address
                } else {
                    dev_id.short_addr = record.dst_address.addr_short;
                    memset(dev_id.ieee_addr, 0, sizeof(esp_zb_ieee_addr_t));
                }

                // Track this device as found
                found_devices.insert(dev_id);
            }

            // Now process each endpoint and update its bound devices
            for (std::list<ZigbeeDevice *>::iterator it = zigbeeCore.getDevices()->begin(); it != zigbeeCore.getDevices()->end(); ++it) {
                ESP_LOGV(TAG, "Processing endpoint %d", (*it)->_endpoint);
                std::list<zb_device_params_t *> bound_devices = (*it)->getBoundDevices();
                std::list<zb_device_params_t *> devices_to_remove;

                // First, identify devices that need to be removed
                for (std::list<zb_device_params_t *>::iterator dev_it = bound_devices.begin(); dev_it != bound_devices.end(); ++dev_it) {
                    DeviceIdentifier dev_id;
                    dev_id.endpoint = (*it)->_endpoint;

                    // Create both short and IEEE address identifiers for the device
                    bool found = false;

                    // Check if device exists with short address
                    if ((*dev_it)->short_addr != 0xFFFF) {
                        dev_id.is_ieee = false;
                        dev_id.short_addr = (*dev_it)->short_addr;
                        memset(dev_id.ieee_addr, 0, sizeof(esp_zb_ieee_addr_t));
                        if (found_devices.find(dev_id) != found_devices.end()) {
                            found = true;
                        }
                    }

                    // Check if device exists with IEEE address
                    if (!found) {
                        dev_id.is_ieee = true;
                        memcpy(dev_id.ieee_addr, (*dev_it)->ieee_addr, sizeof(esp_zb_ieee_addr_t));
                        dev_id.short_addr = 0xFFFF;
                        if (found_devices.find(dev_id) != found_devices.end()) {
                            found = true;
                        }
                    }

                    if (!found) {
                        devices_to_remove.push_back(*dev_it);
                    }
                }

                // Remove devices that are no longer in the binding table
                for (std::list<zb_device_params_t *>::iterator dev_it = devices_to_remove.begin(); dev_it != devices_to_remove.end(); ++dev_it) {
                    (*it)->removeBoundDevice(*dev_it);
                    free(*dev_it);
                }

                // Now add new devices from the binding table
                for (const auto &record : all_records) {
                    if (record.src_endp == (*it)->_endpoint) {
                        ESP_LOGV(TAG, "Processing binding record for EP %d", record.src_endp);
                        zb_device_params_t *device = (zb_device_params_t *)calloc(1, sizeof(zb_device_params_t));
                        if (!device) {
                            ESP_LOGE(TAG, "Failed to allocate memory for device params");
                            continue;
                        }
                        device->endpoint = record.dst_endp;

                        bool is_ieee = (record.dst_addr_mode == ESP_ZB_APS_ADDR_MODE_64_ENDP_PRESENT);
                        if (is_ieee) {
                            memcpy(device->ieee_addr, record.dst_address.addr_long, sizeof(esp_zb_ieee_addr_t));
                            device->short_addr = 0xFFFF;
                        } else {
                            device->short_addr = record.dst_address.addr_short;
                            memset(device->ieee_addr, 0, sizeof(esp_zb_ieee_addr_t));
                        }

                        // Check if device already exists
                        bool device_exists = false;
                        for (std::list<zb_device_params_t *>::iterator dev_it = bound_devices.begin(); dev_it != bound_devices.end(); ++dev_it) {
                            if (is_ieee) {
                                if (memcmp((*dev_it)->ieee_addr, device->ieee_addr, sizeof(esp_zb_ieee_addr_t)) == 0) {
                                    device_exists = true;
                                    break;
                                }
                            } else {
                                if ((*dev_it)->short_addr == device->short_addr) {
                                    device_exists = true;
                                    break;
                                }
                            }
                        }

                        if (!device_exists) {
                            (*it)->addBoundDevice(device);
                            ESP_LOGD(
                                TAG,
                                "Device bound to EP %d -> device endpoint: %d, %s: %s", record.src_endp, device->endpoint, is_ieee ? "ieee addr" : "short addr",
                                is_ieee ? formatIEEEAddress(device->ieee_addr) : formatShortAddress(device->short_addr)
                            );
                        } else {
                            ESP_LOGV(TAG, "Device already exists, freeing allocated memory");
                            free(device);  // Free the device if it already exists
                        }
                    }
                }
            }

            // Print bound devices
            ESP_LOGV(TAG, "Filling bounded devices finished");
            free(req);
        }
    } else {
        ESP_LOGE(TAG, "Binding table request failed with status: %d", zdo_status);
        free(req);
    }
}

void ZigbeeCore::searchBindings() {
    esp_zb_zdo_mgmt_bind_param_t *mb_req = (esp_zb_zdo_mgmt_bind_param_t *)malloc(sizeof(esp_zb_zdo_mgmt_bind_param_t));
    mb_req->dst_addr = esp_zb_get_short_address();
    mb_req->start_index = 0;
    ESP_LOGD(TAG, "Requesting binding table for address 0x%04x", mb_req->dst_addr);
    esp_zb_zdo_binding_table_req(mb_req, bindingTableCb, (void *)mb_req);
}

void ZigbeeCore::deviceUpdate(esp_zb_zdo_signal_device_update_params_t* params) {
    // NOT IMPLMENTED - Used for updating bindings
    ESP_LOGW(TAG, "deviceUpdate not implemented");
}
