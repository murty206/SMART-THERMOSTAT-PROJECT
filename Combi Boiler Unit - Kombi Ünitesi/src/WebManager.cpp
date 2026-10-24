#include "WebManager.h"

WebManager::WebManager(WebServer* server, BoilerController* boiler, const DataPacket* lastPacket,
                       const bool* packetSeen, const bool* macFilterEnabled) {
    this->server = server;
    this->boiler = boiler;
    this->lastPacket = lastPacket;
    this->packetSeen = packetSeen;
    this->macFilterEnabled = macFilterEnabled;
}

void WebManager::begin() {
    server->on("/", HTTP_GET, [this]() { handleRoot(); });
    server->on("/api/status", HTTP_GET, [this]() { handleStatus(); });
}

// ==========================================
// DURUM (JSON)
// ==========================================
void WebManager::handleStatus() {
    char json[320];
    char roomTemp[16] = "null";
    char roomTarget[16] = "null";
    long secondsSince = -1; // -1 = açılıştan beri hiç paket gelmedi

    if (*packetSeen) {
        snprintf(roomTemp, sizeof(roomTemp), "%.1f", lastPacket->currentTemperature);
        snprintf(roomTarget, sizeof(roomTarget), "%.1f", lastPacket->targetTemperature);
        secondsSince = boiler->getMillisSinceHeartbeat() / 1000;
    }

    snprintf(json, sizeof(json),
             "{\"relay\":%s,\"secondsSincePacket\":%ld,\"watchdogSeconds\":%lu,"
             "\"roomTemp\":%s,\"roomTarget\":%s,\"manual\":%s,\"macFilter\":%s,\"rssi\":%d,\"channel\":%d}",
             boiler->getRelayState() ? "true" : "false",
             secondsSince,
             boiler->getWatchdogTimeout() / 1000,
             roomTemp, roomTarget,
             (*packetSeen && lastPacket->messageType == 2) ? "true" : "false", // 2 = manuel
             *macFilterEnabled ? "true" : "false",
             (int)WiFi.RSSI(), (int)WiFi.channel());
    server->send(200, "application/json", json);
}

// ==========================================
// ANA SAYFA
// ==========================================
void WebManager::handleRoot() {
    String html = WifiPortal::pageHeader("Kombi");
    html += R"rawliteral(
<div class="card">
  <div class="big" id="relay">--</div>
  <div class="muted">R&ouml;le durumu</div>
  <div style="margin-top:12px">
    <div class="row"><span>Mod</span><span id="mode">--</span></div>
    <div class="row"><span>Son paket</span><span id="age">--</span></div>
    <div class="row"><span>Oda s&#305;cakl&#305;&#287;&#305;</span><span id="temp">--</span></div>
    <div class="row"><span>Oda hedefi</span><span id="target">--</span></div>
    <div class="row"><span>G&uuml;venlik kapatmas&#305;</span><span id="wd">--</span></div>
    <div class="row"><span>G&ouml;nderen filtresi</span><span id="filter">--</span></div>
    <div class="row"><span>Wi-Fi</span><span id="wifi">--</span></div>
  </div>
</div>
<p class="muted">Ayarlar oda &uuml;nitesinden yap&#305;l&#305;r: <a href="http://termostat.local">termostat.local</a>
&middot; <a href="/wifi">Wi-Fi ayarlar&#305;</a></p>
<script>
async function refresh(){
  try{
    const s = await (await fetch('/api/status')).json();
    const r = document.getElementById('relay');
    r.textContent = s.relay ? 'YANIYOR' : 'Kapalı';
    r.className = s.relay ? 'big on' : 'big';
    document.getElementById('mode').textContent =
      s.manual ? 'Manuel (kombi kendi panelinden)' : 'Termostat';
    const a = document.getElementById('age');
    if (s.secondsSincePacket < 0){ a.textContent = 'Hiç gelmedi'; a.className = 'err'; }
    else {
      a.textContent = s.secondsSincePacket + ' sn önce';
      a.className = s.secondsSincePacket > 120 ? 'err' : '';
    }
    document.getElementById('temp').textContent = s.roomTemp === null ? '--' : s.roomTemp.toFixed(1) + ' °C';
    document.getElementById('target').textContent = s.roomTarget === null ? '--' : s.roomTarget.toFixed(1) + ' °C';
    document.getElementById('wd').textContent = (s.watchdogSeconds / 60) + ' dk sessizlikte';
    const f = document.getElementById('filter');
    f.textContent = s.macFilter ? 'Açık' : 'KAPALI';
    f.className = s.macFilter ? '' : 'err';
    document.getElementById('wifi').textContent = s.rssi + ' dBm, kanal ' + s.channel;
  }catch(e){}
}
refresh(); setInterval(refresh, 5000);
</script>)rawliteral";
    html += WifiPortal::pageFooter();
    server->send(200, "text/html; charset=utf-8", html);
}
