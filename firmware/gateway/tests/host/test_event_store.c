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

int main(void)
{
    test_conflict_and_replay();
    test_local_conflict_full();

    puts("All host tests passed");
    return 0;
}