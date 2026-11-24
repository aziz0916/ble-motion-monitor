#include "event_store.h"

#include "nvs.h"
#include "nvs_flash.h"
#include <stdio.h>

#define EVENT_STORE_CAPACITY 128
#define LOCAL_CONFLICT_CAPACITY 128

typedef struct {
    uint64_t sequence;
    sensor_event_t event;
} stored_event_t;

typedef struct {
    sensor_event_t original;
    sensor_event_t incoming;
} local_conflict_t;

static nvs_handle_t event_store_handle;
static esp_err_t event_slot_is_conflict(
    unsigned int slot,
    const stored_event_t *stored,
    bool *is_conflict);

static void event_slot_key(unsigned int slot, char *key, size_t key_size)
{
    snprintf(key, key_size, "event_%03u", slot);
}

static void conflict_slot_key(
    unsigned int slot, char *key, size_t key_size)
{
    snprintf(key, key_size, "conf_%03u", slot);
}

static void local_conflict_slot_key(
    unsigned int slot, char *key, size_t key_size)
{
    snprintf(key, key_size, "local_%03u", slot);
}

esp_err_t event_store_get_local_conflict(
    unsigned int slot, sensor_event_t *original, sensor_event_t *incoming)
{
    if (slot >= LOCAL_CONFLICT_CAPACITY ||
        original == NULL || incoming == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char key[16];
    local_conflict_t conflict;
    size_t length = sizeof(conflict);

    local_conflict_slot_key(slot, key, sizeof(key));

    esp_err_t err = nvs_get_blob(
        event_store_handle, key, &conflict, &length
    );

    if (err != ESP_OK) {
        return err;
    }

    if (length != sizeof(conflict)) {
        return ESP_ERR_INVALID_SIZE;
    }

    *original = conflict.original;
    *incoming = conflict.incoming;
    return ESP_OK;
}

esp_err_t event_store_list_local_conflicts(void)
{
    unsigned int count = 0;

    for (unsigned int slot = 0; slot < LOCAL_CONFLICT_CAPACITY; slot++) {
        char key[16];
        local_conflict_t conflict;
        size_t length = sizeof(conflict);

        local_conflict_slot_key(slot, key, sizeof(key));

        esp_err_t err = nvs_get_blob(
            event_store_handle, key, &conflict, &length
        );

        if (err == ESP_ERR_NVS_NOT_FOUND) {
            continue;
        }

        if (err != ESP_OK || length != sizeof(conflict)) {
            return err != ESP_OK ? err : ESP_ERR_INVALID_SIZE;
        }

        count++;
        printf("Local conflict: slot=%u, sensor=%u, boot=%lu, event=%lu, "
               "original=%u/%u/v%u, incoming=%u/%u/v%u\n",
               slot,
               (unsigned int)conflict.original.sensor_id,
               (unsigned long)conflict.original.boot_id,
               (unsigned long)conflict.original.event_id,
               (unsigned int)conflict.original.motion,
               (unsigned int)conflict.original.button,
               (unsigned int)conflict.original.version,
               (unsigned int)conflict.incoming.motion,
               (unsigned int)conflict.incoming.button,
               (unsigned int)conflict.incoming.version);
    }

    printf("Local conflict storage: %u record(s)\n", count);
    return ESP_OK;
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

        bool is_conflict = false;

        err = event_slot_is_conflict(slot, &stored, &is_conflict);
        if (err != ESP_OK) {
            nvs_close(event_store_handle);
            return err;
        }

        if (is_conflict) {
            printf("Quarantined event: sensor=%u, boot=%lu, event=%lu, "
            "motion=%u, button=%u, version=%u, rssi=%d\n",
            (unsigned int)stored.event.sensor_id,
            (unsigned long)stored.event.boot_id,
            (unsigned long)stored.event.event_id,
            (unsigned int)stored.event.motion,
            (unsigned int)stored.event.button,
            (unsigned int)stored.event.version,
            stored.event.rssi);
        }
    }

    err = event_store_list_local_conflicts();
    if (err != ESP_OK) {
        nvs_close(event_store_handle);
        return err;
    }

    printf("Event storage: %u saved event(s)\n", count);
    return ESP_OK;
}

static esp_err_t event_slot_is_conflict(
    unsigned int slot,
    const stored_event_t *stored,
    bool *is_conflict)
{
    char key[16];
    stored_event_t marked;
    size_t length = sizeof(marked);

    *is_conflict = false;
    conflict_slot_key(slot, key, sizeof(key));

    esp_err_t err = nvs_get_blob(
        event_store_handle, key, &marked, &length
    );

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }

    if (err != ESP_OK) {
        return err;
    }

    if (length != sizeof(marked)) {
        return ESP_ERR_INVALID_SIZE;
    }

    *is_conflict =
        marked.sequence == stored->sequence &&
        marked.event.sensor_id == stored->event.sensor_id &&
        marked.event.boot_id == stored->event.boot_id &&
        marked.event.event_id == stored->event.event_id &&
        marked.event.motion == stored->event.motion &&
        marked.event.button == stored->event.button &&
        marked.event.version == stored->event.version;

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

        bool is_conflict = false;

        err = event_slot_is_conflict(slot, &stored, &is_conflict);
        if (err != ESP_OK) {
            return err;
        }

        if (is_conflict) {
            continue;
        }

        if (!*found || stored.sequence < oldest_sequence) {
            oldest_sequence = stored.sequence;
            *event = stored.event;
            *found = true;
        }
    }

    return ESP_OK;
}

static bool same_event_content(const sensor_event_t *a,
                               const sensor_event_t *b)
{
    return a->sensor_id == b->sensor_id &&
           a->boot_id == b->boot_id &&
           a->event_id == b->event_id &&
           a->motion == b->motion &&
           a->button == b->button &&
           a->version == b->version;
}

esp_err_t event_store_remove_local_conflict(
    unsigned int slot,
    const sensor_event_t *original,
    const sensor_event_t *incoming)
{
    if (slot >= LOCAL_CONFLICT_CAPACITY ||
        original == NULL || incoming == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    sensor_event_t saved_original;
    sensor_event_t saved_incoming;

    esp_err_t err = event_store_get_local_conflict(
        slot, &saved_original, &saved_incoming);
    if (err != ESP_OK) {
        return err;
    }

    if (!same_event_content(&saved_original, original) ||
        !same_event_content(&saved_incoming, incoming) ||
        saved_original.rssi != original->rssi ||
        saved_incoming.rssi != incoming->rssi) {
        return ESP_ERR_INVALID_STATE;
    }

    char key[16];
    local_conflict_slot_key(slot, key, sizeof(key));

    err = nvs_erase_key(event_store_handle, key);
    if (err != ESP_OK) {
        return err;
    }

    return nvs_commit(event_store_handle);
}

static esp_err_t local_conflict_contains(const sensor_event_t *event,
                                         bool *found)
{
    *found = false;

    for (unsigned int slot = 0; slot < LOCAL_CONFLICT_CAPACITY; slot++) {
        char key[16];
        local_conflict_t saved;
        size_t length = sizeof(saved);

        local_conflict_slot_key(slot, key, sizeof(key));
        esp_err_t err = nvs_get_blob(
            event_store_handle, key, &saved, &length);

        if (err == ESP_ERR_NVS_NOT_FOUND) {
            continue;
        }
        if (err != ESP_OK) {
            return err;
        }
        if (length != sizeof(saved)) {
            return ESP_ERR_INVALID_SIZE;
        }

        if (same_event_content(&saved.incoming, event)) {
            *found = true;
            return ESP_OK;
        }
    }

    return ESP_OK;
}

static esp_err_t local_conflict_find_original(
    const sensor_event_t *event,
    sensor_event_t *original,
    bool *found)
{
    *found = false;

    for (unsigned int slot = 0; slot < LOCAL_CONFLICT_CAPACITY; slot++) {
        char key[16];
        local_conflict_t saved;
        size_t length = sizeof(saved);

        local_conflict_slot_key(slot, key, sizeof(key));
        esp_err_t err = nvs_get_blob(
            event_store_handle, key, &saved, &length);

        if (err == ESP_ERR_NVS_NOT_FOUND) {
            continue;
        }
        if (err != ESP_OK) {
            return err;
        }
        if (length != sizeof(saved)) {
            return ESP_ERR_INVALID_SIZE;
        }

        if (saved.original.sensor_id == event->sensor_id &&
            saved.original.boot_id == event->boot_id &&
            saved.original.event_id == event->event_id) {
            *original = saved.original;
            *found = true;
            return ESP_OK;
        }
    }

    return ESP_OK;
}

static esp_err_t save_local_conflict(const sensor_event_t *original,
                                     const sensor_event_t *incoming)
{
    int free_slot = -1;

    for (unsigned int slot = 0; slot < LOCAL_CONFLICT_CAPACITY; slot++) {
        char key[16];
        local_conflict_t saved;
        size_t length = sizeof(saved);

        local_conflict_slot_key(slot, key, sizeof(key));
        esp_err_t err = nvs_get_blob(
            event_store_handle, key, &saved, &length);

        if (err == ESP_ERR_NVS_NOT_FOUND) {
            if (free_slot < 0) {
                free_slot = (int)slot;
            }
            continue;
        }

        if (err != ESP_OK) {
            return err;
        }

        if (length != sizeof(saved)) {
            return ESP_ERR_INVALID_SIZE;
        }

        if (same_event_content(&saved.original, original) &&
            same_event_content(&saved.incoming, incoming)) {
            return nvs_commit(event_store_handle);
        }
    }

    if (free_slot < 0) {
        printf("Local conflict storage full; event retained for retry\n");
        return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    }

    local_conflict_t conflict = {
        .original = *original,
        .incoming = *incoming,
    };

    char key[16];
    local_conflict_slot_key((unsigned int)free_slot, key, sizeof(key));

    esp_err_t err = nvs_set_blob(
        event_store_handle, key, &conflict, sizeof(conflict));
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_commit(event_store_handle);
    if (err == ESP_OK) {
        printf("Local conflict saved: sensor=%u, boot=%lu, event=%lu\n",
            (unsigned int)incoming->sensor_id,
            (unsigned long)incoming->boot_id,
            (unsigned long)incoming->event_id);
    }
    return err;
}

esp_err_t event_store_add(const sensor_event_t *event)
{
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    bool conflict_found = false;
    esp_err_t conflict_err = local_conflict_contains(event, &conflict_found);

    if (conflict_err != ESP_OK) {
        return conflict_err;
    }

    if (conflict_found) {
        conflict_err = nvs_commit(event_store_handle);
        return conflict_err == ESP_OK ? ESP_ERR_INVALID_STATE : conflict_err;
    }

    sensor_event_t conflict_original;
    bool original_found = false;

    conflict_err = local_conflict_find_original(
        event, &conflict_original, &original_found);
    if (conflict_err != ESP_OK) {
        return conflict_err;
    }

    if (original_found && !same_event_content(&conflict_original, event)) {
        conflict_err = save_local_conflict(&conflict_original, event);
        return conflict_err == ESP_OK ? ESP_ERR_INVALID_STATE : conflict_err;
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

            printf("Local event conflict: sensor=%u, boot=%lu, event=%lu\n",
                (unsigned int)event->sensor_id,
                (unsigned long)event->boot_id,
                (unsigned long)event->event_id);
            err = save_local_conflict(&stored.event, event);
            return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;
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

esp_err_t event_store_mark_conflict(const sensor_event_t *event)
{
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (unsigned int slot = 0; slot < EVENT_STORE_CAPACITY; slot++) {
        char event_key[16];
        stored_event_t stored;
        size_t length = sizeof(stored);

        event_slot_key(slot, event_key, sizeof(event_key));

        esp_err_t err = nvs_get_blob(
            event_store_handle, event_key, &stored, &length
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
            stored.event.event_id == event->event_id &&
            stored.event.motion == event->motion &&
            stored.event.button == event->button &&
            stored.event.version == event->version) {

            char conflict_key[16];
            conflict_slot_key(slot, conflict_key, sizeof(conflict_key));

            err = nvs_set_blob(
                event_store_handle,
                conflict_key,
                &stored,
                sizeof(stored)
            );
            if (err != ESP_OK) {
                return err;
            }

            return nvs_commit(event_store_handle);
        }
    }

    return ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t event_store_list_quarantined(void)
{
    unsigned int count = 0;

    for (unsigned int slot = 0; slot < EVENT_STORE_CAPACITY; slot++) {
        char key[16];
        stored_event_t stored;
        size_t length = sizeof(stored);

        event_slot_key(slot, key, sizeof(key));

        esp_err_t err = nvs_get_blob(
            event_store_handle, key, &stored, &length);

        if (err == ESP_ERR_NVS_NOT_FOUND) {
            continue;
        }
        if (err != ESP_OK) {
            return err;
        }
        if (length != sizeof(stored)) {
            return ESP_ERR_INVALID_SIZE;
        }

        bool is_conflict = false;
        err = event_slot_is_conflict(slot, &stored, &is_conflict);
        if (err != ESP_OK) {
            return err;
        }
        if (!is_conflict) {
            continue;
        }

        count++;
        printf("Quarantined: slot=%u, sensor=%u, boot=%lu, event=%lu, "
               "motion=%u, button=%u, version=%u, rssi=%d\n",
               slot,
               (unsigned int)stored.event.sensor_id,
               (unsigned long)stored.event.boot_id,
               (unsigned long)stored.event.event_id,
               (unsigned int)stored.event.motion,
               (unsigned int)stored.event.button,
               (unsigned int)stored.event.version,
               stored.event.rssi);
    }

    printf("Quarantined events: %u record(s)\n", count);
    return ESP_OK;
}

esp_err_t event_store_peek_conflict(sensor_event_t *event, bool *found)
{
    if (event == NULL || found == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    static unsigned int next_slot = 0;
    *found = false;

    for (unsigned int offset = 0; offset < EVENT_STORE_CAPACITY; offset++) {
        unsigned int slot = (next_slot + offset) % EVENT_STORE_CAPACITY;
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

        bool is_conflict = false;
        err = event_slot_is_conflict(slot, &stored, &is_conflict);
        if (err != ESP_OK) {
            return err;
        }

        if (is_conflict) {
            *event = stored.event;
            *found = true;
            next_slot = (slot + 1) % EVENT_STORE_CAPACITY;
            return ESP_OK;
        }
    }

    return ESP_OK;
}

esp_err_t event_store_release_conflict(const sensor_event_t *event)
{
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (unsigned int slot = 0; slot < EVENT_STORE_CAPACITY; slot++) {
        char event_key[16];
        stored_event_t stored;
        size_t length = sizeof(stored);

        event_slot_key(slot, event_key, sizeof(event_key));

        esp_err_t err = nvs_get_blob(
            event_store_handle, event_key, &stored, &length
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
            stored.event.event_id == event->event_id &&
            stored.event.motion == event->motion &&
            stored.event.button == event->button &&
            stored.event.version == event->version) {

            bool is_conflict = false;
            err = event_slot_is_conflict(slot, &stored, &is_conflict);
            if (err != ESP_OK) {
                return err;
            }

            if (!is_conflict) {
                return ESP_ERR_INVALID_STATE;
            }

            char conflict_key[16];
            conflict_slot_key(slot, conflict_key, sizeof(conflict_key));

            err = nvs_erase_key(event_store_handle, conflict_key);
            if (err != ESP_OK) {
                return err;
            }

            return nvs_commit(event_store_handle);
        }
    }

    return ESP_ERR_NVS_NOT_FOUND;
}