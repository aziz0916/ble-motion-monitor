| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C5 | ESP32-C6 | ESP32-C61 | ESP32-H2 | ESP32-P4 | ESP32-S2 | ESP32-S3 | Linux |
| ----------------- | ----- | -------- | -------- | -------- | -------- | --------- | -------- | -------- | -------- | -------- | ----- |

# Hello World Example

Starts a FreeRTOS task to print "Hello World".

(See the README.md file in the upper level 'examples' directory for more information about examples.)

## How to use example

Follow detailed instructions provided specifically for this example.

Select the instructions depending on Espressif chip installed on your development board:

- [ESP32 Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/stable/get-started/index.html)
- [ESP32-S2 Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s2/get-started/index.html)


## Example folder contents

The project **hello_world** contains one source file in C language [hello_world_main.c](main/hello_world_main.c). The file is located in folder [main](main).

ESP-IDF projects are built using CMake. The project build configuration is contained in `CMakeLists.txt` files that provide set of directives and instructions describing the project's source files and targets (executable, library, or both).

Below is short explanation of remaining files in the project folder.

```
├── CMakeLists.txt
├── pytest_hello_world.py      Python script used for automated testing
├── main
│   ├── CMakeLists.txt
│   └── hello_world_main.c
└── README.md                  This is the file you are currently reading
```

For more information on structure and contents of ESP-IDF projects, please refer to Section [Build System](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/build-system.html) of the ESP-IDF Programming Guide.

## Troubleshooting

* Program upload failure

    * Hardware connection is not correct: run `idf.py -p PORT monitor`, and reboot your board to see if there are any output logs.
    * The baud rate for downloading is too high: lower your baud rate in the `menuconfig` menu, and try again.

## Technical support and feedback

Please use the following feedback channels:

* For technical queries, go to the [esp32.com](https://esp32.com/) forum
* For a feature request or bug report, create a [GitHub issue](https://github.com/espressif/esp-idf/issues)

We will get back to you as soon as possible.

## 網關離線事件保存

- 網關使用獨立的 `events` NVS 分割區，最多保存 128 筆待上傳事件。
- 事件成功寫入 NVS 後，網關重新啟動仍會恢復並重試上傳。
- 上傳成功，或後端確認已有內容一致的相同事件後，網關才會移除本機副本。
- 若後端已有識別碼相同但內容不一致的事件，網關會將本機事件標記為隔離並保留，讓後續事件繼續上傳。隔離狀態在網關重啟後仍會保留。
- 網關每 60 秒最多複查一筆隔離事件；若後端內容已與本機事件一致，網關會解除隔離，並在後續確認事件已存在後移除本機副本。查詢失敗或後端仍有衝突時，事件會繼續保留。
- 儲存區滿載時，新事件暫時等待在最多 16 筆的 RAM 佇列；在寫入 NVS 前重啟可能遺失。佇列滿後也可能無法接收更多事件。
- 感測器使用 BLE 廣播；網關未收到的事件不在離線保存範圍內。
- 此版本使用 NVS 進行功能驗證。長期頻繁寫入時，仍需評估 Flash 壽命與可靠度。
- 若新收到的事件與 NVS 中的待上傳事件識別碼相同、內容不同，網關會將原事件與新事件另存為本機衝突紀錄，成功保存後繼續處理 RAM 佇列中的後續事件。
- 本機衝突紀錄最多設定 128 筆，與待上傳事件共用 `events` NVS 分割區；實際可用空間也會受其他紀錄及 NVS 使用情況影響。網關啟動時會列出本機衝突紀錄；執行期間可在 `idf.py monitor` 輸入 `conflicts` 並按 Enter 查看清單。
- 清理本機衝突時，先輸入 `inspect <slot>` 查看指定紀錄的完整內容，再輸入 `confirm-remove` 確認清理。清理前會再次核對紀錄內容；執行 `conflicts`、輸入未知指令或過長指令會取消待確認的清理操作。
- 清理後，網關不再保留該筆衝突紀錄，可能影響之後收到相同事件時的去重判斷。清理本機衝突紀錄不會直接刪除待上傳事件或後端衝突的隔離標記。
- 若無法保存新的本機衝突，該事件會留在 RAM 佇列重試；在寫入 NVS 前重新啟動，仍有遺失風險。
- 執行期間可在 `idf.py monitor` 輸入 `quarantined`，查看網關目前保留的後端衝突隔離事件。此指令只顯示網關上的事件內容，不會查詢或修改後端資料。
- 在 `idf.py monitor` 輸入 `check-quarantined <slot>`，可查詢指定隔離事件與後端紀錄的比對結果：一致、內容衝突、後端找不到紀錄，或無法確認。查詢前會先讀取網關上的隔離事件；此指令不會主動解除隔離或刪除事件。查詢期間事件狀態可能改變，結果僅供當次檢查參考。
