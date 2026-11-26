# 感測器按鈕 LINE 通知

## 功能流程

感測器按下 IO0 按鈕後，透過 BLE 廣播事件。網關接收並上傳至 NocoBase；新增事件會觸發 NocoBase 工作流程，將事件送到 n8n，由 n8n 判斷按鈕狀態並傳送 LINE 通知。

## 服務

- NocoBase：`http://localhost:13003`
- n8n：`http://localhost:5684`
- NocoBase 呼叫 n8n 時使用 Docker 內部網址：`http://n8n:5678/webhook/sensor-button-event`
- n8n 資料儲存在 Docker volume `motion_n8n_data`。

## 工作流程設定

1. NocoBase 建立非同步 Collection event 工作流程，監聽「感測器事件」的新增紀錄。
2. NocoBase HTTP request 使用 POST、JSON 和自訂認證標頭，將事件送至 n8n 正式 Webhook。
3. n8n Webhook 驗證認證標頭；IF 節點判斷 `button` 是否為 `1`。
4. 符合條件時，n8n 使用 LINE Messaging API 發送通知。

Webhook 密鑰與 LINE Channel access token 分別設定，不應提交至 Git。LINE 接收對象與憑證應依部署環境設定。

## 測試

確認 Docker 服務、網關與感測器均已啟動，且 NocoBase 工作流程已啟用、n8n 工作流程已發布。按下感測器 IO0 後，確認：

- NocoBase 新增對應的感測器事件。
- n8n Executions 顯示執行成功。
- LINE 收到通知，且感測器、啟動與事件編號正確。

## 目前限制

本功能為原型，BLE 廣播可能漏收；通知流程失敗時也尚未建立完整的可靠重送機制，不保證每筆事件都能送達。實際使用前應另行驗證可靠性。
