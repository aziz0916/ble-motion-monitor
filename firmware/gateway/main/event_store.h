#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint8_t sensor_id;
    uint32_t boot_id;
    uint32_t event_id;
    uint8_t motion;
    uint8_t button;
    int rssi;
    uint8_t version;
} sensor_event_t;

esp_err_t event_store_init(void);
esp_err_t event_store_add(const sensor_event_t *event);
esp_err_t event_store_peek(sensor_event_t *event, bool *found);
esp_err_t event_store_remove(const sensor_event_t *event);
esp_err_t event_store_mark_conflict(const sensor_event_t *event);
esp_err_t event_store_peek_conflict(sensor_event_t *event, bool *found);
esp_err_t event_store_release_conflict(const sensor_event_t *event);
esp_err_t event_store_list_local_conflicts(void);
esp_err_t event_store_get_local_conflict(
    unsigned int slot, sensor_event_t *original, sensor_event_t *incoming);
esp_err_t event_store_remove_local_conflict(
    unsigned int slot,
    const sensor_event_t *original,
    const sensor_event_t *incoming);
esp_err_t event_store_list_quarantined(void);
esp_err_t event_store_get_quarantined(
    unsigned int slot, sensor_event_t *event);