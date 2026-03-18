#pragma once

#include <list>

#include "esp_ota_ops.h"
#include "endpoint.h"

#include "zcl/esp_zigbee_zcl_core.h"

#define OTA_ELEMENT_HEADER_LEN 6

typedef enum esp_ota_element_tag_id_e {
    UPGRADE_IMAGE = 0x0000,
} esp_ota_element_tag_id_t;

class ZigbeeHandlers {
    public:
        ZigbeeHandlers(std::list<ZigbeeDevice *>* list);
        ~ZigbeeHandlers();
        esp_err_t handle(esp_zb_core_action_callback_id_t callback_id, const void *message);
        void handle(const zb_zcl_parsed_hdr_t* cmdInfo, const void* data);
    private:
        const char *TAG = "TC-ZBH";
        const esp_partition_t *s_ota_partition = NULL;
        esp_ota_handle_t s_ota_handle = 0;
        bool s_tagid_received = false;

        std::list<ZigbeeDevice *>* ep_objects;

        esp_err_t otaData(uint32_t total_size, const void *payload, uint16_t payload_size, void **outbuf, uint16_t *outlen);
        esp_err_t upgradeStatus(const esp_zb_zcl_ota_upgrade_value_message_t *message);
        esp_err_t queryImageResponse(const esp_zb_zcl_ota_upgrade_query_image_resp_message_t *message);
        esp_err_t attributeUpdate(const esp_zb_zcl_set_attr_value_message_t *message);
        esp_err_t attributeResponse(const esp_zb_zcl_cmd_read_attr_resp_message_t *message);
        esp_err_t reporting(const esp_zb_zcl_report_attr_message_t *message);
        esp_err_t customCommand(const esp_zb_zcl_custom_cluster_command_message_t* message);
};
