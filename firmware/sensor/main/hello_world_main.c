#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "nvs_flash.h"
#include "nvs.h"
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

    if (event == ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT) {
        if (param->adv_stop_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&adv_data));
        } else {
            printf("BLE advertising stop failed\n");
        }
    }
}

static uint32_t next_boot_id(void)
{
    nvs_handle_t handle;
    uint32_t boot_id = 0;

    ESP_ERROR_CHECK(nvs_open("sensor", NVS_READWRITE, &handle));

    esp_err_t err = nvs_get_u32(handle, "boot_id", &boot_id);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_ERROR_CHECK(err);
    }

    if (boot_id == UINT32_MAX) {
        printf("Boot ID exhausted\n");
        abort();
    }

    boot_id++;
    ESP_ERROR_CHECK(nvs_set_u32(handle, "boot_id", boot_id));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);

    return boot_id;
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
    uint32_t boot_id = next_boot_id();
    printf("boot_id=%lu\n", (unsigned long)boot_id);

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
    vTaskDelay(pdMS_TO_TICKS(1000));

    while (1) {
        const char *motion = (count % 2 == 0) ? "active" : "motionless";
        const char *button = (count % 4 == 3) ? "pressed" : "released";

        unsigned int event_id = (count + 1) & 0xFFFF;
        sensor_data[4] = event_id & 0xFF;
        sensor_data[5] = (event_id >> 8) & 0xFF;
        sensor_data[6] = (count % 2 == 0) ? 1 : 0;
        sensor_data[7] = (count % 4 == 3) ? 1 : 0;

        printf("event_id=%u, motion=%s, button=%s, packet=%02X %02X %02X %02X\n",
               event_id, motion, button,
               sensor_data[4], sensor_data[5], sensor_data[6], sensor_data[7]);

        ESP_ERROR_CHECK(esp_ble_gap_stop_advertising());
        count++;
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}