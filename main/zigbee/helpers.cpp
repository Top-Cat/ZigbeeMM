#include "helpers.h"

void fill_zcl_string(char *dest, size_t max_len, const char *src) {
    if (!dest || !src || max_len < 2) return;
    
    size_t src_len = strlen(src);
    size_t copy_len = (src_len < max_len - 1) ? src_len : max_len - 2;

    dest[0] = (char)copy_len;  // First byte = length
    memcpy(dest + 1, src, copy_len);
    dest[copy_len + 1] = '\0';  // Null terminate for safety
}

const char *esp_zb_zcl_status_to_name(esp_zb_zcl_status_t status) {
    switch (status) {
        case ESP_ZB_ZCL_STATUS_SUCCESS:               return "Success";
        case ESP_ZB_ZCL_STATUS_FAIL:                  return "Fail";
        case ESP_ZB_ZCL_STATUS_NOT_AUTHORIZED:        return "Not authorized";
        case ESP_ZB_ZCL_STATUS_MALFORMED_CMD:         return "Malformed command";
        case ESP_ZB_ZCL_STATUS_UNSUP_CLUST_CMD:       return "Unsupported cluster command";
        case ESP_ZB_ZCL_STATUS_UNSUP_GEN_CMD:         return "Unsupported general command";
        case ESP_ZB_ZCL_STATUS_UNSUP_MANUF_CLUST_CMD: return "Unsupported manufacturer cluster command";
        case ESP_ZB_ZCL_STATUS_UNSUP_MANUF_GEN_CMD:   return "Unsupported manufacturer general command";
        case ESP_ZB_ZCL_STATUS_INVALID_FIELD:         return "Invalid field";
        case ESP_ZB_ZCL_STATUS_UNSUP_ATTRIB:          return "Unsupported attribute";
        case ESP_ZB_ZCL_STATUS_INVALID_VALUE:         return "Invalid value";
        case ESP_ZB_ZCL_STATUS_READ_ONLY:             return "Read only";
        case ESP_ZB_ZCL_STATUS_INSUFF_SPACE:          return "Insufficient space";
        case ESP_ZB_ZCL_STATUS_DUPE_EXISTS:           return "Duplicate exists";
        case ESP_ZB_ZCL_STATUS_NOT_FOUND:             return "Not found";
        case ESP_ZB_ZCL_STATUS_UNREPORTABLE_ATTRIB:   return "Unreportable attribute";
        case ESP_ZB_ZCL_STATUS_INVALID_TYPE:          return "Invalid type";
        case ESP_ZB_ZCL_STATUS_WRITE_ONLY:            return "Write only";
        case ESP_ZB_ZCL_STATUS_INCONSISTENT:          return "Inconsistent";
        case ESP_ZB_ZCL_STATUS_ACTION_DENIED:         return "Action denied";
        case ESP_ZB_ZCL_STATUS_TIMEOUT:               return "Timeout";
        case ESP_ZB_ZCL_STATUS_ABORT:                 return "Abort";
        case ESP_ZB_ZCL_STATUS_INVALID_IMAGE:         return "Invalid OTA upgrade image";
        case ESP_ZB_ZCL_STATUS_WAIT_FOR_DATA:         return "Server does not have data block available yet";
        case ESP_ZB_ZCL_STATUS_NO_IMAGE_AVAILABLE:    return "No image available";
        case ESP_ZB_ZCL_STATUS_REQUIRE_MORE_IMAGE:    return "Require more image";
        case ESP_ZB_ZCL_STATUS_NOTIFICATION_PENDING:  return "Notification pending";
        case ESP_ZB_ZCL_STATUS_HW_FAIL:               return "Hardware failure";
        case ESP_ZB_ZCL_STATUS_SW_FAIL:               return "Software failure";
        case ESP_ZB_ZCL_STATUS_CALIB_ERR:             return "Calibration error";
        case ESP_ZB_ZCL_STATUS_UNSUP_CLUST:           return "Cluster is not found on the target endpoint";
        case ESP_ZB_ZCL_STATUS_LIMIT_REACHED:         return "Limit reached";
        default:                                      return "Unknown status";
    }
}
