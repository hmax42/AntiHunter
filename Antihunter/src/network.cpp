#include "network.h"
#include "hardware.h"
#include "scanner.h"
#include "main.h"
#include <AsyncTCP.h>
#include "esp_task_wdt.h"

extern "C"
{
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_coexist.h"
}

// Network
AsyncWebServer *server = nullptr;
bool meshEnabled = false;
static unsigned long lastMeshSend = 0;
const unsigned long MESH_SEND_INTERVAL = 3500;
const int MAX_MESH_SIZE = 230;
static String nodeId = "";

// Scanner vars
extern volatile bool scanning;
extern volatile int totalHits;
extern volatile bool trackerMode;
extern std::set<String> uniqueMacs;

// Module refs
extern Preferences prefs;
extern volatile bool stopRequested;
extern ScanMode currentScanMode;
extern int cfgBeeps, cfgGapMs;
extern std::vector<uint8_t> CHANNELS;
extern TaskHandle_t workerTaskHandle;
extern TaskHandle_t blueTeamTaskHandle;
TaskHandle_t karmaTaskHandle = nullptr;
TaskHandle_t probeFloodTaskHandle = nullptr;
extern String macFmt6(const uint8_t *m);
extern bool parseMac6(const String &in, uint8_t out[6]);
extern void parseChannelsCSV(const String &csv);


void initializeNetwork()
{ 
  esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
  Serial.println("Initializing mesh UART...");
  initializeMesh();

  Serial.println("Starting AP...");
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL, 0);
  delay(500);
  WiFi.setHostname("Antihunter");
  delay(100);
  Serial.println("Starting web server...");
  startWebServer();
}

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Antihunter</title>
<style>
:root{--bg:#000;--fg:#00ff7f;--fg2:#00cc66;--accent:#0aff9d;--card:#0b0b0b;--muted:#00ff7f99}
*{box-sizing:border-box} html,body{height:100%}
body{margin:0;background:var(--bg);color:var(--fg);font-family:ui-monospace,SFMono-Regular,Menlo,Monaco,Consolas,"Liberation Mono","Courier New",monospace}
.header{padding:22px 18px;border-bottom:1px solid #003b24;background:linear-gradient(180deg,#001a10,#000);display:flex;align-items:center;gap:14px}
h1{margin:0;font-size:22px;letter-spacing:1px}
.container{max-width:1200px;margin:0 auto;padding:16px}
.card{background:var(--card);border:1px solid #003b24;border-radius:12px;padding:16px;margin:16px 0;box-shadow:0 8px 30px rgba(0,255,127,.05)}
label{display:block;margin:6px 0 4px;color:var(--muted)}
textarea, input[type=text], input[type=number], select{width:100%;background:#000;border:1px solid #003b24;border-radius:10px;color:var(--fg);padding:10px 12px;outline:none}
textarea{min-height:128px;resize:vertical}
select{cursor:pointer}
select option{background:#000;color:var(--fg)}
.btn{display:inline-block;padding:10px 14px;border-radius:10px;border:1px solid #004e2f;background:#001b12;color:var(--fg);text-decoration:none;cursor:pointer;transition:transform .05s ease, box-shadow .2s}
.btn:hover{box-shadow:0 6px 18px rgba(10,255,157,.15);transform:translateY(-1px)}
.btn.primary{background:#002417;border-color:#00cc66}
.btn.alt{background:#00140d;border-color:#004e2f;color:var(--accent)}
.row{display:flex;gap:10px;flex-wrap:wrap;align-items:center}
.small{opacity:.65} pre{white-space:pre-wrap;background:#000;border:1px dashed #003b24;border-radius:10px;padding:12px}
a{color:var(--accent)} hr{border:0;border-top:1px dashed #003b24;margin:14px 0}
.banner{font-size:12px;color:#0aff9d;border:1px dashed #004e2f;padding:8px;border-radius:10px;background:#001108}
.grid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:14px}
@media(max-width:900px){.grid{grid-template-columns:1fr 1fr}}
@media(max-width:600px){.grid{grid-template-columns:1fr}}
#toast{position:fixed;right:16px;bottom:16px;display:flex;flex-direction:column;gap:8px;z-index:9999}
.toast{background:#001d12;border:1px solid #0aff9d55;color:var(--fg);padding:10px 12px;border-radius:10px;box-shadow:0 8px 30px rgba(10,255,157,.2);opacity:0;transform:translateY(8px);transition:opacity .15s, transform .15s}
.toast.show{opacity:1;transform:none}
.toast .title{color:#0aff9d}
.footer{opacity:.7;font-size:12px;padding:8px 16px;text-align:center}
.logo{width:28px;height:28px}
.mode-indicator{background:#001a10;border:1px solid #00cc66;padding:8px 12px;border-radius:8px;font-weight:bold;margin-left:auto}
</style></head><body>
<div class="header">
  <svg class="logo" viewBox="0 0 64 64" xmlns="http://www.w3.org/2000/svg" aria-hidden="true">
    <rect x="6" y="6" width="52" height="52" rx="8" fill="#00180F" stroke="#00ff7f" stroke-width="2"/>
    <path d="M16 40 L32 16 L48 40" fill="none" stroke="#0aff9d" stroke-width="3"/>
    <circle cx="32" cy="44" r="3" fill="#00ff7f"/>
  </svg>
  <h1>Antihunter</h1>
  <div class="mode-indicator" id="modeIndicator">WiFi Mode</div>
</div>
<div id="toast"></div>
<div class="container">

<div class="grid">
  <div class="card">
    <div class="banner">Targets: full MACs (<code>AA:BB:CC:DD:EE:FF</code>) or OUIs (<code>AA:BB:CC</code>), one per line. Used in <b>List Scan</b>.</div>
    <form id="f" method="POST" action="/save">
      <label for="list">Targets</label>
      <textarea id="list" name="list" placeholder="AA:BB:CC:DD:EE:FF&#10;DC:A6:32"></textarea>
      <div class="row" style="margin-top:10px">
        <button class="btn primary" type="submit">Save</button>
        <a class="btn" href="/export" data-ajax="false">Download</a>
      </div>
    </form>
  </div>

  <div class="card">
    <h3>List Scan</h3>
    <form id="s" method="POST" action="/scan">
      <label>Scan Mode</label>
      <select name="mode" id="scanMode">
        <option value="0">WiFi Only</option>
        <option value="1">BLE Only</option>
        <option value="2">WiFi + BLE</option>
      </select>
      <label>Duration (seconds)</label>
      <input type="number" name="secs" min="0" max="86400" value="60">
      <div class="row"><input type="checkbox" id="forever1" name="forever" value="1"><label for="forever1">∞ Forever</label></div>
      <label>WiFi Channels CSV</label>
      <input type="text" name="ch" value="1,6,11">
      <div class="row" style="margin-top:10px">
        <button class="btn primary" type="submit">Start List Scan</button>
        <a class="btn" href="/stop" data-ajax="true">Stop</a>
      </div>
      <p class="small">AP goes offline during scan and returns.</p>
    </form>
  </div>

  <div class="card">
    <h3>Tracker (single MAC "Geiger")</h3>
    <form id="t" method="POST" action="/track">
      <label>Scan Mode</label>
      <select name="mode">
        <option value="0">WiFi Only</option>
        <option value="1">BLE Only</option>
        <option value="2">WiFi + BLE</option>
      </select>
      <label>Target MAC (AA:BB:CC:DD:EE:FF)</label>
      <input type="text" name="mac" placeholder="34:21:09:83:D9:51">
      <label>Duration (seconds)</label>
      <input type="number" name="secs" min="0" max="86400" value="180">
      <div class="row"><input type="checkbox" id="forever2" name="forever" value="1"><label for="forever2">∞ Forever</label></div>
      <label>WiFi Channels CSV (use single channel for smoother tracking)</label>
      <input type="text" name="ch" value="6">
      <p class="small">Closer = faster & higher-pitch beeps. Lost = slow click.</p>
      <div class="row" style="margin-top:10px">
        <button class="btn primary" type="submit">Start Tracker</button>
        <a class="btn" href="/stop" data-ajax="true">Stop</a>
      </div>
    </form>
  </div>

    <div class="card">
    <h3>Scan and Sniff</h3>
    <form id="sniffer" method="POST" action="/sniffer">  
    <label>Detection Method</label>
    <select name="detection" id="detectionMode">
        <option value="device-scan">Scan BLE/WiFi Devices</option>
        <!--
        <option value="deauth">Deauth/Disassoc Frames</option>
        <option value="pwnagotchi">Pwnagotchi Detection</option>
        <option value="pineapple">WiFi Pineapple Detection</option>
        <option value="multi-ssid">Multi-SSID AP Detection</option>
        <option value="karma">Karma Attack Detection</option>
        <option value="probe-flood">Probe Flood Detection</option>
        <option value="beacon-flood">Beacon Flood Detection</option>
        <option value="ble-spam">BLE Spam Detection</option>
        --> 
      </select>
      <label>Duration (seconds)</label>
      <input type="number" name="secs" min="0" max="86400" value="60">
      <div class="row"><input type="checkbox" id="forever3" name="forever" value="1"><label for="forever3">∞ Forever</label></div>
      <p class="small">Scans for devices. AP goes offline during detection</p>
      <div class="row" style="margin-top:10px">
        <button class="btn primary" type="submit">Start Sniffer</button>
        <a class="btn alt" href="/sniffer-cache" data-ajax="false">View Cache</a>
        <a class="btn" href="/stop" data-ajax="true">Stop</a>
      </div>
    </form>
  </div>

   <div class="card">
    <h3>Node Configuration</h3>
    <div class="row">
      <input type="checkbox" id="meshEnabled" unchecked>
      <label for="meshEnabled">Enable Mesh Notifications</label>
    </div>
    <label for="nodeId">Node ID</label>
    <form id="nodeForm" method="POST" action="/node-id">
      <input type="text" id="nodeId" name="id" maxlength="16" placeholder="NODE_01">
      <div class="row" style="margin-top:10px;">
        <button class="btn primary" type="submit">Save ID</button>
        <a class="btn alt" href="/mesh-test" data-ajax="true">Test Mesh</a>
      </div>
    </form>
    <hr>
    <h4 style="margin:12px 0 8px;color:var(--fg)">Buzzer Settings</h4>
    <form id="buzzerForm" method="POST" action="/config">
      <label for="beeps">Beeps per Hit</label>
      <input type="number" id="beeps" name="beeps" min="1" max="10" value="2">
      <label for="gap">Gap Between Beeps (ms)</label>
      <input type="number" id="gap" name="gap" min="20" max="2000" value="80">
      <div class="row" style="margin-top:10px;">
        <button class="btn primary" type="submit">Save Buzzer</button>
        <a class="btn alt" href="/beep" data-ajax="true">Test Beep</a>
      </div>
    </form>
    <p class="small">Configure mesh notifications and buzzer behavior for target detection alerts.</p>
  </div>

  <div class="card">
    <h3>Diagnostics</h3>
    <pre id="diag">Loading…</pre>
  </div>
</div>
 
<div class="card">
  <h3>Last Results</h3>
  <pre id="r">None yet.</pre>
</div>

<div class="footer">© Team AntiHunter 2025</div>
</div>
<script>
let selectedMode = '0';

function toast(msg){
  const wrap = document.getElementById('toast');
  const el = document.createElement('div');
  el.className = 'toast';
  el.innerHTML = '<div class="title">Antihunter</div><div class="msg">'+msg+'</div>';
  wrap.appendChild(el);
  requestAnimationFrame(()=>{ el.classList.add('show'); });
  setTimeout(()=>{ el.classList.remove('show'); setTimeout(()=>wrap.removeChild(el), 200); }, 2500);
}

async function ajaxForm(form, okMsg){
  const fd = new FormData(form);
  try{
    const r = await fetch(form.action, {method:'POST', body:fd});
    const t = await r.text();
    toast(okMsg || t);
  }catch(e){
    toast('Error: '+e.message);
  }
}

async function load(){
  try{
    const r = await fetch('/export'); 
    document.getElementById('list').value = await r.text();
    const cfg = await fetch('/config').then(r=>r.json());
    document.getElementById('beeps').value = cfg.beeps;
    document.getElementById('gap').value = cfg.gap;
    const rr = await fetch('/results'); 
    document.getElementById('r').innerText = await rr.text();
    loadNodeId();
  }catch(e){}
}

async function loadNodeId(){
  try{
    const r = await fetch('/node-id');
    const data = await r.json();
    document.getElementById('nodeId').value = data.nodeId;
  }catch(e){}
}

document.getElementById('nodeForm').addEventListener('submit', e=>{
  e.preventDefault();
  ajaxForm(e.target, 'Node ID saved');
});

async function tick(){
  try{
    const d = await fetch('/diag'); 
    const diagText = await d.text();
    document.getElementById('diag').innerText = diagText;
    const rr = await fetch('/results'); 
    document.getElementById('r').innerText = await rr.text();
    if (diagText.includes('Scanning: yes')) {
      const modeMatch = diagText.match(/Scan Mode: (\w+)/);
      if (modeMatch) {
        const serverMode = modeMatch[1];
        let modeValue = '0';
        if (serverMode === 'BLE') modeValue = '1';
        else if (serverMode === 'WiFi+BLE') modeValue = '2';
        if (modeValue !== selectedMode) {
          updateModeIndicator(modeValue);
        }
      }
    }
  }catch(e){}
}

function updateModeIndicator(mode) {
  selectedMode = mode;
  const indicator = document.getElementById('modeIndicator');
  switch(mode) {
    case '0': indicator.textContent = 'WiFi Mode'; break;
    case '1': indicator.textContent = 'BLE Mode'; break;
    case '2': indicator.textContent = 'WiFi+BLE Mode'; break;
    default: indicator.textContent = 'WiFi Mode';
  }
}

document.getElementById('f').addEventListener('submit', e=>{ 
  e.preventDefault(); ajaxForm(e.target, 'Targets saved ✓'); 
});

document.getElementById('s').addEventListener('submit', e=>{
  e.preventDefault();
  const fd = new FormData(e.target);
  updateModeIndicator(fd.get('mode'));
  fetch('/scan', {method:'POST', body:fd}).then(()=>{
    toast('List scan started. AP will drop & return…');
  }).catch(err=>toast('Error: '+err.message));
});

document.getElementById('meshEnabled').addEventListener('change', e=>{
  const enabled = e.target.checked;
  fetch('/mesh', {method:'POST', body: new URLSearchParams({enabled: enabled})})
    .then(r=>r.text())
    .then(t=>toast(t))
    .catch(err=>toast('Error: '+err.message));
});

document.getElementById('t').addEventListener('submit', e=>{
  e.preventDefault();
  const fd = new FormData(e.target);
  updateModeIndicator(fd.get('mode'));
  fetch('/track', {method:'POST', body:fd}).then(()=>{
    toast('Tracker started. AP will drop & return…');
  }).catch(err=>toast('Error: '+err.message));
});

document.getElementById('scanMode').addEventListener('change', e=>{
  updateModeIndicator(e.target.value);
});

document.getElementById('sniffer').addEventListener('submit', e=>{  // Now matches form id
  e.preventDefault();
  const fd = new FormData(e.target);
  fetch('/sniffer', {method:'POST', body:fd}).then(()=>{
    toast('Sniffer scan started. AP will drop & return...');
  }).catch(err=>toast('Error: '+err.message));
});

document.querySelector('#t select[name="mode"]').addEventListener('change', e=>{
  updateModeIndicator(e.target.value);
});

document.addEventListener('click', e=>{
  const a = e.target.closest('a[href="/stop"]');
  if (!a) return;
  e.preventDefault();
  fetch('/stop').then(r=>r.text()).then(t=>toast(t));
});

document.getElementById('buzzerForm').addEventListener('submit', e=>{
  e.preventDefault();
  ajaxForm(e.target, 'Buzzer settings saved');
});

load();
setInterval(tick, 1000);
</script>
</body></html>
)HTML";

void startWebServer()
{
  if (!server)
    server = new AsyncWebServer(80);

  server->on("/", HTTP_GET, [](AsyncWebServerRequest *r)
             {
        AsyncWebServerResponse* res = r->beginResponse(200, "text/html", (const uint8_t*)INDEX_HTML, strlen_P(INDEX_HTML));
        res->addHeader("Cache-Control", "no-store");
        r->send(res); });

  server->on("/export", HTTP_GET, [](AsyncWebServerRequest *r)
             { r->send(200, "text/plain", getTargetsList()); });

  server->on("/results", HTTP_GET, [](AsyncWebServerRequest *r)
           { 
               std::lock_guard<std::mutex> lock(antihunter::lastResultsMutex);
               String results = antihunter::lastResults.empty() ? "None yet." : String(antihunter::lastResults.c_str());
               
               r->send(200, "text/plain", results); 
           });

  server->on("/save", HTTP_POST, [](AsyncWebServerRequest *req)
             {
        if (!req->hasParam("list", true)) {
            req->send(400, "text/plain", "Missing 'list'");
            return;
        }
        String txt = req->getParam("list", true)->value();
        saveTargetsList(txt);
        req->send(200, "text/plain", "Saved"); });

  server->on("/node-id", HTTP_POST, [](AsyncWebServerRequest *req)
             {
    String id = req->hasParam("id", true) ? req->getParam("id", true)->value() : "";
    if (id.length() > 0 && id.length() <= 16) {
        setNodeId(id);
        req->send(200, "text/plain", "Node ID updated");
    } else {
        req->send(400, "text/plain", "Invalid ID (1-16 chars)");
    } });

  server->on("/node-id", HTTP_GET, [](AsyncWebServerRequest *r)
             {
    String j = "{\"nodeId\":\"" + getNodeId() + "\"}";
    r->send(200, "application/json", j); });

  server->on("/scan", HTTP_POST, [](AsyncWebServerRequest *req)
             {
        int secs = 60;
        bool forever = false;
        ScanMode mode = SCAN_WIFI;
        
        if (req->hasParam("forever", true)) forever = true;
        if (req->hasParam("secs", true)) {
            int v = req->getParam("secs", true)->value().toInt();
            if (v < 0) v = 0;
            if (v > 86400) v = 86400;
            secs = v;
        }
        if (req->hasParam("mode", true)) {
            int m = req->getParam("mode", true)->value().toInt();
            if (m >= 0 && m <= 2) mode = (ScanMode)m;
        }
        String ch = "1,6,11";
        if (req->hasParam("ch", true)) ch = req->getParam("ch", true)->value();
        
        parseChannelsCSV(ch);
        currentScanMode = mode;
        stopRequested = false;
        
        String modeStr = (mode == SCAN_WIFI) ? "WiFi" : (mode == SCAN_BLE) ? "BLE" : "WiFi+BLE";
        req->send(200, "text/plain", forever ? ("Scan starting (forever) - " + modeStr) : ("Scan starting for " + String(secs) + "s - " + modeStr));
        
        if (!workerTaskHandle) {
            xTaskCreatePinnedToCore(listScanTask, "scan", 8192, (void*)(intptr_t)(forever ? 0 : secs), 1, &workerTaskHandle, 1);
        } });

  server->on("/track", HTTP_POST, [](AsyncWebServerRequest *req)
             {
        String mac = req->getParam("mac", true) ? req->getParam("mac", true)->value() : "";
        int secs = req->getParam("secs", true) ? req->getParam("secs", true)->value().toInt() : 180;
        bool forever = req->hasParam("forever", true);
        ScanMode mode = SCAN_WIFI;
        
        if (req->hasParam("mode", true)) {
            int m = req->getParam("mode", true)->value().toInt();
            if (m >= 0 && m <= 2) mode = (ScanMode)m;
        }
        String ch = req->getParam("ch", true) ? req->getParam("ch", true)->value() : "6";
        
        uint8_t tmp[6];
        if (!parseMac6(mac, tmp)) {
            req->send(400, "text/plain", "Invalid MAC");
            return;
        }
        
        setTrackerMac(tmp);
        parseChannelsCSV(ch);
        currentScanMode = mode;
        stopRequested = false;
        
        String modeStr = (mode == SCAN_WIFI) ? "WiFi" : (mode == SCAN_BLE) ? "BLE" : "WiFi+BLE";
        req->send(200, "text/plain", forever ? ("Tracker starting (forever) - " + modeStr) : ("Tracker starting for " + String(secs) + "s - " + modeStr));
        
        if (!workerTaskHandle) {
            xTaskCreatePinnedToCore(trackerTask, "tracker", 8192, (void*)(intptr_t)(forever ? 0 : secs), 1, &workerTaskHandle, 1);
        } });

  server->on("/gps", HTTP_GET, [](AsyncWebServerRequest *r)
             {
    String gpsInfo = "GPS Data: " + getGPSData() + "\n";
    if (gpsValid) {
        gpsInfo += "Latitude: " + String(gpsLat, 6) + "\n";
        gpsInfo += "Longitude: " + String(gpsLon, 6) + "\n";
    } else {
        gpsInfo += "GPS: No valid fix\n";
    }
    r->send(200, "text/plain", gpsInfo); });

  server->on("/sd-status", HTTP_GET, [](AsyncWebServerRequest *r)
             {
    String status = sdAvailable ? "SD card: Available" : "SD card: Not available";
    r->send(200, "text/plain", status); });

  server->on("/stop", HTTP_GET, [](AsyncWebServerRequest *r)
             {
        stopRequested = true;
        r->send(200, "text/plain", "Stopping… (AP will return shortly)"); });

  server->on("/beep", HTTP_GET, [](AsyncWebServerRequest *r)
             {
        beepPattern(getBeepsPerHit(), getGapMs());
        r->send(200, "text/plain", "Beeped"); });

  server->on("/config", HTTP_GET, [](AsyncWebServerRequest *r)
             {
        String j = String("{\"beeps\":") + cfgBeeps + ",\"gap\":" + cfgGapMs + "}";
        r->send(200, "application/json", j); });

  server->on("/config", HTTP_POST, [](AsyncWebServerRequest *req)
             {
        int beeps = cfgBeeps, gap = cfgGapMs;
        if (req->hasParam("beeps", true)) beeps = req->getParam("beeps", true)->value().toInt();
        if (req->hasParam("gap", true)) gap = req->getParam("gap", true)->value().toInt();
        if (beeps < 1) beeps = 1;
        if (beeps > 10) beeps = 10;
        if (gap < 20) gap = 20;
        if (gap > 2000) gap = 2000;
        cfgBeeps = beeps;
        cfgGapMs = gap;
        saveConfiguration();
        req->send(200, "text/plain", "Config saved"); });

  server->on("/mesh", HTTP_POST, [](AsyncWebServerRequest *req)
             {
        if (req->hasParam("enabled", true)) {
            meshEnabled = req->getParam("enabled", true)->value() == "true";
            Serial.printf("[MESH] %s\n", meshEnabled ? "Enabled" : "Disabled");
            req->send(200, "text/plain", meshEnabled ? "Mesh enabled" : "Mesh disabled");
        } else {
            req->send(400, "text/plain", "Missing enabled parameter");
        } });

  server->on("/mesh-test", HTTP_GET, [](AsyncWebServerRequest *r)
             {
        char test_msg[] = "Antihunter: Test mesh notification";
        Serial.printf("[MESH] Test: %s\n", test_msg);
        Serial1.println(test_msg);
        r->send(200, "text/plain", "Test message sent to mesh"); });

  server->on("/diag", HTTP_GET, [](AsyncWebServerRequest *r)
             {
        String s = getDiagnostics();
        r->send(200, "text/plain", s); });

  server->on("/sniffer", HTTP_POST, [](AsyncWebServerRequest *req)
           {
  String detection = req->getParam("detection", true) ? req->getParam("detection", true)->value() : "device-scan";
  int secs = req->getParam("secs", true) ? req->getParam("secs", true)->value().toInt() : 60;
  bool forever = req->hasParam("forever", true);
  
  if (detection == "deauth") {
    if (secs < 0) secs = 0; 
    if (secs > 86400) secs = 86400;
    
    stopRequested = false;
    req->send(200, "text/plain", forever ? "Deauth detection starting (forever)" : ("Deauth detection starting for " + String(secs) + "s"));
    
    if (!blueTeamTaskHandle) {
      xTaskCreatePinnedToCore(blueTeamTask, "blueteam", 12288, (void*)(intptr_t)(forever ? 0 : secs), 1, &blueTeamTaskHandle, 1);
    }
    
  } else if (detection == "beacon-flood") {
    if (secs < 0) secs = 0; 
    if (secs > 86400) secs = 86400;
    
    stopRequested = false;
    req->send(200, "text/plain", forever ? "Beacon flood detection starting (forever)" : ("Beacon flood detection starting for " + String(secs) + "s"));
    
    if (!blueTeamTaskHandle) {
      xTaskCreatePinnedToCore(beaconFloodTask, "beaconflood", 12288, (void*)(intptr_t)(forever ? 0 : secs), 1, &blueTeamTaskHandle, 1);
    }
  } else if (detection == "pwnagotchi") {
    stopRequested = false;
    req->send(200, "text/plain", "Pwnagotchi detection starting");
    if (!blueTeamTaskHandle) {
        xTaskCreatePinnedToCore(pwnagotchiDetectionTask, "pwn", 12288,
                              (void*)(intptr_t)(forever ? 0 : secs),
                              1, &blueTeamTaskHandle, 1);
    }
} else if (detection == "pineapple") {
    pineappleDetectionEnabled = true;
    stopRequested = false;
    req->send(200, "text/plain", forever ? "Pineapple detection starting (forever)" : 
             ("Pineapple detection starting for " + String(secs) + "s"));
    
    if (!blueTeamTaskHandle) {
        xTaskCreatePinnedToCore(snifferScanTask, "sniffer", 12288, 
                              (void*)(intptr_t)(forever ? 0 : secs), 1, &blueTeamTaskHandle, 1);
    }
}

else if (detection == "multi-ssid") {
    multissidDetectionEnabled = true;
    stopRequested = false;
    req->send(200, "text/plain", forever ? "Multi-SSID detection starting (forever)" : 
             ("Multi-SSID detection starting for " + String(secs) + "s"));
    
    if (!blueTeamTaskHandle) {
        xTaskCreatePinnedToCore(snifferScanTask, "sniffer", 12288, 
                              (void*)(intptr_t)(forever ? 0 : secs), 1, &blueTeamTaskHandle, 1);
    }
} else if (detection == "ble-spam") {
    if (secs < 0) secs = 0; 
    if (secs > 86400) secs = 86400;
    
    stopRequested = false;
    req->send(200, "text/plain", forever ? "BLE spam detection starting (forever)" : ("BLE spam detection starting for " + String(secs) + "s"));
    
    if (!blueTeamTaskHandle) {
        xTaskCreatePinnedToCore(bleScannerTask, "blescan", 12288, (void*)(intptr_t)(forever ? 0 : secs), 1, &blueTeamTaskHandle, 1);
    }
  
  } else if (detection == "device-scan") {
      if (secs < 0) secs = 0;
      if (secs > 86400) secs = 86400;
      
      stopRequested = false;
      req->send(200, "text/plain", forever ? "Device scan starting (forever)" : ("Device scan starting for " + String(secs) + "s"));
      
      if (!workerTaskHandle) {
          xTaskCreatePinnedToCore(snifferScanTask, "sniffer", 12288, (void*)(intptr_t)(forever ? 0 : secs), 1, &workerTaskHandle, 1);
      }
   } else if (detection == "karma") {
      karmaDetectionEnabled = true;
      stopRequested = false;
      req->send(200, "text/plain",
                forever ? "Karma detection starting (forever)" :
                ("Karma detection starting for " + String(secs) + "s"));

      if (!blueTeamTaskHandle) {
          xTaskCreatePinnedToCore(karmaDetectionTask, "karma", 12288,
                                  (void*)(intptr_t)(forever ? 0 : secs),
                                  1, &blueTeamTaskHandle, 1);
      }

  } else if (detection == "probe-flood") {
      probeFloodDetectionEnabled = true;
      stopRequested = false;
      req->send(200, "text/plain",
                forever ? "Probe flood detection starting (forever)" :
                ("Probe flood detection starting for " + String(secs) + "s"));

      if (!blueTeamTaskHandle) {
          xTaskCreatePinnedToCore(probeFloodDetectionTask, "probe", 12288,
                                  (void*)(intptr_t)(forever ? 0 : secs),
                                  1, &blueTeamTaskHandle, 1);
      }
  } else {
    req->send(400, "text/plain", "Unknown detection mode");
  } });

  server->on("/deauth-results", HTTP_GET, [](AsyncWebServerRequest *r)
             {
  String results = "Deauth Detection Results\n";
  results += "Deauth frames: " + String(deauthCount) + "\n";
  results += "Disassoc frames: " + String(disassocCount) + "\n\n";
  
  int show = min((int)deauthLog.size(), 100);
  for (int i = 0; i < show; i++) {
    const auto &hit = deauthLog[i];
    results += String(hit.isDisassoc ? "DISASSOC" : "DEAUTH") + " ";
    results += macFmt6(hit.srcMac) + " -> " + macFmt6(hit.destMac);
    results += " BSSID:" + macFmt6(hit.bssid);
    results += " RSSI:" + String(hit.rssi) + "dBm";
    results += " CH:" + String(hit.channel);
    results += " Reason:" + String(hit.reasonCode) + "\n";
  }  
  r->send(200, "text/plain", results); });

  server->on("/sniffer-cache", HTTP_GET, [](AsyncWebServerRequest *r) {
      r->send(200, "text/plain", getSnifferCache());
  });

  server->begin();
  Serial.println("[WEB] Server started.");
}

void startAPAndServer() {
    Serial.println("[SYS] Starting AP and web server...");
    
    if (server) {
        server->end();
        delay(1000);
        delete server;
        server = nullptr;
        delay(1000);
    }
    
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_set_promiscuous_filter(nullptr);
    delay(500);
    
    WiFi.persistent(false);
    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    delay(1000);
    
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK) {
        // Serial.printf("[SYS] WiFi stop error: %d\n", err);
        delay(500);
    }
    
    // err = esp_wifi_deinit();
    // if (err != ESP_OK) {
    //     // Serial.printf("[SYS] WiFi deinit error: %d\n", err);
    //     esp_wifi_stop();
    //     delay(500);
    //     esp_wifi_deinit();
    // }
    
    WiFi.mode(WIFI_OFF);
    delay(3000);
    
    const int MAX_RETRIES = 3;
    bool apStarted = false;
    
    for (int attempt = 1; attempt <= MAX_RETRIES && !apStarted; attempt++) {
        Serial.printf("[SYS] AP start attempt %d/%d\n", attempt, MAX_RETRIES);
        
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        err = esp_wifi_init(&cfg);
        if (err != ESP_OK) {
            Serial.printf("[SYS] WiFi init failed: %d\n", err);
            delay(2000);
            continue;
        }
        
        WiFi.mode(WIFI_AP);
        delay(1500);
        
        IPAddress local_IP(192, 168, 4, 1);
        IPAddress gateway(192, 168, 4, 1);
        IPAddress subnet(255, 255, 255, 0);
        
        if (!WiFi.softAPConfig(local_IP, gateway, subnet)) {
            Serial.println("[SYS] AP Config failed");
            delay(1000);
        }
        
        int channel = (attempt == 3) ? 11 : AP_CHANNEL;
        apStarted = WiFi.softAP(AP_SSID, AP_PASS, channel, 0, 8);
        
        if (!apStarted) {
            WiFi.mode(WIFI_OFF);
            delay(2000);
        }
    }
    
    if (apStarted) {
        delay(3000);
        
        IPAddress ip = WiFi.softAPIP();
        for (int retries = 0; retries < 10 && ip == IPAddress(0,0,0,0); retries++) {
            delay(500);
            ip = WiFi.softAPIP();
        }
        
        Serial.printf("[SYS] AP started successfully. IP: %s\n", ip.toString().c_str());
        WiFi.setHostname("Antihunter");
        delay(2000);
        
        server = new AsyncWebServer(80);
        startWebServer();
        
    } else {
        Serial.println("[CRITICAL] Cannot start ANY AP mode!");
        Serial.println("[CRITICAL] Device will restart in 5 seconds...");
        delay(5000);
        ESP.restart();
    }
}

void stopAPAndServer() {
    Serial.println("[SYS] Stopping AP and web server...");
    
    if (server) {
        server->end();
        delay(500);
        delete server;
        server = nullptr;
        delay(500);
    }
    
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_set_promiscuous_filter(nullptr);
    delay(200);
    
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true);
    delay(1000);
    
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK) {
        Serial.printf("[SYS] WiFi stop error: %d\n", err);
        delay(500);
        esp_wifi_stop();
    }
    
    err = esp_wifi_deinit();
    if (err != ESP_OK) {
        Serial.printf("[SYS] WiFi deinit error: %d\n", err);
        delay(500);
        esp_wifi_stop();
        delay(200);
        esp_wifi_deinit();
    }
    
    WiFi.mode(WIFI_OFF);
    delay(2000);
    WiFi.persistent(false);
}

// Mesh UART Message Sender
void sendMeshNotification(const Hit &hit) {
    if (!meshEnabled || millis() - lastMeshSend < MESH_SEND_INTERVAL) return;
    lastMeshSend = millis();
    
    
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             hit.mac[0], hit.mac[1], hit.mac[2], hit.mac[3], hit.mac[4], hit.mac[5]);
    
    String cleanName = "";
    if (strlen(hit.name) > 0 && strcmp(hit.name, "WiFi") != 0) {
        for (size_t i = 0; i < strlen(hit.name) && i < 32; i++) {
            char c = hit.name[i];
            if (c >= 32 && c <= 126) {
                cleanName += c;
            }
        }
    }
    
    char mesh_msg[MAX_MESH_SIZE];
    memset(mesh_msg, 0, sizeof(mesh_msg));
    
    int msg_len;
    if (cleanName.length() > 0) {
        if (tempSensorAvailable) {
            msg_len = snprintf(mesh_msg, sizeof(mesh_msg) - 1,
                              "%s: Target: %s %s RSSI:%d Name:%s Temp:%.1fC",
                              nodeId.c_str(), 
                              hit.isBLE ? "BLE" : "WiFi", 
                              mac_str, 
                              hit.rssi,
                              cleanName.c_str(),
                              ambientTemp);
        } else {
            msg_len = snprintf(mesh_msg, sizeof(mesh_msg) - 1,
                              "%s: Target: %s %s RSSI:%d Name:%s",
                              nodeId.c_str(), 
                              hit.isBLE ? "BLE" : "WiFi", 
                              mac_str, 
                              hit.rssi,
                              cleanName.c_str());
        }
    } else {
        if (tempSensorAvailable) {
            msg_len = snprintf(mesh_msg, sizeof(mesh_msg) - 1,
                              "%s: Target: %s %s RSSI:%d Temp:%.1fC",
                              nodeId.c_str(), 
                              hit.isBLE ? "BLE" : "WiFi", 
                              mac_str, 
                              hit.rssi,
                              ambientTemp);
        } else {
            msg_len = snprintf(mesh_msg, sizeof(mesh_msg) - 1,
                              "%s: Target: %s %s RSSI:%d",
                              nodeId.c_str(), 
                              hit.isBLE ? "BLE" : "WiFi", 
                              mac_str, 
                              hit.rssi);
        }
    }
    
    if (msg_len > 0 && msg_len < MAX_MESH_SIZE) {
        mesh_msg[msg_len] = '\0';
        
        delay(10);
        if (Serial1.availableForWrite() >= msg_len + 2) {
            Serial.printf("[MESH] %s\n", mesh_msg);
            Serial1.println(mesh_msg);
            Serial1.flush();
        }
    }
}

void sendTrackerMeshUpdate() {
    static unsigned long lastTrackerMesh = 0;
    const unsigned long trackerInterval = 15000;

    if (millis() - lastTrackerMesh < trackerInterval) return;
    lastTrackerMesh = millis();

    uint8_t trackerMac[6];
    int8_t trackerRssi;
    uint32_t trackerLastSeen, trackerPackets;
    getTrackerStatus(trackerMac, trackerRssi, trackerLastSeen, trackerPackets);

    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             trackerMac[0], trackerMac[1], trackerMac[2],
             trackerMac[3], trackerMac[4], trackerMac[5]);

    char tracker_msg[MAX_MESH_SIZE];
    uint32_t ago = trackerLastSeen ? (millis() - trackerLastSeen) / 1000 : 999;

    int msg_len = snprintf(tracker_msg, sizeof(tracker_msg),
                          "%s: Tracking: %s RSSI:%ddBm LastSeen:%us Pkts:%u",
                          nodeId.c_str(), mac_str, (int)trackerRssi, ago, (unsigned)trackerPackets);

    if (Serial1.availableForWrite() >= msg_len) {
        Serial.printf("[MESH] %s\n", tracker_msg);
        Serial1.println(tracker_msg);
    }
}

void initializeMesh() {
    Serial1.end();
    delay(100);
  
    Serial1.setRxBufferSize(2048);
    Serial1.setTxBufferSize(1024);
    Serial1.begin(115200, SERIAL_8N1, MESH_RX_PIN, MESH_TX_PIN);
    Serial1.setTimeout(100);
    
    // Clear any garbage data
    delay(100);
    while (Serial1.available()) {
        Serial1.read();
    }
    
    Serial.println("[MESH] UART initialized");
    Serial.printf("[MESH] Config: 115200 8N1 on RX=%d TX=%d\n", MESH_RX_PIN, MESH_TX_PIN);
}

void processCommand(const String &command) {
    if (command.startsWith("CONFIG_BEEPS:")) {
        int beeps = command.substring(13).toInt();
        if (beeps >= 1 && beeps <= 10) {
            cfgBeeps = beeps;
            saveConfiguration();
            Serial.printf("[MESH] Updated beeps config: %d\n", cfgBeeps);
            Serial1.println(nodeId + ": CONFIG_ACK:BEEPS:" + String(cfgBeeps));
        }
    } else if (command.startsWith("CONFIG_GAP:")) {
        int gap = command.substring(11).toInt();
        if (gap >= 20 && gap <= 2000) {
            cfgGapMs = gap;
            saveConfiguration();
            Serial.printf("[MESH] Updated gap config: %d\n", cfgGapMs);
            Serial1.println(nodeId + ": CONFIG_ACK:GAP:" + String(cfgGapMs));
        }
    } else if (command.startsWith("CONFIG_CHANNELS:")) {
        String channels = command.substring(16);
        parseChannelsCSV(channels);
        Serial.printf("[MESH] Updated channels: %s\n", channels.c_str());
        Serial1.println(nodeId + ": CONFIG_ACK:CHANNELS:" + channels);
    } else if (command.startsWith("CONFIG_TARGETS:")) {
        String targets = command.substring(15);
        saveTargetsList(targets);
        Serial.printf("[MESH] Updated targets list\n");
        Serial1.println(nodeId + ": CONFIG_ACK:TARGETS:OK");
    } else if (command.startsWith("SCAN_START:")) {
        String params = command.substring(11);
        int modeDelim = params.indexOf(':');
        int secsDelim = params.indexOf(':', modeDelim + 1);
        int channelDelim = params.indexOf(':', secsDelim + 1);
        
        if (modeDelim > 0 && secsDelim > 0) {
            int mode = params.substring(0, modeDelim).toInt();
            int secs = params.substring(modeDelim + 1, secsDelim).toInt();
            String channels = (channelDelim > 0) ? params.substring(secsDelim + 1, channelDelim) : "1,6,11";
            bool forever = (channelDelim > 0 && params.substring(channelDelim + 1) == "FOREVER");
            
            if (mode >= 0 && mode <= 2) {
                currentScanMode = (ScanMode)mode;
                parseChannelsCSV(channels);
                stopRequested = false;
                
                if (!workerTaskHandle) {
                    xTaskCreatePinnedToCore(listScanTask, "scan", 8192, 
                                          (void*)(intptr_t)(forever ? 0 : secs), 1, &workerTaskHandle, 1);
                }
                Serial.printf("[MESH] Started scan via mesh command\n");
                Serial1.println(nodeId + ": SCAN_ACK:STARTED");
            }
        }
    } else if (command.startsWith("TRACK_START:")) {
        String params = command.substring(12);
        int macDelim = params.indexOf(':');
        int modeDelim = params.indexOf(':', macDelim + 1);
        int secsDelim = params.indexOf(':', modeDelim + 1);
        int channelDelim = params.indexOf(':', secsDelim + 1);
        
        if (macDelim > 0 && modeDelim > 0 && secsDelim > 0) {
            String mac = params.substring(0, macDelim);
            int mode = params.substring(macDelim + 1, modeDelim).toInt();
            int secs = params.substring(modeDelim + 1, secsDelim).toInt();
            String channels = (channelDelim > 0) ? params.substring(secsDelim + 1, channelDelim) : "6";
            bool forever = (channelDelim > 0 && params.indexOf("FOREVER", channelDelim) > 0);
            
            uint8_t trackerMac[6];
            if (parseMac6(mac, trackerMac) && mode >= 0 && mode <= 2) {
                setTrackerMac(trackerMac);
                currentScanMode = (ScanMode)mode;
                parseChannelsCSV(channels);
                stopRequested = false;
                
                if (!workerTaskHandle) {
                    xTaskCreatePinnedToCore(trackerTask, "tracker", 8192, 
                                          (void*)(intptr_t)(forever ? 0 : secs), 1, &workerTaskHandle, 1);
                }
                Serial.printf("[MESH] Started tracker via mesh command for %s\n", mac.c_str());
                Serial1.println(nodeId + ": TRACK_ACK:STARTED:" + mac);
            }
        }
    } else if (command.startsWith("STOP")) {
        stopRequested = true;
        Serial.println("[MESH] Stop command received via mesh");
        Serial1.println(nodeId + ": STOP_ACK:OK");
    } else if (command.startsWith("STATUS")) {
    // Get current status info
    float esp_temp = temperatureRead();
    float esp_temp_f = (esp_temp * 9.0 / 5.0) + 32.0;
    String modeStr = (currentScanMode == SCAN_WIFI) ? "WiFi" : 
                     (currentScanMode == SCAN_BLE) ? "BLE" : "WiFi+BLE";
    
    uint32_t uptime_secs = millis() / 1000;
    uint32_t uptime_mins = uptime_secs / 60;
    uint32_t uptime_hours = uptime_mins / 60;
    
    char status_msg[MAX_MESH_SIZE];
    
    // Include both temperatures if DS18B20 is available
    if (tempSensorAvailable) {
        float ambient_f = (ambientTemp * 9.0 / 5.0) + 32.0;
        snprintf(status_msg, sizeof(status_msg), 
                "%s: STATUS: Mode:%s Scan:%s Hits:%d Targets:%d Unique:%d Ambient:%.1fC/%.1fF ESP:%.1fC/%.1fF Up:%02d:%02d:%02d",
                nodeId.c_str(),
                modeStr.c_str(),
                scanning ? "YES" : "NO",
                totalHits,
                (int)getTargetCount(),
                (int)uniqueMacs.size(),
                ambientTemp, ambient_f,
                esp_temp, esp_temp_f,
                (int)uptime_hours, (int)(uptime_mins % 60), (int)(uptime_secs % 60));
    } else {
        snprintf(status_msg, sizeof(status_msg), 
                "%s: STATUS: Mode:%s Scan:%s Hits:%d Targets:%d Unique:%d ESP:%.1fC/%.1fF Up:%02d:%02d:%02d",
                nodeId.c_str(),
                modeStr.c_str(),
                scanning ? "YES" : "NO",
                totalHits,
                (int)getTargetCount(),
                (int)uniqueMacs.size(),
                esp_temp, esp_temp_f,
                (int)uptime_hours, (int)(uptime_mins % 60), (int)(uptime_secs % 60));
    }
    
    Serial1.println(status_msg);
        
        if (trackerMode) {
            uint8_t trackerMac[6];
            int8_t trackerRssi;
            uint32_t trackerLastSeen, trackerPackets;
            getTrackerStatus(trackerMac, trackerRssi, trackerLastSeen, trackerPackets);
            
            char tracker_status[MAX_MESH_SIZE];
            snprintf(tracker_status, sizeof(tracker_status),
                    "%s: TRACKER: Target:%s RSSI:%ddBm Pkts:%u",
                    nodeId.c_str(),
                    macFmt6(trackerMac).c_str(),
                    (int)trackerRssi,
                    (unsigned)trackerPackets);
            Serial1.println(tracker_status);
        }
        if (gpsValid) {
            char gps_status[MAX_MESH_SIZE];
            snprintf(gps_status, sizeof(gps_status),
                    "%s: GPS: %.6f,%.6f",
                    nodeId.c_str(), gpsLat, gpsLon);
            Serial1.println(gps_status);
        }
    } else if (command.startsWith("BEEP_TEST")) {
        beepPattern(getBeepsPerHit(), getGapMs());
        Serial.println("[MESH] Beep test via mesh");
        Serial1.println(nodeId + ": BEEP_ACK:OK");
    } else if (command.startsWith("VIBRATION_STATUS")) {
        String status = lastVibrationTime > 0 ? 
                      ("Last vibration: " + String(lastVibrationTime) + "ms (" + String((millis() - lastVibrationTime) / 1000) + "s ago)") :
                      "No vibrations detected";
        Serial1.println(nodeId + ": VIBRATION_STATUS: " + status);
    }
}

void sendMeshCommand(const String &command) {
    if (meshEnabled && Serial1.availableForWrite() >= command.length()) {
        Serial.printf("[MESH] Sending command: %s\n", command.c_str());
        Serial1.println(command);
    }
}

void setNodeId(const String &id) {
    nodeId = id;
    prefs.putString("nodeId", nodeId);
    Serial.printf("[MESH] Node ID set to: %s\n", nodeId.c_str());
}

String getNodeId() {
    return nodeId;
}

void processMeshMessage(const String &message) {
    // Skip empty or corrupted messages
    if (message.length() == 0 || message.length() > MAX_MESH_SIZE) {
        return;
    }
    
    // Clean the message - remove non-printable characters
    String cleanMessage = "";
    for (size_t i = 0; i < message.length(); i++) {
        char c = message[i];
        if (c >= 32 && c <= 126) {
            cleanMessage += c;
        }
    }
    
    if (cleanMessage.length() == 0) {
        return;
    }
    
    Serial.printf("[MESH] Processing message: '%s'\n", cleanMessage.c_str());
    
    if (cleanMessage.startsWith("@")) {
        int spaceIndex = cleanMessage.indexOf(' ');
        if (spaceIndex > 0) {
            String targetId = cleanMessage.substring(1, spaceIndex);
            
            if (targetId != nodeId && targetId != "ALL") {
                return;
            }
            String command = cleanMessage.substring(spaceIndex + 1);
            processCommand(command);
        }
    } else {
        processCommand(cleanMessage);
    }
}

void processUSBToMesh() {
    static String usbBuffer = "";
    
    while (Serial.available()) {
        char c = Serial.read();
        Serial.write(c);
        // Only process printable ASCII characters and line endings for mesh
        if ((c >= 32 && c <= 126) || c == '\n' || c == '\r') {
            if (c == '\n' || c == '\r') {
                if (usbBuffer.length() > 5 && usbBuffer.length() <= 240) {  // Mesh 240 char limit
                    Serial.printf("[MESH RX] %s\n", usbBuffer.c_str());
                    processMeshMessage(usbBuffer.c_str());
                } else if (usbBuffer.length() > 0) {
                    Serial.println("[MESH] Ignoring invalid message length");
                }
                usbBuffer = "";
            } else {
                usbBuffer += c;
            }
        } else {
            // ecchooooo
        }
        
        // Prevent buffer overflow at mesh limit
        if (usbBuffer.length() > 240) {
            Serial.println("[MESH] at 240 chars, clearing");
            usbBuffer = "";
        }
    }
}

// void processUSBToMesh() {
//     static String usbBuffer = "";
//     while (Serial.available()) {
//     int ch = Serial.read();  // read a byte (returns -1 if none)
//     if (ch < 0) break;
//     char c = (char)ch;
//     Serial.write((uint8_t)c); // echo the byte back
//     if (c == '\n' || c == '\r' || c == ':') {
//         if (usbBuffer.length() > 0) {
//             // only log/process when we have a complete message
//             Serial.println(usbBuffer);
//             processMeshMessage(usbBuffer);
//             Serial.printf("[MESH RX] %s\n", usbBuffer.c_str());
//             usbBuffer = "";
//         }
//     } else {
//         usbBuffer += c;
//         if (usbBuffer.length() > 2048) {
//             usbBuffer = "";
//         }
//     }
// }
