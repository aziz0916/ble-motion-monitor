#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "nvs_flash.h"

static esp_ble_scan_params_t scan_params = {
    .scan_type = BLE_SCAN_TYPE_PASSIVE,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval = 0x50,
    .scan_window = 0x30,
    .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
};

static void gap_callback(esp_gap_ble_cb_event_t event,
                         esp_ble_gap_cb_param_t *param)
{
    if (event == ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT) {
        if (param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_ERROR_CHECK(esp_ble_gap_start_scanning(0));
        } else {
            printf("BLE scan parameters failed\n");
        }
    }

    if (event == ESP_GAP_BLE_SCAN_START_COMPLETE_EVT) {
        if (param->scan_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            printf("BLE scan started\n");
        } else {
            printf("BLE scan start failed\n");
        }
    }

    if (event == ESP_GAP_BLE_SCAN_RESULT_EVT &&
        param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
        uint8_t name_length = 0;
        uint8_t *name = esp_ble_resolve_adv_data(
            param->scan_rst.ble_adv,
            ESP_BLE_AD_TYPE_NAME_CMPL,
            &name_length
        );

        const char *target = "BLE_Motion";
        if (name != NULL &&
            name_length == strlen(target) &&
            memcmp(name, target, name_length) == 0) {
            printf("Found BLE_Motion, RSSI=%d dBm\n",
                   param->scan_rst.rssi);
            uint8_t data_length = 0;
            uint8_t *data = esp_ble_resolve_adv_data(
                param->scan_rst.ble_adv,
                ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE,
                &data_length
            );

            if (data != NULL) {
                printf("Manufacturer data:");
                for (uint8_t i = 0; i < data_length; i++) {
                    printf(" %02X", data[i]);
                }
                printf("\n");
            }
        }
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    esp_bt_controller_config_t bt_config =
        BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_config));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_callback));
    ESP_ERROR_CHECK(esp_ble_gap_set_scan_params(&scan_params));
}