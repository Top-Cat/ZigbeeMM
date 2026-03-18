#include "esp_zigbee_type.h"

#include "zigbee/endpoint.h"
#include "prefs.h"

#define MANUFACTURER_CODE        0x1234

#define MS_MMW_CLUSTER_ID        0xFC10
#define ATTR_BLUETOOTH_ID        0x0001

#define OTA_UPGRADE_QUERY_INTERVAL (1 * 60)
#define NVS_NAMESPACE         "config"
#define NVS_OCC_TIMEOUT       "occ_timeout"

class ZigbeeSensor : public ZigbeeDevice {
    public:
        ZigbeeSensor(uint8_t endpoint);
        ~ZigbeeSensor();

        void zbAttributeSet(const esp_zb_zcl_set_attr_value_message_t *message) override;
        void zbCommand(const zb_zcl_parsed_hdr_t* cmdInfo, const void* data) override;

        bool setOccupancy(bool occupied);
        bool setTemperature(float temperature);
        bool setIlluminance(float illuminance);
        void init();
        uint16_t getTimeout();
        uint16_t getManualHoldout();

        void onConnect();
        void requestOTA();
    private:
        const char* TAG = "TC-ZBS";
        const char* manufacturer_name = "TC";
        const char* model_identifier = "MMWave Sensor";

        uint16_t occupancyTimeoutSec = 60;

        Preferences prefs;

        esp_zb_basic_cluster_cfg_t basic_cfg;
        esp_zb_identify_cluster_cfg_t identify_cfg;
        esp_zb_occupancy_sensing_cluster_cfg_t occupancy_meas_cfg;
        esp_zb_ota_cluster_cfg_t ota_cluster_cfg;
        esp_zb_temperature_meas_cluster_cfg_t temperature_cfg;
        esp_zb_illuminance_meas_cluster_cfg_t lux_cfg;

        esp_zb_cluster_list_t* createClusters() override;
        void createBasicCluster(esp_zb_cluster_list_t* cluster_list);
        void createIdentifyCluster(esp_zb_cluster_list_t* cluster_list);
        void createOccupancyCluster(esp_zb_cluster_list_t* cluster_list);
        void createOtaCluster(esp_zb_cluster_list_t* cluster_list);
        void createTimeCluster(esp_zb_cluster_list_t* cluster_list);
        void createTemperatureCluster(esp_zb_cluster_list_t* cluster_list);
        void createIlluminanceCluster(esp_zb_cluster_list_t* cluster_list);
        void createCustomClusters(esp_zb_cluster_list_t* cluster_list);
};
