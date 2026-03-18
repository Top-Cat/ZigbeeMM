#include <stdint.h>
#include <functional>
#include "driver/gpio.h"

#define UART_PORT         UART_NUM_1
#define TX_BUF_SIZE       256
#define RX_BUF_SIZE       256

typedef enum {
    L_CMD_RESOLUTION_SET = 0x01,
    L_CMD_BASE_PARAM_SET = 0x02,
    L_CMD_SENSITIVITY_MOTION_SET = 0x03,
    L_CMD_SENSITIVITY_STATIC_SET = 0x04,
    L_CMD_BDCE_START = 0x0B,
    L_CMD_LIGHT_AUX_SET = 0x0C,
    L_CMD_RESOLUTION_GET = 0x11,
    L_CMD_BASE_PARAM_GET = 0x12,
    L_CMD_SENSITIVITY_MOTION_GET = 0x13,
    L_CMD_SENSITIVITY_STATIC_GET = 0x14,
    L_CMD_DBCE_STATUS_GET = 0x1B,
    L_CMD_LIGHT_AUX_GET = 0x1C,
    L_CMD_ENGINEER_ENABLE = 0x62,
    L_CMD_ENGINEER_DISABLE = 0x63,
    L_CMD_FIRMWARE_INFO = 0xA0,
    L_CMD_BAUD_SET = 0xA1,
    L_CMD_FACTORY_RESET = 0xA2,
    L_CMD_REBOOT = 0xA3,
    L_CMD_BLUETOOTH = 0xA4,
    L_CMD_MAC_GET = 0xA5,
    L_CMD_DISABLE_CONFIG = 0xFE,
    L_CMD_ENABLE_CONFIG = 0xFF
} LD2412_cmd;

typedef enum {
    L_BAUD_9600 = 0x01,
    L_BAUD_19200 = 0x02,
    L_BAUD_38400 = 0x03,
    L_BAUD_57600 = 0x04,
    L_BAUD_115200 = 0x05, // DEFAULT
    L_BAUD_230400 = 0x06,
    L_BAUD_256000 = 0x07,
    L_BAUD_460800 = 0x08,
} LD2412_baud;

typedef enum {
    L_RESOLUTION_75 = 0x00,
    L_RESOLUTION_50 = 0x01,
    L_RESOLUTION_20 = 0x03,
} LD2412_resolution;

typedef enum {
    L_FRAME_UNKNOWN,
    L_FRAME_CMD,
    L_FRAME_DATA
} LD2412_frame;

typedef enum {
    L_DATA_ENGINEERING = 0x01,
    L_DATA_BASIC = 0x02
} LD2412_datatype;

typedef enum {
    L_DBCE_UNTARGETTED = 0x00,
    L_DBCE_CAMPAIGN_TARGET = 0x01,
    L_DBCE_STATIONARY_TARGET = 0x02,
    L_DBCE_BOTH_TARGET = 0x03,
    L_DBCE_IN_PROGRESS = 0x04,
    L_DBCE_SUCCESSFUL = 0x05,
    L_DBCE_FAILURE = 0x06
} LD2412_DBCE_status;

typedef enum {
    L_AUX_OFF = 0x00,
    L_AUX_BELOW = 0x01,
    L_AUX_ABOVE = 0x02,
} LD2412_AuxMode;

struct EngineeringData {
    uint8_t movingMaximum;
    uint8_t staticMaximum;
    uint8_t movingEnergy[14];
    uint8_t staticEnergy[14];
    uint8_t lux;
    uint8_t reserved;
};

struct LD2412Data {
    LD2412_DBCE_status state;
    uint16_t movingDistance;
    uint8_t movingEnergy;
    uint16_t stationaryDistance;
    uint8_t stationaryEnergy;
    bool hasEnginneringData;
    EngineeringData ed = {};
};

struct FirmwareVersion {
    uint16_t firmwareType;
    uint16_t majorVersion;
    uint32_t minorVersion;
};

struct AckData {
    LD2412_cmd cmd;
    uint8_t* data;
    uint16_t len;
};

struct LD2412Config {
    uint8_t minDistanceGate;
    uint8_t maxDistanceGate;
    uint16_t unoccupiedDuration;
    uint8_t outputPolarity;
};

struct LD2412Aux {
    LD2412_AuxMode mode;
    uint8_t threshold;
};

class LD2412 {
    public:
        LD2412() {};
        ~LD2412() {};
    
        void init(gpio_num_t txPin, gpio_num_t rxPin);
        void processRx(const uint8_t* buffer, const uint16_t size);
        void onData(void (*callback)(LD2412Data));
        bool restart();
        bool factoryReset();

        bool setConfig(const bool cfg, const uint8_t retry = 1);
        bool triggerCorrection();
        bool isCorrectionRunning();

        bool setBaud(const LD2412_baud baud);
        bool setResolution(const LD2412_resolution res);
        bool setBluetooth(const bool state);
        bool setEngineeringMode(const bool state);
        bool setMotionSensitivity(const uint8_t* gates);
        bool setStaticSensitivity(const uint8_t* gates);
        bool setBasic(const LD2412Config conf);
        bool setAux(const LD2412Aux conf);

        bool getResolution(LD2412_resolution* out);
        FirmwareVersion getFirmwareVersion();
        bool getMac(uint8_t* out);
        bool getMotionSensitivity(uint8_t* out);
        bool getStaticSensitivity(uint8_t* out);
        bool getBasic(LD2412Config* out);
        bool getAux(LD2412Aux* out);
    private:
        const char *TAG = "LD2";
        const uint8_t cmdHeader[4] = {0xFD, 0xFC, 0xFB, 0xFA};
        const uint8_t cmdFooter[4] = {0x04, 0x03, 0x02, 0x01};

        void (*on_data)(LD2412Data);

        bool configEnabled = false;

        bool sendCommand(const LD2412_cmd cmd, const uint8_t* args, const uint16_t argLen, std::function<void(AckData)> ack);

        void processData(const uint8_t* buffer, const uint16_t size);
        void processAck(const uint8_t* buffer, const uint16_t size);
        LD2412_frame checkHeader(const uint8_t* buffer, const uint16_t size);
        LD2412_frame checkFooter(const uint8_t* buffer, const uint16_t size);
};

extern LD2412 mmwave;
