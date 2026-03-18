#pragma once

#include <list>
#include <set>
#include <vector>

#include "esp_zigbee_core.h"
#include "esp_zigbee_type.h"

#include "endpoint.h"
#include "handlers.h"

#define INSTALLCODE_POLICY_ENABLE       false
#define ESP_ZB_PRIMARY_CHANNEL_MASK     ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK

class ZigbeeCore {
    public:
        ZigbeeCore();
        ~ZigbeeCore() {}
        void start();
        void registerEndpoint(ZigbeeDevice* device);
        void setChannelMask(uint32_t mask);
        void searchBindings();
        void deviceUpdate(esp_zb_zdo_signal_device_update_params_t* params);
        esp_err_t handle(esp_zb_core_action_callback_id_t callback_id, const void *message);
        void handle(const zb_zcl_parsed_hdr_t* cmdInfo, const void* data);

        bool connected = false;
        bool started = false;

        std::list<ZigbeeDevice *>* getDevices() {
            return &ep_objects;
        }
    private:
        static const char *TAG;
        std::list<ZigbeeDevice *> ep_objects;

        esp_zb_ep_list_t* _zb_ep_list;
        ZigbeeHandlers* handlers;
        uint32_t _primary_channel_mask;

        static void bindingTableCb(const esp_zb_zdo_binding_table_info_t *table_info, void *user_ctx);

        static inline const char *formatIEEEAddress(const esp_zb_ieee_addr_t addr) {
            static char buf[24];
            snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", addr[7], addr[6], addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
            return buf;
        }

        static inline const char *formatShortAddress(uint16_t addr) {
            static char buf[7];
            snprintf(buf, sizeof(buf), "0x%04X", addr);
            return buf;
        }
};

extern ZigbeeCore zigbeeCore;
