#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "nvs_flash.h"
#include "nvs.h"

#define BUTTON_GPIO GPIO_NUM_0
#define BUTTON_POLL_MS 20
#define BUTTON_DEBOUNCE_SAMPLES 3

static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x00A0,
    .adv_int_max = 0x00F0,
    .adv_type = ADV_TYPE_NONCONN_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static uint8_t sensor_data[] = {
    0xFF, 0xFF,              // 測試用識別欄位
    0x03,                    // 格式版本
    0x01,                    // 感測器編號
    0x00, 0x00,              // 事件編號低 2 位元組
    0x00,                    // 活動狀態：尚未接入感測器
    0x01,                    // 按鈕事件
    0x00, 0x00, 0x00, 0x00,  // 啟動編號
    0x00, 0x00               // 事件編號高 2 位元組
};

static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .flag = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT,
    .manufacturer_len = sizeof(sensor_data),
    .p_manufacturer_data = sensor_data,
};

static SemaphoreHandle_t advertising_started;
static SemaphoreHandle_t advertising_stopped;

static void gap_callback(esp_gap_ble_cb_event_t event,
                         esp_ble_gap_cb_param_t *param)
{
    if (event == ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT) {
        if (param->adv_data_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_ERROR_CHECK(esp_ble_gap_start_advertising(&adv_params));
        } else {
            printf("BLE advertising data configuration failed: %d\n",
                   param->adv_data_cmpl.status);
        }
    } else if (event == ESP_GAP_BLE_ADV_START_COMPLETE_EVT) {
        if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            printf("BLE advertising started\n");
            xSemaphoreGive(advertising_started);
        } else {
            printf("BLE advertising start failed: %d\n",
                   param->adv_start_cmpl.status);
        }
    } else if (event == ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT) {
        if (param->adv_stop_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            xSemaphoreGive(advertising_stopped);
        } else {
            printf("BLE advertising stop failed: %d\n",
                   param->adv_stop_cmpl.status);
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

static void set_event_id(uint32_t event_id)
{
    sensor_data[4] = event_id & 0xFF;
    sensor_data[5] = (event_id >> 8) & 0xFF;
    sensor_data[12] = (event_id >> 16) & 0xFF;
    sensor_data[13] = (event_id >> 24) & 0xFF;
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    uint32_t boot_id = next_boot_id();

    sensor_data[8] = boot_id & 0xFF;
    sensor_data[9] = (boot_id >> 8) & 0xFF;
    sensor_data[10] = (boot_id >> 16) & 0xFF;
    sensor_data[11] = (boot_id >> 24) & 0xFF;

    printf("boot_id=%" PRIu32 "\n", boot_id);

    gpio_config_t button_config = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&button_config));

    advertising_started = xSemaphoreCreateBinary();
    advertising_stopped = xSemaphoreCreateBinary();
    assert(advertising_started != NULL);
    assert(advertising_stopped != NULL);

    esp_bt_controller_config_t bt_config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_config));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_callback));
    ESP_ERROR_CHECK(esp_ble_gap_set_device_name("BLE_Motion"));

    printf("Ready. Press IO0 after startup.\n");

    uint32_t event_id = 0;
    bool advertising = false;
    int stable_level = gpio_get_level(BUTTON_GPIO);
    int previous_level = stable_level;
    unsigned int same_samples = 0;

    while (1) {
        int level = gpio_get_level(BUTTON_GPIO);

        if (level == previous_level) {
            if (same_samples < BUTTON_DEBOUNCE_SAMPLES) {
                same_samples++;
            }
        } else {
            previous_level = level;
            same_samples = 1;
        }

        if (same_samples >= BUTTON_DEBOUNCE_SAMPLES &&
            level != stable_level) {
            stable_level = level;

            if (stable_level == 0) {
                if (event_id == UINT32_MAX) {
                    printf("Event ID exhausted; restart sensor.\n");
                    abort();
                }

                if (advertising) {
                    ESP_ERROR_CHECK(esp_ble_gap_stop_advertising());
                    xSemaphoreTake(advertising_stopped, portMAX_DELAY);
                    advertising = false;
                }

                event_id++;
                set_event_id(event_id);

                printf("Button pressed: boot=%" PRIu32
                       ", event=%" PRIu32 ", motion=0, button=1\n",
                       boot_id, event_id);

                ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&adv_data));
                xSemaphoreTake(advertising_started, portMAX_DELAY);
                advertising = true;
            } else {
                printf("Button released\n");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
    }
}