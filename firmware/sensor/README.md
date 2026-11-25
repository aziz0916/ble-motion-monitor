# BLE Motion Monitor：感測器韌體

此韌體執行於 ESP32 感測器板，使用板上的 `IO0` 按鈕產生事件，並透過 BLE 廣播給網關。

## 目前功能

- 感測器啟動時，從 NVS 讀取並遞增 `boot_id`。
- 板子正常啟動後，每次按下並放開 `IO0`，會產生一筆新的按鈕事件，並遞增 `event_id`。
- 按鈕事件的欄位為 `button=1`、`motion=0`、`version=3`。
- 韌體會持續廣播最近一次按鈕事件，直到下一次按下按鈕或感測器重啟。
- 網關收到重複廣播時，會依感測器編號、啟動編號及事件編號進行去重。

目前尚未接入 LIS3DH；`motion=0` 是固定值，不代表已完成動作偵測。

## 硬體與按鈕

目前實測使用 ESP32-D0WD-V3 開發板：

- `IO0`：測試按鈕事件。
- `EN`：重新啟動板子。

`IO0` 也與晶片啟動模式有關；請在板子正常啟動後再按下進行測試，避免按住 `IO0` 的同時重新啟動或上電。

## 編譯與燒錄

在 Dev Container 終端機進入感測器目錄：

```bash
cd /workspaces/ble-motion-monitor/firmware/sensor
idf.py build
```

燒錄前，請確認序列埠對應的是感測器，而非 ESP32-S3 網關。以下以 `/dev/ttyUSB0` 為範例；實際名稱可能不同：

```bash
esptool.py -p /dev/ttyUSB0 chip_id
idf.py -p /dev/ttyUSB0 flash monitor
```

感測器測試板的 MAC 為 `24:dc:c3:c3:3c:9c`。看到 `Ready. Press IO0 after startup.` 後，按一下 `IO0` 並放開，感測器應輸出按鈕事件及 BLE 廣播啟動訊息。

若使用 `idf.py monitor`，依序按 `Ctrl+T`、`Ctrl+X` 可退出。

## 已驗證與限制

已實機驗證：按下 `IO0` 後，事件可經 BLE 傳至網關，再上傳至 NocoBase；連續三次按下產生三個不同事件編號。

BLE 廣播不保證網關一定收到。感測器目前只持續廣播最近一次事件，沒有保存待確認的事件佇列；若網關未收到舊事件，就收到更新事件，舊事件可能遺失。快速連續按下、長時間斷線及斷電恢復的完整交付能力尚未驗證。