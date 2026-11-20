#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "nvs.h"

/*
 * 將實際儲存程式編入測試，讓測試也能檢查本機衝突紀錄。
 * 測試使用 mock_nvs.c，不會存取 ESP32。
 */
#include "../../main/event_store.c"

void mock_nvs_reset(void);

static sensor_event_t make_event(uint32_t id, uint8_t motion)
{
    sensor_event_t event = {
        .sensor_id = 1,
        .boot_id = 100,
        .event_id = id,
        .motion = motion,
        .button = 0,
        .rssi = -50,
        .version = 3,
    };
    return event;
}

static local_conflict_t read_local(unsigned int slot)
{
    char key[16];
    local_conflict_t saved;
    size_t length = sizeof(saved);

    local_conflict_slot_key(slot, key, sizeof(key));
    assert(nvs_get_blob(event_store_handle, key, &saved, &length) == ESP_OK);
    assert(length == sizeof(saved));

    return saved;
}

static void test_conflict_and_replay(void)
{
    mock_nvs_reset();
    assert(event_store_init() == ESP_OK);

    sensor_event_t original = make_event(1, 1);
    sensor_event_t incoming = make_event(1, 0);
    sensor_event_t later = make_event(2, 1);

    assert(event_store_add(&original) == ESP_OK);
    assert(event_store_add(&incoming) == ESP_ERR_INVALID_STATE);

    local_conflict_t first = read_local(0);
    assert(first.original.motion == 1);
    assert(first.incoming.motion == 0);

    /* 重複接收相同衝突，不應新增第二筆。 */
    assert(event_store_add(&incoming) == ESP_ERR_INVALID_STATE);

    char key[16];
    local_conflict_slot_key(1, key, sizeof(key));
    size_t length = 0;
    assert(nvs_get_blob(event_store_handle, key, NULL, &length) ==
           ESP_ERR_NVS_NOT_FOUND);

    /* 原事件移除後，相同衝突仍不應進入一般待上傳區。 */
    assert(event_store_remove(&original) == ESP_OK);
    assert(event_store_add(&incoming) == ESP_ERR_INVALID_STATE);

    bool found = false;
    sensor_event_t peeked;
    assert(event_store_peek(&peeked, &found) == ESP_OK);
    assert(!found);

    /* 相同識別碼的第三種內容也應另存。 */
    sensor_event_t third = incoming;
    third.button = 1;
    assert(event_store_add(&third) == ESP_ERR_INVALID_STATE);

    local_conflict_t second = read_local(1);
    assert(second.original.motion == 1);
    assert(second.incoming.motion == 0);
    assert(second.incoming.button == 1);

    /* 後續一般事件仍可進入待上傳區。 */
    assert(event_store_add(&later) == ESP_OK);
    assert(event_store_peek(&peeked, &found) == ESP_OK);
    assert(found && peeked.event_id == 2);

    /* 重新初始化模擬儲存區，資料仍應可讀取。 */
    assert(event_store_init() == ESP_OK);
    first = read_local(0);
    second = read_local(1);
    assert(first.incoming.motion == 0);
    assert(second.incoming.button == 1);

    puts("PASS: conflict, replay and later event");
}

static void test_local_conflict_full(void)
{
    mock_nvs_reset();
    assert(event_store_init() == ESP_OK);

    sensor_event_t original = make_event(10, 0);
    assert(event_store_add(&original) == ESP_OK);

    for (unsigned int i = 1; i <= LOCAL_CONFLICT_CAPACITY; i++) {
        sensor_event_t incoming = original;
        incoming.motion = (uint8_t)i;

        assert(event_store_add(&incoming) == ESP_ERR_INVALID_STATE);
    }

    local_conflict_t first = read_local(0);
    assert(first.incoming.motion == 1);

    sensor_event_t extra = original;
    extra.motion = 129;
    assert(event_store_add(&extra) == ESP_ERR_NVS_NOT_ENOUGH_SPACE);

    /* 滿載時不能覆寫第一筆衝突，也不能刪除原事件。 */
    first = read_local(0);
    assert(first.incoming.motion == 1);

    bool found = false;
    sensor_event_t peeked;
    assert(event_store_peek(&peeked, &found) == ESP_OK);
    assert(found && peeked.event_id == original.event_id);
    assert(peeked.motion == original.motion);

    puts("PASS: local conflict storage full");
}

static void test_get_local_conflict(void)
{
    mock_nvs_reset();
    assert(event_store_init() == ESP_OK);

    sensor_event_t original = make_event(1, 1);
    sensor_event_t incoming = make_event(1, 0);

    assert(event_store_add(&original) == ESP_OK);
    assert(event_store_add(&incoming) == ESP_ERR_INVALID_STATE);

    sensor_event_t saved_original;
    sensor_event_t saved_incoming;

    assert(event_store_get_local_conflict(
               0, &saved_original, &saved_incoming) == ESP_OK);
    assert(saved_original.event_id == 1);
    assert(saved_original.motion == 1);
    assert(saved_incoming.event_id == 1);
    assert(saved_incoming.motion == 0);

    assert(event_store_get_local_conflict(
               1, &saved_original, &saved_incoming) ==
           ESP_ERR_NVS_NOT_FOUND);
    assert(event_store_get_local_conflict(
               LOCAL_CONFLICT_CAPACITY, &saved_original, &saved_incoming) ==
           ESP_ERR_INVALID_ARG);
    assert(event_store_get_local_conflict(
               0, NULL, &saved_incoming) == ESP_ERR_INVALID_ARG);
    assert(event_store_get_local_conflict(
               0, &saved_original, NULL) == ESP_ERR_INVALID_ARG);

    puts("PASS: get local conflict");
}

static void test_remove_local_conflict(void)
{
    mock_nvs_reset();
    assert(event_store_init() == ESP_OK);

    sensor_event_t original = make_event(1, 1);
    sensor_event_t incoming = make_event(1, 0);
    sensor_event_t other = incoming;
    other.button = 1;

    assert(event_store_add(&original) == ESP_OK);
    assert(event_store_add(&incoming) == ESP_ERR_INVALID_STATE);
    assert(event_store_add(&other) == ESP_ERR_INVALID_STATE);

    sensor_event_t saved_original;
    sensor_event_t saved_incoming;
    assert(event_store_get_local_conflict(
               0, &saved_original, &saved_incoming) == ESP_OK);

    /* 內容不符時不得刪除。 */
    sensor_event_t mismatch = saved_incoming;
    mismatch.motion = 9;
    assert(event_store_remove_local_conflict(
               0, &saved_original, &mismatch) == ESP_ERR_INVALID_STATE);
    assert(event_store_get_local_conflict(
               0, &saved_original, &saved_incoming) == ESP_OK);

    /* 清除指定紀錄，其他紀錄與待上傳事件必須保留。 */
    assert(event_store_remove_local_conflict(
               0, &saved_original, &saved_incoming) == ESP_OK);
    assert(event_store_get_local_conflict(
               0, &saved_original, &saved_incoming) ==
           ESP_ERR_NVS_NOT_FOUND);

    local_conflict_t remaining = read_local(1);
    assert(remaining.incoming.button == 1);

    bool found = false;
    sensor_event_t peeked;
    assert(event_store_peek(&peeked, &found) == ESP_OK);
    assert(found && peeked.event_id == 1 && peeked.motion == 1);

    puts("PASS: remove local conflict");
}

int main(void)
{
    test_conflict_and_replay();
    test_local_conflict_full();
    test_get_local_conflict();
    test_remove_local_conflict();

    puts("All host tests passed");
    return 0;
}