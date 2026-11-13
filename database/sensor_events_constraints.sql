-- 先在 NocoBase 建立 sensor_events collection 與相關欄位，
-- 再執行此檔案，設定事件識別欄位的資料庫限制。

ALTER TABLE sensor_events
    ALTER COLUMN sensor_id SET NOT NULL,
    ALTER COLUMN boot_id SET NOT NULL,
    ALTER COLUMN event_id SET NOT NULL;

CREATE UNIQUE INDEX IF NOT EXISTS sensor_events_identity_unique
    ON sensor_events (sensor_id, boot_id, event_id);