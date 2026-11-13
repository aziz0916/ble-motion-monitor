# 感測器事件資料表

在 NocoBase 的 Main 資料來源建立 General collection：

- 顯示名稱：感測器事件
- Collection name：`sensor_events`
- 保留預設欄位：ID、Created at、Created by、Last updated at、Last updated by

新增下列欄位，Field interface 均選擇 Integer：

| 顯示名稱 | Field name | 用途 |
| --- | --- | --- |
| 感測器編號 | `sensor_id` | 識別感測器 |
| 啟動編號 | `boot_id` | 識別感測器啟動次數 |
| 事件編號 | `event_id` | 識別本次啟動期間的事件 |
| 活動狀態 | `motion` | 模擬活動狀態 |
| 按鈕狀態 | `button` | 模擬按鈕狀態 |
| 訊號強度 | `rssi` | 網關接收到的 RSSI，單位 dBm |
| 封包版本 | `version` | BLE 封包格式版本 |

這些欄位的 Primary、Unique 和 Auto increment 均不勾選，
預設值留空。

建立 collection 與欄位後，執行 `sensor_events_constraints.sql`：
三個識別欄位設為資料庫必填，並對其組合建立唯一索引，
避免同一組事件識別資料重複寫入。