#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "nvs_flash.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_device.h"

static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x00A0,
    .adv_int_max = 0x00F0,
    .adv_type = ADV_TYPE_NONCONN_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static uint8_t sensor_data[] = {
    0xFF, 0xFF,       // 測試用識別欄位
    0x01,             // 格式版本
    0x01,             // 模擬感測器編號
    0x01, 0x00,       // 測試用事件編號：1
    0x01,             // 模擬活動：1
    0x00              // 模擬按鈕：0
};

static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .flag = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT,
    .manufacturer_len = sizeof(sensor_data),
    .p_manufacturer_data = sensor_data,
};

static void gap_callback(esp_gap_ble_cb_event_t event,
                         esp_ble_gap_cb_param_t *param)
{
    if (event == ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT &&
        param->adv_data_cmpl.status == ESP_BT_STATUS_SUCCESS) {
        esp_ble_gap_start_advertising(&adv_params);
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    esp_bt_controller_config_t bt_config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_config));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    printf("BLE initialized\n");

    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_callback));
    ESP_ERROR_CHECK(esp_ble_gap_set_device_name("BLE_Motion"));
    ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&adv_data));

    unsigned int count = 0;

    while (1) {
        const char *motion = (count % 2 == 0) ? "active" : "motionless";
        const char *button = (count % 4 == 3) ? "pressed" : "released";

        printf("event_id=%u, motion=%s, button=%s\n", count + 1, motion, button);

        count++;
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}