#include "ld2412.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"

LD2412 mmwave;
QueueHandle_t uartRxQueue;
QueueHandle_t ackQueue;

void uartTask(void *pvParameters) {
    static const char *TAG = "UART";
    uart_event_t event;
    uint8_t rxBuffer[RX_BUF_SIZE];

    while (true) {
        if (xQueueReceive(uartRxQueue, (void * )&event, portMAX_DELAY)) {
            switch(event.type) {
                case UART_DATA:
                    ESP_LOGD(TAG, "[UART DATA]: %d", event.size);
                    uart_read_bytes(UART_PORT, rxBuffer, event.size, portMAX_DELAY);
                    mmwave.processRx(rxBuffer, event.size);
                    break;
                case UART_BREAK:
                    // Got NULL. Probably due to reboot?
                    break;
                default:
                    ESP_LOGI(TAG, "uart event type: %d", event.type);
                    break;
            }
        }
    }
}

void LD2412::init(gpio_num_t txPin, gpio_num_t rxPin) {
    const uart_config_t uart_config = {
        .baud_rate  = 115200,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {
            .allow_pd = 0,
            .backup_before_sleep = 0
        }
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, RX_BUF_SIZE, TX_BUF_SIZE, 10, &uartRxQueue, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, txPin, rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ackQueue = xQueueCreate(4, sizeof(AckData));
    xTaskCreate(uartTask, "uartRx", 4096, NULL, 4, NULL);
}

LD2412_frame LD2412::checkHeader(const uint8_t* buffer, const uint16_t size) {
    if (size < 4) return L_FRAME_UNKNOWN;
    if (buffer[0] == 0xF4 && buffer[1] == 0xF3 && buffer[2] == 0xF2 && buffer[3] == 0xF1) return L_FRAME_DATA;
    if (buffer[0] == cmdHeader[0] && buffer[1] == cmdHeader[1] && buffer[2] == cmdHeader[2] && buffer[3] == cmdHeader[3]) return L_FRAME_CMD;
    return L_FRAME_UNKNOWN;
}

LD2412_frame LD2412::checkFooter(const uint8_t* buffer, const uint16_t size) {
    if (size < 8) return L_FRAME_UNKNOWN;
    if (buffer[size - 4] == 0xF8 && buffer[size - 3] == 0xF7 && buffer[size - 2] == 0xF6 && buffer[size - 1] == 0xF5) return L_FRAME_DATA;
    if (buffer[size - 4] == cmdFooter[0] && buffer[size - 3] == cmdFooter[1] && buffer[size - 2] == cmdFooter[2] && buffer[size - 1] == cmdFooter[3]) return L_FRAME_CMD;
    return L_FRAME_UNKNOWN;
}

inline uint16_t readLE16(const uint8_t* buff) {
    return buff[0] | (buff[1] << 8);
}

inline uint32_t readLE32(const uint8_t* buff) {
    return buff[0] | (buff[1] << 8) | (buff[2] << 16) | (buff[3] << 24);
}

inline void writeLE16(const uint16_t in, uint8_t* buff) {
    buff[0] = in & 0xFF;
    buff[1] = in >> 8;
}

void LD2412::processData(const uint8_t* buff, const uint16_t size) {
    if (buff[1] != 0xAA || buff[size - 2] != 0x55) return;

    LD2412Data data = {
        .state = (LD2412_DBCE_status) buff[2],
        .movingDistance = readLE16(&buff[3]),
        .movingEnergy = buff[5],
        .stationaryDistance = readLE16(&buff[6]),
        .stationaryEnergy = buff[8],
        .hasEnginneringData = false
    };

    if (buff[0] == L_DATA_ENGINEERING) {
        data.hasEnginneringData = true;
        data.ed.movingMaximum = buff[9];
        data.ed.staticMaximum = buff[10];
        data.ed.lux = buff[39];
        data.ed.reserved = buff[40];
        memcpy(data.ed.movingEnergy, &buff[11], 14);
        memcpy(data.ed.staticEnergy, &buff[25], 14);
    }

    if (on_data) on_data(data);
}

void LD2412::processAck(const uint8_t* buff, const uint16_t size) {
    if (buff[1] != 0x01) {
        ESP_LOGW(TAG, "Invalid ACK cmd");
        return;
    }

    LD2412_cmd ackCmd = (LD2412_cmd) buff[0];
    ESP_LOGV(TAG, "ACK for %d", ackCmd);

    AckData ackData = {
        .cmd = ackCmd,
        .data = (uint8_t*) malloc(size - 2),
        .len = (uint16_t) (size - 2)
    };
    memcpy(ackData.data, &buff[2], ackData.len);
    xQueueSend(ackQueue, &ackData, 0);
}

void LD2412::processRx(const uint8_t* buff, const uint16_t size) {
    uint16_t packetLength = size < 6 ? 0 : buff[4] | (buff[5] << 8);
    LD2412_frame headerType = checkHeader(buff, size);
    LD2412_frame footerType = checkFooter(buff, 10 + packetLength);

    // Ignore malformed data
    if (headerType != footerType || headerType == L_FRAME_UNKNOWN) {
        ESP_LOGI(TAG, "Malformed UART data");
        return;
    }

    if (headerType == L_FRAME_DATA) {
        ESP_LOGV(TAG, "Time Target data received: %lld us", esp_timer_get_time());
        processData(&buff[6], packetLength);
    } else {
        ESP_LOGV(TAG, "Time ACK data received: %lld us", esp_timer_get_time());
        processAck(&buff[6], packetLength);
    }
}

static uint8_t uartTx[64];

bool LD2412::sendCommand(const LD2412_cmd cmd, const uint8_t* args, const uint16_t argLen, std::function<void(AckData)> ack) {
    uint16_t len = /* cmd */ sizeof(uint16_t) + argLen;
    uint16_t packetLen = sizeof(cmdHeader) + /* len */ sizeof(uint16_t) + len + sizeof(cmdFooter);

    memcpy(uartTx, cmdHeader, sizeof(cmdHeader));

    writeLE16(len, &uartTx[4]);
    writeLE16(cmd, &uartTx[6]);

    memcpy(&uartTx[8], args, argLen);
    memcpy(&uartTx[argLen + 8], cmdFooter, sizeof(cmdFooter));

    uart_write_bytes(UART_PORT, uartTx, packetLen);

    AckData ackData;
    bool res = false;
    if ((res = xQueueReceive(ackQueue, &ackData, 500 / portTICK_PERIOD_MS))) {
        if (ackData.cmd == cmd) {
            ack(ackData);
        } else {
            res = false;
        }
        free(ackData.data);
    }

    return res;
}

FirmwareVersion LD2412::getFirmwareVersion() {
    FirmwareVersion res = {};
    if (!configEnabled) return res;

    sendCommand(L_CMD_FIRMWARE_INFO, NULL, 0, [&res](AckData ack) {
        if (readLE16(&ack.data[0]) != 0x0000) return;

        res = {
            .firmwareType = readLE16(&ack.data[2]),
            .majorVersion = readLE16(&ack.data[4]),
            .minorVersion = readLE32(&ack.data[6]),
        };
    });

    return res;
}

bool LD2412::setConfig(const bool state, const uint8_t retry) {
    if (configEnabled == state) return true;

    bool res = false;
    if (state) {
        uint8_t args[] = {0x01, 0x00};
        res = sendCommand(L_CMD_ENABLE_CONFIG, args, 2, [](AckData ack) {});
    } else {
        res = sendCommand(L_CMD_DISABLE_CONFIG, NULL, 0, [](AckData ack) {});
    }

    if (res) {
        configEnabled = state;
    } else if (retry > 0) {
        return setConfig(state, retry - 1);
    }

    return res;
}

bool LD2412::setBluetooth(const bool state) {
    if (!configEnabled) return false;

    uint8_t args[2];
    writeLE16(state ? 0x01 : 0x00, args);
    return sendCommand(L_CMD_BLUETOOTH, args, 2, [](AckData ack) {});
}

bool LD2412::setEngineeringMode(const bool state) {
    if (!configEnabled) return false;

    if (state) {
        return sendCommand(L_CMD_ENGINEER_ENABLE, NULL, 0, [](AckData ack) {});
    } else {
        return sendCommand(L_CMD_ENGINEER_DISABLE, NULL, 0, [](AckData ack) {});
    }
}

bool LD2412::getMac(uint8_t* out) {
    if (!configEnabled) return false;

    uint8_t args[] = {0x01, 0x00};
    return sendCommand(L_CMD_MAC_GET, args, 2, [out](AckData ack) {
        if (readLE16(&ack.data[0]) != 0x0000) return;

        memcpy(out, &ack.data[2], 6);
    });
}

bool LD2412::setResolution(const LD2412_resolution res) {
    if (!configEnabled) return false;

    uint8_t args[] = {res, 0x00, 0x00, 0x00, 0x00, 0x00};
    return sendCommand(L_CMD_RESOLUTION_SET, args, 2, [](AckData ack) {});
}

bool LD2412::getResolution(LD2412_resolution* out) {
    if (!configEnabled) return false;

    return sendCommand(L_CMD_RESOLUTION_GET, NULL, 0, [out](AckData ack) {
        if (readLE16(&ack.data[0]) != 0x0000) return;

        *out = (LD2412_resolution) ack.data[2];
    });
}

bool LD2412::getMotionSensitivity(uint8_t* out) {
    if (!configEnabled) return false;

    return sendCommand(L_CMD_SENSITIVITY_MOTION_GET, NULL, 0, [out](AckData ack) {
        if (readLE16(&ack.data[0]) != 0x0000) return;

        memcpy(out, &ack.data[2], 14);
    });
}

bool LD2412::setMotionSensitivity(const uint8_t* gates) {
    if (!configEnabled) return false;

    return sendCommand(L_CMD_SENSITIVITY_MOTION_SET, gates, 14, [](AckData ack) {});
}

bool LD2412::getStaticSensitivity(uint8_t* out) {
    if (!configEnabled) return false;

    return sendCommand(L_CMD_SENSITIVITY_STATIC_GET, NULL, 0, [out](AckData ack) {
        if (readLE16(&ack.data[0]) != 0x0000) return;

        memcpy(out, &ack.data[2], 14);
    });
}

bool LD2412::setStaticSensitivity(const uint8_t* gates) {
    if (!configEnabled) return false;

    return sendCommand(L_CMD_SENSITIVITY_STATIC_SET, gates, 14, [](AckData ack) {});
}

bool LD2412::triggerCorrection() {
    if (!configEnabled) return false;

    return sendCommand(L_CMD_BDCE_START, NULL, 0, [](AckData ack) {});
}

bool LD2412::isCorrectionRunning() {
    if (!configEnabled) return false;

    bool status = false;
    sendCommand(L_CMD_DBCE_STATUS_GET, NULL, 0, [&status](AckData ack) {
        if (readLE16(&ack.data[0]) != 0x0000) return;

        status = ack.data[2];
    });

    return status;
}

bool LD2412::restart() {
    if (!configEnabled) return false;

    if (sendCommand(L_CMD_REBOOT, NULL, 0, [](AckData ack) {})) {
        configEnabled = false;
        return true;
    }

    return false;
}

bool LD2412::factoryReset() {
    if (!configEnabled) return false;

    return sendCommand(L_CMD_FACTORY_RESET, NULL, 0, [](AckData ack) {});
}

bool LD2412::setBaud(const LD2412_baud baud) {
    if (!configEnabled) return false;

    uint8_t args[2];
    writeLE16(baud, args);
    return sendCommand(L_CMD_BAUD_SET, args, 2, [](AckData ack) {});
}

bool LD2412::getBasic(LD2412Config* out) {
    if (!configEnabled) return false;

    return sendCommand(L_CMD_BASE_PARAM_GET, NULL, 0, [out](AckData ack) {
        if (readLE16(&ack.data[0]) != 0x0000) return;

        out->minDistanceGate = ack.data[2];
        out->maxDistanceGate = ack.data[3];
        out->unoccupiedDuration = readLE16(&ack.data[4]);
        out->outputPolarity = ack.data[6];
    });
}

bool LD2412::setBasic(const LD2412Config conf) {
    if (!configEnabled) return false;

    uint8_t args[5];
    args[0] = conf.minDistanceGate;
    args[1] = conf.maxDistanceGate;
    writeLE16(conf.unoccupiedDuration, &args[2]);
    args[4] = conf.outputPolarity;
    return sendCommand(L_CMD_BASE_PARAM_SET, args, 5, [](AckData ack) {});
}

bool LD2412::getAux(LD2412Aux* out) {
    if (!configEnabled) return false;

    return sendCommand(L_CMD_LIGHT_AUX_GET, NULL, 0, [out](AckData ack) {
        if (readLE16(&ack.data[0]) != 0x0000) return;

        out->mode = (LD2412_AuxMode) ack.data[2];
        out->threshold = ack.data[3];
    });
}

bool LD2412::setAux(const LD2412Aux conf) {
    if (!configEnabled) return false;

    uint8_t args[2];
    args[0] = conf.mode;
    args[1] = conf.threshold;
    return sendCommand(L_CMD_LIGHT_AUX_SET, args, 2, [](AckData ack) {});
}

void LD2412::onData(void (*callback)(LD2412Data)) {
    on_data = callback;
}
