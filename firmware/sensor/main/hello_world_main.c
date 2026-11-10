#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "nvs_flash.h"

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

    unsigned int count = 0;

    while (1) {
        const char *motion = (count % 2 == 0) ? "active" : "motionless";
        const char *button = (count % 4 == 3) ? "pressed" : "released";

        printf("event_id=%u, motion=%s, button=%s\n", count + 1, motion, button);

        count++;
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}