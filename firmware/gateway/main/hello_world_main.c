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
#include "esp_http_client.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "event_store.h"
#include "freertos/semphr.h"

static QueueHandle_t event_queue;
static SemaphoreHandle_t event_store_mutex;

static void console_check_quarantined(unsigned int slot);

static void console_task(void *arg)
{
    char line[64];
    size_t length = 0;
    bool overflow = false;
    bool removal_pending = false;
    unsigned int pending_slot = 0;
    sensor_event_t pending_original;
    sensor_event_t pending_incoming;

    printf("Console ready. Type conflicts and press Enter.\n");

    while (1) {
        int ch = getchar();

        if (ch == EOF) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            if (overflow) {
                removal_pending = false;
                printf("Command too long.\n");
            } else if (length > 0) {
                line[length] = '\0';

                if (strcmp(line, "conflicts") == 0) {
                    removal_pending = false;
                    xSemaphoreTake(event_store_mutex, portMAX_DELAY);
                    esp_err_t err = event_store_list_local_conflicts();
                    xSemaphoreGive(event_store_mutex);

                    if (err != ESP_OK) {
                        printf("Local conflict list failed: %s\n",
                               esp_err_to_name(err));
                    }
                } else if (strcmp(line, "quarantined") == 0) {
                    removal_pending = false;

                    xSemaphoreTake(event_store_mutex, portMAX_DELAY);
                    esp_err_t err = event_store_list_quarantined();
                    xSemaphoreGive(event_store_mutex);

                    if (err != ESP_OK) {
                        printf("Quarantined list failed: %s\n",
                               esp_err_to_name(err));
                    }
                } else if (strncmp(line, "check-quarantined ", 18) == 0) {
                    unsigned int slot;
                    char extra;

                    removal_pending = false;

                    if (sscanf(line + 18, "%3u %c", &slot, &extra) != 1) {
                        printf("Usage: check-quarantined <slot>\n");
                    } else {
                        console_check_quarantined(slot);
                    }
                } else if (strcmp(line, "confirm-remove") == 0) {
                    if (!removal_pending) {
                        printf("No removal pending. Use inspect <slot> first.\n");
                    } else {
                        xSemaphoreTake(event_store_mutex, portMAX_DELAY);
                        esp_err_t err = event_store_remove_local_conflict(
                            pending_slot, &pending_original, &pending_incoming);
                        xSemaphoreGive(event_store_mutex);

                        removal_pending = false;

                        if (err == ESP_OK) {
                            printf("Local conflict removed: slot=%u\n",
                                   pending_slot);
                        } else {
                            printf("Local conflict removal failed: %s\n",
                                   esp_err_to_name(err));
                        }
                    }
                } else if (strncmp(line, "inspect ", 8) == 0) {
                    char extra;
                    unsigned int slot;

                    removal_pending = false;

                    if (sscanf(line + 8, "%u %c", &slot, &extra) != 1) {
                        printf("Usage: inspect <slot>\n");
                    } else {
                        sensor_event_t original;
                        sensor_event_t incoming;

                        xSemaphoreTake(event_store_mutex, portMAX_DELAY);
                        esp_err_t err = event_store_get_local_conflict(
                            slot, &original, &incoming);
                        xSemaphoreGive(event_store_mutex);

                        if (err == ESP_OK) {
                            pending_slot = slot;
                            pending_original = original;
                            pending_incoming = incoming;
                            removal_pending = true;

                            printf("Local conflict slot=%u\n", slot);
                            printf("Original: sensor=%u, boot=%lu, event=%lu, "
                                   "motion=%u, button=%u, version=%u, rssi=%d\n",
                                   (unsigned int)original.sensor_id,
                                   (unsigned long)original.boot_id,
                                   (unsigned long)original.event_id,
                                   (unsigned int)original.motion,
                                   (unsigned int)original.button,
                                   (unsigned int)original.version,
                                   original.rssi);
                            printf("Incoming: sensor=%u, boot=%lu, event=%lu, "
                                   "motion=%u, button=%u, version=%u, rssi=%d\n",
                                   (unsigned int)incoming.sensor_id,
                                   (unsigned long)incoming.boot_id,
                                   (unsigned long)incoming.event_id,
                                   (unsigned int)incoming.motion,
                                   (unsigned int)incoming.button,
                                   (unsigned int)incoming.version,
                                   incoming.rssi);
                            printf("To remove this record, type confirm-remove.\n");
                        } else {
                            printf("Inspect failed: %s\n",
                                   esp_err_to_name(err));
                        }
                    }
                } else {
                    removal_pending = false;
                    printf("Unknown command. Type conflicts.\n");
                }
            }

            length = 0;
            overflow = false;
        } else if (!overflow) {
            if (length < sizeof(line) - 1) {
                line[length++] = (char)ch;
            } else {
                overflow = true;
            }
        }
    }
}

static void http_test_task(void *arg)
{
    esp_http_client_config_t config = {
        .url = CONFIG_GATEWAY_HTTP_TEST_URL,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        printf("HTTP client initialization failed\n");
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        printf("HTTP test status: %d\n",
               esp_http_client_get_status_code(client));
    } else {
        printf("HTTP test failed: %s\n", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    vTaskDelete(NULL);
}

typedef struct {
    char data[1024];
    size_t length;
    bool overflow;
} event_response_t;

static esp_err_t event_response_handler(esp_http_client_event_t *event)
{
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        event_response_t *response = event->user_data;
        size_t incoming = (size_t)event->data_len;

        if (response == NULL ||
            incoming > sizeof(response->data) - 1 - response->length) {
            if (response != NULL) {
                response->overflow = true;
            }
            return ESP_FAIL;
        }

        memcpy(response->data + response->length, event->data, incoming);
        response->length += incoming;
        response->data[response->length] = '\0';
    }

    return ESP_OK;
}

typedef enum {
    EVENT_CHECK_MATCH,
    EVENT_CHECK_CONFLICT,
    EVENT_CHECK_NOT_FOUND,
    EVENT_CHECK_ERROR
} event_check_result_t;

static bool json_number_matches(const cJSON *object, const char *name,
                                double expected)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(item) && item->valuedouble == expected;
}

static event_check_result_t event_response_matches(
    const char *body, const sensor_event_t *event)
{
    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        return EVENT_CHECK_ERROR;
    }

    event_check_result_t result = EVENT_CHECK_ERROR;
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");

    if (cJSON_IsArray(data)) {
        int count = cJSON_GetArraySize(data);

        if (count == 0) {
            result = EVENT_CHECK_NOT_FOUND;
        } else if (count == 1) {
            const cJSON *item = cJSON_GetArrayItem(data, 0);

            if (cJSON_IsObject(item) &&
                json_number_matches(item, "sensor_id", event->sensor_id) &&
                json_number_matches(item, "boot_id", event->boot_id) &&
                json_number_matches(item, "event_id", event->event_id)) {

                const cJSON *motion =
                    cJSON_GetObjectItemCaseSensitive(item, "motion");
                const cJSON *button =
                    cJSON_GetObjectItemCaseSensitive(item, "button");
                const cJSON *version =
                    cJSON_GetObjectItemCaseSensitive(item, "version");

                if (cJSON_IsNumber(motion) &&
                    cJSON_IsNumber(button) &&
                    cJSON_IsNumber(version)) {
                    if (json_number_matches(item, "motion", event->motion) &&
                        json_number_matches(item, "button", event->button) &&
                        json_number_matches(item, "version", event->version)) {
                        result = EVENT_CHECK_MATCH;
                    } else {
                        result = EVENT_CHECK_CONFLICT;
                    }
                }
            }
        }
    }

    cJSON_Delete(root);
    return result;
}

static event_check_result_t event_already_recorded(
    const sensor_event_t *event, const char *authorization)
{
    char url[512];

    int written = snprintf(
        url, sizeof(url),
        "%s?filter=%%7B%%22sensor_id%%22%%3A%u%%2C"
        "%%22boot_id%%22%%3A%lu%%2C"
        "%%22event_id%%22%%3A%lu%%7D"
        "&fields=sensor_id,boot_id,event_id,motion,button,version",
        CONFIG_GATEWAY_EVENT_LIST_URL,
        (unsigned int)event->sensor_id,
        (unsigned long)event->boot_id,
        (unsigned long)event->event_id);

    if (written < 0 || (size_t)written >= sizeof(url)) {
        printf("Event check: URL is too long\n");
        return EVENT_CHECK_ERROR;
    }

    event_response_t response = {0};

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 10000,
        .event_handler = event_response_handler,
        .user_data = &response,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        printf("Event check: client initialization failed\n");
        return EVENT_CHECK_ERROR;
    }

    esp_err_t err =
        esp_http_client_set_header(client, "Authorization", authorization);
    if (err == ESP_OK) {
        err = esp_http_client_perform(client);
    }

    int status = 0;
    if (err == ESP_OK) {
        status = esp_http_client_get_status_code(client);
    }

    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200 || response.overflow) {
        printf("Event check failed: %s, HTTP status: %d\n",
               esp_err_to_name(err), status);
        return EVENT_CHECK_ERROR;
    }

    return event_response_matches(response.data, event);
}

static void console_check_quarantined(unsigned int slot)
{
    sensor_event_t event;

    xSemaphoreTake(event_store_mutex, portMAX_DELAY);
    esp_err_t err = event_store_get_quarantined(slot, &event);
    xSemaphoreGive(event_store_mutex);

    if (err != ESP_OK) {
        printf("Quarantined read failed: %s\n", esp_err_to_name(err));
        return;
    }

    printf("Checking slot=%u, sensor=%u, boot=%lu, event=%lu\n",
           slot,
           (unsigned int)event.sensor_id,
           (unsigned long)event.boot_id,
           (unsigned long)event.event_id);

    char authorization[sizeof(CONFIG_GATEWAY_API_KEY) + sizeof("Bearer ")];
    snprintf(authorization, sizeof(authorization),
             "Bearer %s", CONFIG_GATEWAY_API_KEY);

    event_check_result_t result = event_already_recorded(&event, authorization);

    switch (result) {
    case EVENT_CHECK_MATCH:
        printf("Backend result: matching record found\n");
        break;
    case EVENT_CHECK_CONFLICT:
        printf("Backend result: record content conflicts\n");
        break;
    case EVENT_CHECK_NOT_FOUND:
        printf("Backend result: record not found\n");
        break;
    case EVENT_CHECK_ERROR:
    default:
        printf("Backend result: unable to verify\n");
        break;
    }
}

static void conflict_check_task(void *arg)
{
    char authorization[sizeof(CONFIG_GATEWAY_API_KEY) + sizeof("Bearer ")];
    snprintf(authorization, sizeof(authorization),
             "Bearer %s", CONFIG_GATEWAY_API_KEY);

    while (1) {
        sensor_event_t event;
        bool found = false;

        xSemaphoreTake(event_store_mutex, portMAX_DELAY);
        esp_err_t err = event_store_peek_conflict(&event, &found);
        xSemaphoreGive(event_store_mutex);

        if (err != ESP_OK) {
            printf("Conflict storage read failed: %s\n",
                   esp_err_to_name(err));
        } else if (found) {
            event_check_result_t result =
                event_already_recorded(&event, authorization);

            if (result == EVENT_CHECK_MATCH) {
                xSemaphoreTake(event_store_mutex, portMAX_DELAY);
                err = event_store_release_conflict(&event);
                xSemaphoreGive(event_store_mutex);

                if (err == ESP_OK) {
                    printf("Conflict released: sensor=%u, boot=%lu, event=%lu\n",
                           (unsigned int)event.sensor_id,
                           (unsigned long)event.boot_id,
                           (unsigned long)event.event_id);
                } else {
                    printf("Conflict release failed: %s\n",
                           esp_err_to_name(err));
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

static void event_store_task(void *arg)
{
    sensor_event_t event;

    while (1) {
        if (xQueuePeek(event_queue, &event, portMAX_DELAY) != pdPASS) {
            continue;
        }

        xSemaphoreTake(event_store_mutex, portMAX_DELAY);
        esp_err_t err = event_store_add(&event);
        xSemaphoreGive(event_store_mutex);

        if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
            if (xQueueReceive(event_queue, &event, 0) == pdPASS) {
                printf("Event saved or already in storage\n");
            }
        } else {
            printf("Event storage failed: %s; retrying in 5 seconds\n",
                   esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
}

static void event_upload_task(void *arg)
{
    sensor_event_t event;

    while (1) {
        bool found = false;

        xSemaphoreTake(event_store_mutex, portMAX_DELAY);
        esp_err_t store_err = event_store_peek(&event, &found);
        xSemaphoreGive(event_store_mutex);

        if (store_err != ESP_OK) {
            printf("Event storage read failed: %s\n",
                esp_err_to_name(store_err));
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        if (!found) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        char body[256];
        snprintf(body, sizeof(body),
                 "{\"sensor_id\":%u,\"boot_id\":%lu,\"event_id\":%lu,"
                 "\"motion\":%u,\"button\":%u,\"rssi\":%d,\"version\":%u}",
                 (unsigned int)event.sensor_id,
                 (unsigned long)event.boot_id,
                 (unsigned long)event.event_id,
                 (unsigned int)event.motion,
                 (unsigned int)event.button,
                 event.rssi,
                 (unsigned int)event.version);

        char authorization[sizeof(CONFIG_GATEWAY_API_KEY) + sizeof("Bearer ")];
        snprintf(authorization, sizeof(authorization),
                 "Bearer %s", CONFIG_GATEWAY_API_KEY);

        bool quarantined = false;

        while (1) {
            esp_http_client_config_t config = {
                .url = CONFIG_GATEWAY_EVENT_URL,
                .timeout_ms = 10000,
            };

            esp_http_client_handle_t client = esp_http_client_init(&config);
            if (client == NULL) {
                printf("Event upload: client initialization failed\n");
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }

            esp_err_t err = esp_http_client_set_method(client, HTTP_METHOD_POST);
            if (err == ESP_OK) {
                err = esp_http_client_set_header(client, "Authorization", authorization);
            }
            if (err == ESP_OK) {
                err = esp_http_client_set_header(client, "Content-Type", "application/json");
            }
            if (err == ESP_OK) {
                err = esp_http_client_set_post_field(client, body, strlen(body));
            }
            if (err == ESP_OK) {
                err = esp_http_client_perform(client);
            }

            int status = 0;
            if (err == ESP_OK) {
                status = esp_http_client_get_status_code(client);
                printf("Event upload HTTP status: %d\n", status);
            } else {
                printf("Event upload failed: %s\n", esp_err_to_name(err));
            }

            esp_http_client_cleanup(client);

            if (err == ESP_OK && status >= 200 && status < 300) {
                break;
            }

            event_check_result_t check_result =
    event_already_recorded(&event, authorization);

            if (check_result == EVENT_CHECK_MATCH) {
                printf("Event already recorded; upload confirmed\n");
                break;
            }

            if (check_result == EVENT_CHECK_CONFLICT) {
                xSemaphoreTake(event_store_mutex, portMAX_DELAY);
                esp_err_t mark_err = event_store_mark_conflict(&event);
                xSemaphoreGive(event_store_mutex);

                if (mark_err == ESP_OK) {
                    printf("Event content conflict quarantined: sensor=%u, boot=%lu, event=%lu\n",
       (unsigned int)event.sensor_id,
       (unsigned long)event.boot_id,
       (unsigned long)event.event_id);
                    quarantined = true;
                    break;
                }

                printf("Failed to quarantine event: %s\n",
                    esp_err_to_name(mark_err));
            }

            if (check_result == EVENT_CHECK_NOT_FOUND) {
                printf("Event check: record not found; retaining event\n");
            } else if (check_result == EVENT_CHECK_ERROR) {
                printf("Event check: unable to verify; retaining event\n");
            }

            printf("Event upload will retry in 5 seconds\n");
            vTaskDelay(pdMS_TO_TICKS(5000));
        }

        if (quarantined) {
            continue;
        }

        while (1) {
            xSemaphoreTake(event_store_mutex, portMAX_DELAY);
            esp_err_t remove_err = event_store_remove(&event);
            xSemaphoreGive(event_store_mutex);

            if (remove_err == ESP_OK || remove_err == ESP_ERR_NVS_NOT_FOUND) {
                printf("Confirmed event removed from storage\n");
                break;
            }

            printf("Event removal failed: %s; retrying in 5 seconds\n",
                   esp_err_to_name(remove_err));
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
}

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
        if (xTaskCreate(http_test_task, "http_test", 8192, NULL, 5, NULL) != pdPASS) {
            printf("HTTP test task creation failed\n");
        }
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
                        sensor_event_t event = {
                            .sensor_id = sensor_id,
                            .boot_id = boot_id,
                            .event_id = event_id,
                            .motion = data[6],
                            .button = data[7],
                            .rssi = param->scan_rst.rssi,
                            .version = data[2],
                        };

                        if (xQueueSend(event_queue, &event, 0) == pdPASS) {
                            seen[sensor_id] = true;
                            last_event[sensor_id] = event_id;
                            last_boot[sensor_id] = boot_id;
                            printf("new event queued\n");
                        } else {
                            printf("event queue full\n");
                        }
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
    event_queue = xQueueCreate(16, sizeof(sensor_event_t));
    if (event_queue == NULL) {
        printf("Failed to create event queue\n");
        abort();
    }

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(event_store_init());
    printf("Event storage initialized\n");

    event_store_mutex = xSemaphoreCreateMutex();
    if (event_store_mutex == NULL) {
        printf("Failed to create event storage mutex\n");
        abort();
    }

    if (xTaskCreate(console_task, "console", 8192, NULL, 5, NULL) != pdPASS) {
        printf("Failed to create console task\n");
        abort();
    }

    wifi_init();

    if (xTaskCreate(event_store_task, "event_store",
                4096, NULL, 5, NULL) != pdPASS) {
        printf("Failed to create event storage task\n");
        abort();
    }

    if (xTaskCreate(event_upload_task, "event_upload",
            8192, NULL, 5, NULL) != pdPASS) {
        printf("Failed to create event upload task\n");
        abort();
    }

    if (xTaskCreate(conflict_check_task, "conflict_check",
                8192, NULL, 5, NULL) != pdPASS) {
        printf("Failed to create conflict check task\n");
        abort();
    }

    esp_bt_controller_config_t bt_config =
        BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_config));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_callback));
    ESP_ERROR_CHECK(esp_ble_gap_set_scan_params(&scan_params));
}