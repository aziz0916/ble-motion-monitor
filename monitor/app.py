import json
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen

API_KEY = os.environ.get("NOCOBASE_API_KEY", "")
BASE_URL = os.environ.get("NOCOBASE_URL", "http://nocobase:80").rstrip("/")

HTML = """<!doctype html>
<html lang="zh-Hant">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>BLE 感測器監控</title>
<style>
  body { font-family: system-ui, sans-serif; margin: 0; background: #f3f6fa; color: #172338; }
  main { max-width: 1100px; margin: auto; padding: 24px; }
  h1 { margin-bottom: 8px; }
  .muted { color: #5d6b7f; }
  .card { background: white; border-radius: 12px; padding: 20px; margin-top: 20px;
          box-shadow: 0 2px 12px #17233812; }
  .status { font-weight: 600; }
  .error { color: #b42318; }
  .table-wrap { overflow-x: auto; }
  table { width: 100%; border-collapse: collapse; white-space: nowrap; }
  th, td { text-align: left; padding: 12px; border-bottom: 1px solid #e8edf3; }
  th { color: #536278; }
</style>
</head>
<body>
<main>
  <h1>BLE 感測器監控</h1>
  <p class="muted">每 5 秒更新一次；時間為資料寫入系統的時間。</p>
  <p id="status" class="status">正在載入…</p>

  <section class="card">
    <h2>最近按鈕事件</h2>
    <p id="latest">載入中…</p>
    <p class="muted">此處顯示最近 20 筆紀錄中的按鈕事件，並非目前按鈕或設備在線狀態。</p>
  </section>

  <section class="card">
    <h2>最近事件</h2>
    <div class="table-wrap">
      <table>
        <thead><tr>
          <th>建立時間</th><th>感測器</th><th>啟動編號</th><th>事件編號</th>
          <th>活動</th><th>按鈕</th><th>訊號強度</th>
        </tr></thead>
        <tbody id="events"></tbody>
      </table>
    </div>
  </section>
</main>
<script>
const statusEl = document.getElementById('status');
const latestEl = document.getElementById('latest');
const eventsEl = document.getElementById('events');

function timeText(value) {
  if (!value) return '—';
  const date = new Date(value);
  return Number.isNaN(date.getTime()) ? String(value) :
    date.toLocaleString('zh-TW', { timeZone: 'Asia/Taipei' });
}

async function refresh() {
  try {
    const response = await fetch('/api/events', { cache: 'no-store' });
    if (!response.ok) throw new Error('資料讀取失敗');
    const records = await response.json();
    if (!Array.isArray(records)) throw new Error('資料格式不正確');

    eventsEl.replaceChildren();
    for (const event of records) {
      const row = document.createElement('tr');
      const values = [
        timeText(event.createdAt),
        event.sensor_id, event.boot_id, event.event_id,
        event.motion === 1 ? '活動' : event.motion === 0 ? '無活動' : '未知',
        event.button === 1 ? '按下' : event.button === 0 ? '未按下' : '未知',
        event.rssi == null ? '—' : `${event.rssi} dBm`
      ];
      for (const value of values) {
        const cell = document.createElement('td');
        cell.textContent = value ?? '—';
        row.appendChild(cell);
      }
      eventsEl.appendChild(row);
    }

    const latest = records.find(event => event.button === 1);
    latestEl.textContent = latest
      ? `感測器 ${latest.sensor_id}・啟動 ${latest.boot_id}・事件 ${latest.event_id}・${timeText(latest.createdAt)}`
      : '最近 20 筆中沒有按鈕事件';
    statusEl.className = 'status';
    statusEl.textContent = `更新成功：${new Date().toLocaleTimeString('zh-TW')}`;
  } catch (error) {
    statusEl.className = 'status error';
    statusEl.textContent = '更新失敗，請確認監控服務與 NocoBase 的連線。';
  } finally {
    setTimeout(refresh, 5000);
  }
}
refresh();
</script>
</body>
</html>
"""

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/":
            self.respond(200, HTML.encode(), "text/html; charset=utf-8")
            return

        if self.path != "/api/events":
            self.send_error(404)
            return

        if not API_KEY:
            self.respond(503, b'{"error":"API key not configured"}')
            return

        params = urlencode({
            "pageSize": 20,
            "sort": "-createdAt",
            "fields": "sensor_id,boot_id,event_id,motion,button,rssi,version,createdAt",
        })
        request = Request(
            f"{BASE_URL}/api/sensor_events:list?{params}",
            headers={"Authorization": f"Bearer {API_KEY}"},
        )
        try:
            with urlopen(request, timeout=10) as response:
                result = json.load(response)
            data = result.get("data")
            if not isinstance(data, list):
                raise ValueError("Invalid response")
            self.respond(200, json.dumps(data).encode())
        except (HTTPError, URLError, TimeoutError, ValueError, OSError):
            self.respond(502, b'{"error":"Unable to load events"}')

    def respond(self, status, body, content_type="application/json"):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        pass

ThreadingHTTPServer(("0.0.0.0", 8080), Handler).serve_forever()
