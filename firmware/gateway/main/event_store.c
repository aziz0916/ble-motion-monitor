#include "event_store.h"

#include "nvs.h"
#include "nvs_flash.h"
#include <stdio.h>

#define EVENT_STORE_CAPACITY 128

typedef struct {
    uint64_t sequence;
    sensor_event_t event;
} stored_event_t;

static nvs_handle_t event_store_handle;

static void event_slot_key(unsigned int slot, char *key, size_t key_size)
{
    snprintf(key, key_size, "event_%03u", slot);
}

esp_err_t event_store_init(void)
{
    esp_err_t err = nvs_flash_init_partition("events");
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_open_from_partition(
        "events",
        "event_queue",
        NVS_READWRITE,
        &event_store_handle
    );
    if (err != ESP_OK) {
        return err;
    }

    unsigned int count = 0;

    for (unsigned int slot = 0; slot < EVENT_STORE_CAPACITY; slot++) {
        char key[16];
        stored_event_t stored;
        size_t length = sizeof(stored);

        event_slot_key(slot, key, sizeof(key));
        err = nvs_get_blob(event_store_handle, key, &stored, &length);

        if (err == ESP_ERR_NVS_NOT_FOUND) {
            continue;
        }

        if (err != ESP_OK || length != sizeof(stored)) {
            nvs_close(event_store_handle);
            return err != ESP_OK ? err : ESP_ERR_INVALID_SIZE;
        }

        count++;
    }

    printf("Event storage: %u saved event(s)\n", count);
    return ESP_OK;
}

esp_err_t event_store_peek(sensor_event_t *event, bool *found)
{
    if (event == NULL || found == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *found = false;
    uint64_t oldest_sequence = UINT64_MAX;

    for (unsigned int slot = 0; slot < EVENT_STORE_CAPACITY; slot++) {
        char key[16];
        stored_event_t stored;
        size_t length = sizeof(stored);

        event_slot_key(slot, key, sizeof(key));

        esp_err_t err = nvs_get_blob(
            event_store_handle, key, &stored, &length
        );

        if (err == ESP_ERR_NVS_NOT_FOUND) {
            continue;
        }

        if (err != ESP_OK) {
            return err;
        }

        if (length != sizeof(stored)) {
            return ESP_ERR_INVALID_SIZE;
        }

        if (!*found || stored.sequence < oldest_sequence) {
            oldest_sequence = stored.sequence;
            *event = stored.event;
            *found = true;
        }
    }

    return ESP_OK;
}

esp_err_t event_store_add(const sensor_event_t *event)
{
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int free_slot = -1;
    uint64_t max_sequence = 0;

    for (unsigned int slot = 0; slot < EVENT_STORE_CAPACITY; slot++) {
        char key[16];
        stored_event_t stored;
        size_t length = sizeof(stored);

        event_slot_key(slot, key, sizeof(key));

        esp_err_t err = nvs_get_blob(
            event_store_handle, key, &stored, &length
        );

        if (err == ESP_ERR_NVS_NOT_FOUND) {
            if (free_slot < 0) {
                free_slot = (int)slot;
            }
            continue;
        }

        if (err != ESP_OK) {
            return err;
        }

        if (length != sizeof(stored)) {
            return ESP_ERR_INVALID_SIZE;
        }

        if (stored.event.sensor_id == event->sensor_id &&
            stored.event.boot_id == event->boot_id &&
            stored.event.event_id == event->event_id) {
            if (stored.event.motion == event->motion &&
                stored.event.button == event->button &&
                stored.event.version == event->version) {
                err = nvs_commit(event_store_handle);
                return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;
            }

            return ESP_ERR_INVALID_RESPONSE;
        }

        if (stored.sequence > max_sequence) {
            max_sequence = stored.sequence;
        }
    }

    if (free_slot < 0) {
        return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    }

    if (max_sequence == UINT64_MAX) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    stored_event_t stored = {
        .sequence = max_sequence + 1,
        .event = *event,
    };

    char key[16];
    event_slot_key((unsigned int)free_slot, key, sizeof(key));

    esp_err_t err = nvs_set_blob(
        event_store_handle, key, &stored, sizeof(stored)
    );
    if (err != ESP_OK) {
        return err;
    }

    return nvs_commit(event_store_handle);
}

esp_err_t event_store_remove(const sensor_event_t *event)
{
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (unsigned int slot = 0; slot < EVENT_STORE_CAPACITY; slot++) {
        char key[16];
        stored_event_t stored;
        size_t length = sizeof(stored);

        event_slot_key(slot, key, sizeof(key));

        esp_err_t err = nvs_get_blob(
            event_store_handle, key, &stored, &length
        );

        if (err == ESP_ERR_NVS_NOT_FOUND) {
            continue;
        }

        if (err != ESP_OK) {
            return err;
        }

        if (length != sizeof(stored)) {
            return ESP_ERR_INVALID_SIZE;
        }

        if (stored.event.sensor_id == event->sensor_id &&
            stored.event.boot_id == event->boot_id &&
            stored.event.event_id == event->event_id) {
            err = nvs_erase_key(event_store_handle, key);
            if (err != ESP_OK) {
                return err;
            }

            return nvs_commit(event_store_handle);
        }
    }

    esp_err_t err = nvs_commit(event_store_handle);
    return err == ESP_OK ? ESP_ERR_NVS_NOT_FOUND : err;
}