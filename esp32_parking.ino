#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "time.h"

const char* ssid = "Anh_Thư";
const char* password = "anhthu2021";

// Cấu hình NTP (Giờ Việt Nam GMT+7)
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7 * 3600; 
const int   daylightOffset_sec = 0;

WebServer server(80);

int inCount = 0, outCount = 0;
long totalRevenue = 0; 
String slots = "0000";

struct LogEntry { 
  String uid; 
  String action; 
  int mins; 
  long money; 
  String timeStr;
};

LogEntry logs[30]; 
int logCount = 0;

String getLocalTimeStr() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    return "--/--/-- --:--:--";
  }
  char timeStringBuff[25];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%d/%m/%Y %H:%M:%S", &timeinfo);
  return String(timeStringBuff);
}

void addLog(String u, String a, int m, long f) {
  for (int i = 29; i > 0; i--) logs[i] = logs[i-1];
  logs[0] = {u, a, m, f, getLocalTimeStr()};
  if (logCount < 30) logCount++;
}

const char INDEX_HTML[] PROGMEM = R"=====(
<!doctype html>
<html lang="vi">
<head>
<meta charset="utf-8"/><meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>Bãi đỗ xe tự động - Nhóm 25</title>
<style>
:root{ --bg:#a2afaf; --card:#fff; --border:#d1d5db; --text:#22223b; --muted:#6b7280; --accent:#2563eb; --accent2:#fbbf24; --shadow:0 4px 24px 0 rgba(37,99,235,0.08); }
body{ margin:0; font-family:'Segoe UI',Arial,sans-serif; background:var(--bg); color:var(--text); min-height:100vh; }
.container{ max-width:1200px; margin:auto; padding:20px; }
.stats{ display:grid; grid-template-columns:repeat(auto-fit,minmax(220px,1fr)); gap:16px; margin-bottom:16px; }
.card{ background:var(--card); border:1px solid var(--border); border-radius:18px; padding:20px; box-shadow:var(--shadow); text-align:center;}
.value{ font-size:32px; font-weight:bold; color:var(--accent); }
.slot-grid{ display:grid; grid-template-columns:repeat(auto-fit,minmax(200px,1fr)); gap:16px; margin-bottom:20px; }
.slot-card{ background:var(--card); border:1px solid var(--border); border-radius:14px; padding:16px; display:flex; justify-content:space-between; align-items:center; font-weight:bold; box-shadow:var(--shadow); color:var(--accent); }
.led{ width:22px; height:22px; border-radius:50%; background:#e5e7eb; border:2px solid #d1d5db; }
.led.on.red { background:#ef4444; box-shadow:0 0 16px #ef4444; }
.led.on.blue { background:#3b82f6; box-shadow:0 0 16px #3b82f6; }
.led.on.green { background:#22c55e; box-shadow:0 0 16px #22c55e; }
.led.on.pink { background:#d946ef; box-shadow:0 0 16px #d946ef; }
table{ width:100%; border-collapse:collapse; background:var(--card); border-radius:14px; overflow:hidden; margin-top:10px; }
th,td{ padding:12px 10px; border-bottom:1px solid var(--border); text-align:center; }
th{ background:var(--accent); color:#fff; }
/* Cố định chiều rộng cột thời gian */
th:nth-child(2), td:nth-child(2) { min-width: 180px; white-space: nowrap; }
.in{ color:#22c55e; font-weight:bold; } .out{ color:#ef4444; font-weight:bold; }
.snowflake { position: fixed; top: -40px; color: #fff; pointer-events: none; z-index: 9999; }
</style>
</head>
<body>
<div class="container">
  <h2 style="color:var(--accent); text-align:center;">🚗 Bãi đỗ xe thông minh - Nhóm 24</h2>
  <div class="stats">
    <div class="card"><div>Xe vào</div><div class="value" id="cntIn">0</div></div>
    <div class="card"><div>Xe ra</div><div class="value" id="cntOut">0</div></div>
    <div class="card"><div>Chỗ trống</div><div class="value" id="freeNow">4</div></div>
    <div class="card"><div>Tổng tiền hôm nay</div><div class="value" id="totalMoney">0 VNĐ</div></div>
  </div>
  <div class="slot-grid">
    <div class="slot-card">SLOT 1 <div id="led1" class="led"></div></div>
    <div class="slot-card">SLOT 2 <div id="led2" class="led"></div></div>
    <div class="slot-card">SLOT 3 <div id="led3" class="led"></div></div>
    <div class="slot-card">SLOT 4 <div id="led4" class="led"></div></div>
  </div>
  <div style="overflow-x:auto;">
    <table>
      <thead><tr><th>STT</th><th>Thời gian</th><th>UID</th><th>Trạng thái</th><th>Phút</th><th>Số tiền</th></tr></thead>
      <tbody id="rows"></tbody>
    </table>
  </div>
</div>
<script>
function formatMoneyVND(m) { 
  return m.toString().replace(/\B(?=(\d{3})+(?!\d))/g, ".") + " VNĐ"; 
}
function updateData() {
  fetch('/data').then(r => r.json()).then(d => {
    document.getElementById("cntIn").innerText = d.in;
    document.getElementById("cntOut").innerText = d.out;
    document.getElementById("totalMoney").innerText = formatMoneyVND(d.rev);
    let free = 0;
    for(let i=0; i<4; i++) {
      let led = document.getElementById("led" + (i+1));
      let colors = ["red", "blue", "green", "pink"];
      led.className = "led";
      if(d.slots[i] == '1') led.classList.add("on", colors[i]); else free++;
    }
    document.getElementById("freeNow").innerText = free;
    let h = "";
    d.logs.forEach((l, i) => {
      h += `<tr><td>${i+1}</td><td>${l.t}</td><td>${l.u}</td><td class="${l.s=='IN'?'in':'out'}">${l.s}</td><td>${l.m}</td><td style="font-weight:bold; color:#f59e42;">${formatMoneyVND(l.f)}</td></tr>`;
    });
    document.getElementById("rows").innerHTML = h;
  });
}
setInterval(updateData, 2000);
updateData();

// Tuyết rơi Noel
setInterval(() => {
  const s = document.createElement('span'); s.className = 'snowflake'; s.textContent = '❄';
  s.style.left = Math.random() * 100 + 'vw'; s.style.fontSize = (Math.random() * 10 + 10) + 'px';
  document.body.appendChild(s);
  s.animate([{ transform: `translateY(0px)` }, { transform: `translateY(${window.innerHeight}px)` }], { duration: 5000 });
  setTimeout(() => s.remove(), 5000);
}, 400);
</script>
</body></html>
)=====";

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, 16, 17); 
  
  WiFi.begin(ssid, password);
  while(WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi Connected!");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  server.on("/", [](){ server.send(200, "text/html", INDEX_HTML); });
  
  server.on("/data", [](){
    StaticJsonDocument<4000> doc;
    doc["in"] = inCount;
    doc["out"] = outCount;
    doc["rev"] = totalRevenue;
    doc["slots"] = slots;
    JsonArray arr = doc.createNestedArray("logs");
    for(int i=0; i<logCount; i++){ 
      JsonObject o = arr.createNestedObject(); 
      o["u"] = logs[i].uid; 
      o["s"] = logs[i].action; 
      o["m"] = logs[i].mins; 
      o["f"] = logs[i].money; 
      o["t"] = logs[i].timeStr;
    }
    String j;
    serializeJson(doc, j);
    server.send(200, "application/json", j);
  });
  
  server.begin();
}

void loop() {
  server.handleClient();
  if(Serial2.available()){
    String r = Serial2.readStringUntil('\n'); r.trim();
    if(r.startsWith("S:")) {
      slots = r.substring(2);
    }
    else if(r.startsWith("IN:")){
      inCount++;
      addLog(r.substring(3), "IN", 0, 0);
    }
    else if(r.startsWith("OUT:")){
      outCount++;
      int c1 = r.indexOf(',');
      int c2 = r.lastIndexOf(',');
      String u = r.substring(4, c1);
      int m = r.substring(c1+1, c2).toInt();
      long f = r.substring(c2+1).toInt(); // Đọc giá trị tiền tệ
      
      totalRevenue += f;
      addLog(u, "OUT", m, f);
    }
  }
}
