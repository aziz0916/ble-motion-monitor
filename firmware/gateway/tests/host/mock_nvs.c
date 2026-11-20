#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "nvs.h"
#include "nvs_flash.h"

#define MOCK_MAX_ENTRIES 512
#define MOCK_MAX_VALUE_SIZE 256

typedef struct {
    bool used;
    char key[16];
    unsigned char value[MOCK_MAX_VALUE_SIZE];
    size_t length;
} mock_entry_t;

static mock_entry_t entries[MOCK_MAX_ENTRIES];

void mock_nvs_reset(void)
{
    memset(entries, 0, sizeof(entries));
}

static int find_entry(const char *key)
{
    for (int i = 0; i < MOCK_MAX_ENTRIES; i++) {
        if (entries[i].used && strcmp(entries[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

esp_err_t nvs_flash_init_partition(const char *partition_name)
{
    (void)partition_name;
    return ESP_OK;
}

esp_err_t nvs_open_from_partition(
    const char *partition_name,
    const char *namespace_name,
    int open_mode,
    nvs_handle_t *out_handle)
{
    (void)partition_name;
    (void)namespace_name;
    (void)open_mode;
    *out_handle = 1;
    return ESP_OK;
}

esp_err_t nvs_get_blob(
    nvs_handle_t handle,
    const char *key,
    void *out_value,
    size_t *length)
{
    (void)handle;

    int index = find_entry(key);
    if (index < 0) {
        return ESP_ERR_NVS_NOT_FOUND;
    }

    size_t required = entries[index].length;
    if (out_value == NULL) {
        *length = required;
        return ESP_OK;
    }

    if (*length < required) {
        *length = required;
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(out_value, entries[index].value, required);
    *length = required;
    return ESP_OK;
}

esp_err_t nvs_set_blob(
    nvs_handle_t handle,
    const char *key,
    const void *value,
    size_t length)
{
    (void)handle;

    if (strlen(key) >= sizeof(entries[0].key) ||
        length > MOCK_MAX_VALUE_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    int index = find_entry(key);
    if (index < 0) {
        for (int i = 0; i < MOCK_MAX_ENTRIES; i++) {
            if (!entries[i].used) {
                index = i;
                break;
            }
        }
    }

    if (index < 0) {
        return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    }

    entries[index].used = true;
    snprintf(entries[index].key, sizeof(entries[index].key), "%s", key);
    memcpy(entries[index].value, value, length);
    entries[index].length = length;
    return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    (void)handle;

    int index = find_entry(key);
    if (index < 0) {
        return ESP_ERR_NVS_NOT_FOUND;
    }

    memset(&entries[index], 0, sizeof(entries[index]));
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    (void)handle;
}