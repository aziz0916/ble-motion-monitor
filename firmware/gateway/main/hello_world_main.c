#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_ERROR_CHECK(esp_wifi_connect());
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        printf("Wi-Fi disconnected; reconnecting\n");
        ESP_ERROR_CHECK(esp_wifi_connect());
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        printf("Wi-Fi connected, IP: " IPSTR "\n", IP2STR(&event->ip_info.ip));
    }
}

static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    if (esp_netif_create_default_wifi_sta() == NULL) {
        abort();
    }

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));

    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_GATEWAY_WIFI_SSID,
            .password = CONFIG_GATEWAY_WIFI_PASSWORD,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

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
                if (data_length == 14 && data[0] == 0xFF &&
                    data[1] == 0xFF && data[2] == 0x03) {
                    uint32_t event_id = (uint32_t)data[4] |
                                        ((uint32_t)data[5] << 8) |
                                        ((uint32_t)data[12] << 16) |
                                        ((uint32_t)data[13] << 24);
                    uint32_t boot_id = (uint32_t)data[8] |
                                       ((uint32_t)data[9] << 8) |
                                       ((uint32_t)data[10] << 16) |
                                       ((uint32_t)data[11] << 24);

                    printf("boot_id=%lu\n", (unsigned long)boot_id);
                    static bool seen[256] = {0};
                    static unsigned int last_event[256] = {0};
                    static uint32_t last_boot[256] = {0};
                    uint8_t sensor_id = data[3];

                    if (seen[sensor_id] &&
                        last_boot[sensor_id] == boot_id &&
                        last_event[sensor_id] == event_id) {
                        printf("duplicate event\n");
                    } else {
                        seen[sensor_id] = true;
                        last_event[sensor_id] = event_id;
                        last_boot[sensor_id] = boot_id;
                        printf("new event\n");
                    }
                    printf("version=%u, sensor=%u, event=%lu, motion=%u, button=%u\n",
                           data[2], data[3], (unsigned long)event_id, data[6], data[7]);
                }
            }
        }
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    wifi_init();

    esp_bt_controller_config_t bt_config =
        BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_config));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_callback));
    ESP_ERROR_CHECK(esp_ble_gap_set_scan_params(&scan_params));
}