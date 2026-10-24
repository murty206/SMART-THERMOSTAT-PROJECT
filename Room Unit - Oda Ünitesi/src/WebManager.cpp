#include "WebManager.h"

WebManager::WebManager(WebServer* server, WifiPortal* portal, ThermostatController* thermostat,
                       UIManager* ui, HistoryLog* history, SettingsLog* settingsLog) {
    this->server = server;
    this->portal = portal;
    this->thermostat = thermostat;
    this->ui = ui;
    this->history = history;
    this->settingsLog = settingsLog;
}

void WebManager::begin() {
    server->on("/", HTTP_GET, [this]() { handleRoot(); });
    server->on("/ayarlar", HTTP_GET, [this]() { handleSettingsPage(); });
    server->on("/giris", HTTP_GET, [this]() { handleLogin(); });
    server->on("/api/status", HTTP_GET, [this]() { handleStatus(); });
    server->on("/api/history", HTTP_GET, [this]() { handleHistory(); });
    server->on("/api/target", HTTP_POST, [this]() { handleTarget(); });
    server->on("/settings", HTTP_POST, [this]() { handleSettings(); });
    server->on("/calibrate", HTTP_POST, [this]() { handleCalibrate(); });
    server->on("/api/events", HTTP_GET, [this]() {
        server->send(200, "application/json", settingsLog->toJson());
    });
}

static const char* modeName(OperatingMode mode) {
    if (mode == MODE_SUMMER) return "summer";
    if (mode == MODE_MANUAL) return "manual";
    return "auto";
}

// Sayfalara özgü stiller (ortak stiller WifiPortal::pageHeader içinde)
static const char PAGE_STYLE[] = R"rawliteral(
<style>
main{position:relative}
.gear{position:absolute;right:16px;top:20px;text-decoration:none}
.hero{display:flex;justify-content:space-between;align-items:baseline;gap:12px}
.tgt{display:flex;align-items:center;justify-content:center;gap:20px;margin:18px 0 4px}
.tgt button{width:56px;height:56px;margin:0;padding:0;font-size:1.6rem;border-radius:28px}
.tgt .val{font-size:1.8rem;font-weight:600;min-width:120px;text-align:center}
.tabs{display:flex;gap:8px;margin-bottom:8px}
.tabs button{margin:0;padding:6px;flex:1;font-weight:500;background:var(--bg);color:var(--text);border:1px solid var(--line)}
.tabs button.sel{background:var(--accent);color:#fff;border-color:var(--accent)}
svg text{fill:var(--muted);font-size:11px}
.legend{font-size:.85rem;margin-top:4px}
.ok{color:#16a34a;font-weight:600}
</style>)rawliteral";

// ==========================================
// DURUM (JSON) - Sayfalar bunu 5 sn'de bir çeker
// ==========================================
void WebManager::handleStatus() {
    char json[384]; // tx sayaçları 10 haneye çıkabilir; 256 sınırda kalıyordu
    char temp[16];
    char rawTemp[16]; // Ofsetsiz, 0.01 çözünürlük: termometreyle karşılaştırıp kalibre etmek için
    if (thermostat->isSensorFault()) {
        strcpy(temp, "null");
        strcpy(rawTemp, "null");
    } else {
        snprintf(temp, sizeof(temp), "%.1f", thermostat->getCurrentTemp());
        snprintf(rawTemp, sizeof(rawTemp), "%.2f", thermostat->getRawTemp());
    }

    snprintf(json, sizeof(json),
             "{\"temp\":%s,\"rawTemp\":%s,\"sensorFault\":%s,\"target\":%.1f,\"hysteresis\":%.1f,"
             "\"offset\":%.1f,\"relay\":%s,\"mode\":\"%s\",\"linkOk\":%s,\"rssi\":%d,"
             "\"tx\":{\"ok\":%u,\"ackFail\":%u,\"sendErr\":%u}}",
             temp, rawTemp,
             thermostat->isSensorFault() ? "true" : "false",
             ui->getNewTargetTemp(),
             thermostat->getHysteresis(),
             thermostat->getTemperatureOffset(),
             thermostat->getRelayState() ? "true" : "false",
             modeName(ui->getOperatingMode()),
             thermostat->isLinkOk() ? "true" : "false",
             (int)WiFi.RSSI(),
             (unsigned)thermostat->getTxOk(), (unsigned)thermostat->getTxAckFail(),
             (unsigned)thermostat->getTxSendErr());
    server->send(200, "application/json", json);
}

// ==========================================
// GEÇMİŞ (JSON)
// ==========================================
void WebManager::handleHistory() {
    uint32_t range = server->hasArg("range") ? server->arg("range").toInt() : 86400;
    if (range != 604800 && range != 2592000) range = 86400; // 24 saat / 7 gün / 30 gün
    server->send(200, "application/json", history->toJson(range, 288, thermostat->getTemperatureOffset()));
}

// ==========================================
// ANA SAYFA: durum, hızlı hedef, grafik
// ==========================================
void WebManager::handleRoot() {
    String html = WifiPortal::pageHeader("Termostat");
    html += PAGE_STYLE;
    html += R"rawliteral(
<a class="gear" href="/ayarlar">&#9881; Ayarlar</a>
<div class="card">
  <div class="hero"><div class="big" id="temp">--</div><div id="relay">--</div></div>
  <div class="muted" id="sub">--</div>
  <div class="tgt">
    <button id="minus" aria-label="Hedefi d&uuml;&#351;&uuml;r">&minus;</button>
    <div class="val" id="target">--</div>
    <button id="plus" aria-label="Hedefi art&#305;r">+</button>
  </div>
  <div class="muted" id="tmsg" style="text-align:center;min-height:1.5em"></div>
</div>
<div class="card">
  <div class="tabs">
    <button data-r="86400" class="sel">24 saat</button>
    <button data-r="604800">7 g&uuml;n</button>
    <button data-r="2592000">30 g&uuml;n</button>
  </div>
  <svg id="chart" viewBox="0 0 600 240" width="100%" role="img" aria-label="S&#305;cakl&#305;k ge&ccedil;mi&#351;i"></svg>
  <div class="muted" id="cmsg"></div>
  <div class="muted legend">&#9472; oda s&#305;cakl&#305;&#287;&#305; &nbsp; &#9476; hedef &nbsp; &#9646; kombi &ccedil;al&#305;&#351;ma oran&#305; &nbsp; &#9617; yaz/manuel modu</div>
</div>
<div class="card"><div class="row"><span>Kombi &uuml;nitesiyle ba&#287;lant&#305;</span><span id="link">--</span></div></div>
<div class="muted" id="upd" style="text-align:center;font-size:.85rem">Ba&#287;lan&#305;yor&hellip;</div>
<script>
const $ = id => document.getElementById(id);
let busy = false, range = 86400, lastOk = 0;

// Sayfanın canlı olduğu görünsün: her başarılı yenilemede saat, 15 sn gelmezse uyarı
function stamp(){
  if (!lastOk) return;
  const u = $('upd'), hh = new Date(lastOk).toLocaleTimeString('tr-TR');
  if ((Date.now() - lastOk) > 15000){ u.textContent = 'Bağlantı yok — son güncelleme ' + hh; u.className = 'err'; }
  else { u.textContent = 'Son güncelleme ' + hh; u.className = 'muted'; }
}

async function refresh(){
  try{
    const s = await (await fetch('/api/status', {signal: AbortSignal.timeout(4000)})).json();
    lastOk = Date.now(); stamp();
    const t = $('temp');
    if (s.sensorFault){ t.textContent = 'Sensör hatası'; t.className = 'big err'; }
    else { t.textContent = s.temp.toFixed(1) + ' °C'; t.className = 'big'; }
    const r = $('relay');
    if (s.mode === 'manual'){ r.textContent = 'Kombi kendi panelinden'; r.className = 'on'; }
    else { r.textContent = s.relay ? '🔥 Yanıyor' : 'Kapalı'; r.className = s.relay ? 'on' : 'muted'; }
    if (!busy) $('target').textContent = s.target.toFixed(1) + ' °C';
    $('sub').textContent = s.mode === 'manual' ? 'Manuel mod' : (s.mode === 'summer' ? 'Yaz modu' : 'Otomatik');
    const l = $('link');
    l.textContent = s.linkOk ? 'Var' : 'Yok';
    l.className = s.linkOk ? 'ok' : 'err';
  }catch(e){}
}

async function nudge(d){
  busy = true; $('tmsg').textContent = '';
  try{
    const res = await fetch('/api/target', {method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:'delta=' + d});
    if (res.status === 401){ location.href = '/giris'; return; }
    const j = await res.json();
    $('target').textContent = j.target.toFixed(1) + ' °C';
    if (j.mode !== 'auto') $('tmsg').textContent = (j.mode === 'manual' ? 'Manuel' : 'Yaz') + ' modunda hedef etkisizdir.';
  }catch(e){ $('tmsg').textContent = 'Gönderilemedi'; }
  busy = false;
}
$('minus').onclick = () => nudge(-0.5);
$('plus').onclick = () => nudge(0.5);

async function loadChart(){
  $('cmsg').textContent = '';
  try{
    const h = await (await fetch('/api/history?range=' + range)).json();
    if (!h.ok){
      $('chart').innerHTML = '';
      $('cmsg').textContent = h.reason === 'saat yok'
        ? 'Saat henüz alınmadı (internet gerekli); kayıt saat gelince başlar.'
        : 'Geçmiş kullanılamıyor.';
      return;
    }
    draw(h);
  }catch(e){ $('cmsg').textContent = 'Geçmiş alınamadı'; }
}

function draw(h){
  const P = h.points, svg = $('chart');
  if (P.length < 2){
    svg.innerHTML = '';
    $('cmsg').textContent = 'Henüz yeterli kayıt yok (10 dakikada bir kayıt alınıyor).';
    return;
  }
  const W = 600, H = 240, L = 34, R = 8, T = 10, B = 22, BAR = 28;
  const t0 = h.now - range, t1 = h.now;
  let lo = Infinity, hi = -Infinity;
  P.forEach(p => { if (p[1] !== null){ lo = Math.min(lo, p[1]); hi = Math.max(hi, p[1]); }
                   lo = Math.min(lo, p[2]); hi = Math.max(hi, p[2]); });
  lo = Math.floor(lo - 0.5); hi = Math.ceil(hi + 0.5);
  const plotH = H - T - B - BAR - 6;
  const x = t => L + (t - t0) / (t1 - t0) * (W - L - R);
  const y = v => T + (hi - v) / (hi - lo) * plotH;
  let g = '';
  const ystep = (hi - lo) > 8 ? 2 : 1;
  for (let v = lo; v <= hi; v += ystep)
    g += `<line x1="${L}" x2="${W-R}" y1="${y(v)}" y2="${y(v)}" stroke="var(--line)"/>` +
         `<text x="${L-4}" y="${y(v)+4}" text-anchor="end">${v}</text>`;
  for (let i = 0; i <= 4; i++){
    const t = t0 + (t1 - t0) * i / 4, d = new Date(t * 1000);
    const lab = range <= 86400 ? d.toLocaleTimeString('tr-TR', {hour:'2-digit', minute:'2-digit'})
                               : d.toLocaleDateString('tr-TR', {day:'numeric', month:'short'});
    g += `<text x="${x(t)}" y="${H-6}" text-anchor="${i==0?'start':i==4?'end':'middle'}">${lab}</text>`;
  }
  const by = H - B - BAR, bw = Math.max(1, (W - L - R) * h.step / (t1 - t0));
  // Yaz / manuel modundaki dilimler gölgeli: o sürede kombinin yanmaması termostatın kararı değil
  P.forEach(p => { if (p[4] === 1 || p[4] === 2)
    g += `<rect x="${x(p[0])}" y="${T}" width="${bw}" height="${by - T}" fill="${p[4] === 1 ? 'var(--muted)' : 'var(--accent)'}" opacity="0.12"/>`; });
  P.forEach(p => { if (p[3] > 0){ const bh = BAR * p[3] / 100;
    g += `<rect x="${x(p[0])}" y="${by + BAR - bh}" width="${bw}" height="${bh}" fill="var(--accent)" opacity="0.35"/>`; }});
  // Kayıt olmayan aralıklarda (elektrik kesintisi vb.) çizgi kopar
  const path = idx => { let d = '', prev = null;
    P.forEach(p => { const v = p[idx];
      if (v === null){ prev = null; return; }
      d += ((prev !== null && p[0] - prev <= h.step * 2) ? 'L' : 'M') + x(p[0]).toFixed(1) + ' ' + y(v).toFixed(1) + ' ';
      prev = p[0]; });
    return d; };
  g += `<path d="${path(2)}" fill="none" stroke="var(--muted)" stroke-width="1.5" stroke-dasharray="5 4"/>`;
  g += `<path d="${path(1)}" fill="none" stroke="var(--accent)" stroke-width="2"/>`;
  svg.innerHTML = g;
}

document.querySelectorAll('.tabs button').forEach(b => b.onclick = () => {
  document.querySelectorAll('.tabs button').forEach(o => o.classList.remove('sel'));
  b.classList.add('sel'); range = +b.dataset.r; loadChart();
});
refresh(); loadChart();
setInterval(refresh, 5000);
setInterval(loadChart, 600000);
setInterval(stamp, 1000);
// Telefonda arka plandaki sekmenin zamanlayıcıları durur: geri dönünce hemen yenile
document.addEventListener('visibilitychange', () => { if (!document.hidden){ refresh(); loadChart(); } });
</script>)rawliteral";
    html += WifiPortal::pageFooter();
    server->send(200, "text/html; charset=utf-8", html);
}

// ==========================================
// AYARLAR SAYFASI (şifreli)
// ==========================================
void WebManager::handleSettingsPage() {
    if (!portal->requireAdmin()) return;
    OperatingMode mode = ui->getOperatingMode();

    String html = WifiPortal::pageHeader("Ayarlar");
    html += PAGE_STYLE;
    html += "<a class='gear' href='/'>&larr; Ana sayfa</a>";
    if (server->hasArg("kaydedildi")) html += "<div class='card ok'>Kaydedildi.</div>";

    html += R"rawliteral(
<div class="card">
  <div class="row"><span>Oda s&#305;cakl&#305;&#287;&#305; (ofsetli)</span><span id="temp">--</span></div>
  <div class="row"><span>Ham sens&ouml;r (ofsetsiz)</span><span id="raw">--</span></div>
  <div class="muted" style="margin-top:8px;font-size:.9rem">Kalibrasyon = termometre &minus; ham de&#287;er.
  Cihaz en az 30 dk &ccedil;al&#305;&#351;m&#305;&#351; ve ekran kapal&#305;yken &ouml;l&ccedil;&uuml;n.</div>
</div>
<form class="card" method="POST" action="/calibrate">
  <label>Termometrenin g&ouml;sterdi&#287;i (&deg;C)
    <input name="ref" id="ref" type="number" step="0.1" min="-10" max="50" required></label>
  <div class="muted" id="calc" style="font-size:.9rem">Hesaplanan ofset: --</div>
  <button type="submit">Kalibre et</button>
</form>
<div class="card">
  <div class="muted">Ayar ge&ccedil;mi&#351;i (hedef, mod, tolerans, kalibrasyon)</div>
  <table id="cal" style="width:100%;border-collapse:collapse;font-size:.85rem;margin-top:6px"></table>
  <div class="muted" id="drift" style="font-size:.9rem;margin-top:6px"></div>
</div>)rawliteral";

    // Sayfa açıldığı andaki değerler (orig_*): yalnızca değiştirilen alan uygulanır
    html += "<form class='card' method='POST' action='/settings'>";
    html += "<input type='hidden' name='orig_hysteresis' value='" + String(thermostat->getHysteresis(), 1) + "'>";
    html += "<input type='hidden' name='orig_offset' value='" + String(thermostat->getTemperatureOffset(), 1) + "'>";
    html += "<input type='hidden' name='orig_mode' value='" + String(modeName(mode)) + "'>";
    html += "<div class='muted'>&Ccedil;al&#305;&#351;ma modu</div>";
    html += "<label><input name='mode' type='radio' value='auto'";
    html += mode == MODE_AUTO ? " checked" : "";
    html += ">Otomatik (termostat karar verir)</label>";
    html += "<label><input name='mode' type='radio' value='summer'";
    html += mode == MODE_SUMMER ? " checked" : "";
    html += ">Yaz (kombi yaln&#305;zca donma tehlikesinde yanar)</label>";
    html += "<label><input name='mode' type='radio' value='manual'";
    html += mode == MODE_MANUAL ? " checked" : "";
    html += ">Manuel (termostat devre d&#305;&#351;&#305;, kombi kendi panelinden)</label>";
    html += "<label>Tolerans &plusmn; (&deg;C)<input name='hysteresis' type='number' min='0.1' max='2' step='0.1' value='"
            + String(thermostat->getHysteresis(), 1) + "'></label>";
    html += "<label>Kalibrasyon (&deg;C)<input name='offset' type='number' min='-10' max='10' step='0.1' value='"
            + String(thermostat->getTemperatureOffset(), 1) + "'></label>";
    html += "<button type='submit'>Kaydet</button></form>";
    html += "<p class='muted'><a href='/wifi'>Wi-Fi ayarlar&#305;</a></p>";

    html += R"rawliteral(
<div class="muted" id="upd" style="text-align:center;font-size:.85rem">Ba&#287;lan&#305;yor&hellip;</div>
<script>
let lastOk = 0;
function stamp(){
  if (!lastOk) return;
  const u = document.getElementById('upd'), hh = new Date(lastOk).toLocaleTimeString('tr-TR');
  if ((Date.now() - lastOk) > 15000){ u.textContent = 'Bağlantı yok — son güncelleme ' + hh; u.className = 'err'; }
  else { u.textContent = 'Son güncelleme ' + hh; u.className = 'muted'; }
}
async function refresh(){
  try{
    const s = await (await fetch('/api/status', {signal: AbortSignal.timeout(4000)})).json();
    lastOk = Date.now(); stamp();
    document.getElementById('temp').textContent = s.temp === null ? 'Sensör hatası' : s.temp.toFixed(1) + ' °C';
    document.getElementById('raw').textContent = s.rawTemp === null ? '--' : s.rawTemp.toFixed(2) + ' °C';
    lastRaw = s.rawTemp; preview();
  }catch(e){}
}
// Kalibrasyon önizlemesi: cihaz kaydederken kendi o anki ham değeriyle yeniden hesaplar
let lastRaw = null;
function preview(){
  const v = parseFloat(document.getElementById('ref').value), c = document.getElementById('calc');
  if (isNaN(v) || lastRaw === null){ c.textContent = 'Hesaplanan ofset: --'; return; }
  const off = Math.round((v - lastRaw) * 10) / 10;
  c.textContent = 'Hesaplanan ofset: ' + (off > 0 ? '+' : '') + off.toFixed(1) + ' °C' +
                  (Math.abs(off) > 10 ? ' (±10 sınırını aşıyor!)' : '');
  c.className = Math.abs(off) > 10 ? 'err' : 'muted';
}
document.getElementById('ref').addEventListener('input', preview);

// Ayar günlüğü: [zaman, tür, eski, yeni, kaynak, ham]. Sensör kayması yalnızca
// termometreyle yapılan ofset satırlarından: sapma = ham − termometre = −ofset
// (elle girilen ofset ölçüm değildir).
const MODES = ['Otomatik', 'Yaz', 'Manuel'];
async function loadCal(){
  try{
    const j = await (await fetch('/api/events')).json();
    const T = document.getElementById('cal'), D = document.getElementById('drift');
    if (!j.entries.length){ T.innerHTML = ''; D.textContent = 'Henüz kayıt yok. Bir ayar değişince burada görünür.'; return; }
    const f = v => (v > 0 ? '+' : '') + v.toFixed(1);
    const val = (k, v) => k === 'mod' ? (MODES[v] || v) : k === 'ofset' ? f(v) : v.toFixed(1);
    const NAMES = {hedef:'Hedef', mod:'Mod', tolerans:'Tolerans', ofset:'Kalibrasyon'};
    let h = '<tr class="muted"><th align="left">Tarih</th><th align="left">Ne</th><th>De&#287;i&#351;iklik</th><th>Kaynak</th></tr>';
    j.entries.slice().reverse().slice(0, 60).forEach(e => {
      const d = e[0] ? new Date(e[0] * 1000).toLocaleString('tr-TR', {day:'numeric', month:'short', hour:'2-digit', minute:'2-digit'}) : 'saat yok';
      let ch = val(e[1], e[2]) + ' &rarr; ' + val(e[1], e[3]);
      if (e[1] === 'ofset' && e[5] !== null) ch += `<br><span class="muted">ham ${e[5].toFixed(2)}, termometre ${(e[5] + e[3]).toFixed(1)}</span>`;
      h += `<tr style="border-top:1px solid var(--line)"><td>${d}</td><td>${NAMES[e[1]] || e[1]}</td>` +
           `<td align="center">${ch}</td><td align="center">${e[4]}</td></tr>`;
    });
    T.innerHTML = h;
    const R = j.entries.filter(e => e[1] === 'ofset' && e[4] === 'termometre' && e[0] && e[5] !== null);
    if (R.length < 2){ D.textContent = 'Kayma için termometreyle en az iki kalibrasyon gerekir.'; return; }
    const a = R[0], b = R[R.length - 1], days = (b[0] - a[0]) / 86400;
    const shift = a[3] - b[3]; // Sensörün fazladan okuduğu miktardaki değişim
    D.textContent = `Sensör kayması: ${f(shift)} °C, ${days.toFixed(0)} günde` +
      (days >= 7 ? ` (${f(shift / days * 30)} °C/ay)` : '') +
      '. Artı: sensör zamanla daha yüksek okuyor.';
  }catch(e){}
}
loadCal();
refresh(); setInterval(refresh, 5000); setInterval(stamp, 1000);
document.addEventListener('visibilitychange', () => { if (!document.hidden) refresh(); });
</script>)rawliteral";
    html += WifiPortal::pageFooter();
    server->send(200, "text/html; charset=utf-8", html);
}

// Hızlı hedef düğmeleri şifre isterse buraya gelinir: tarayıcı şifreyi sorar,
// sonra aynı oturumda sonraki isteklere kendiliğinden ekler.
void WebManager::handleLogin() {
    if (!portal->requireAdmin()) return;
    server->sendHeader("Location", "/");
    server->send(303, "text/plain", "");
}

// ==========================================
// HIZLI HEDEF (− / +)
// ==========================================
void WebManager::handleTarget() {
    if (!portal->requireAdmin()) return;
    if (!server->hasArg("delta")) {
        server->send(400, "text/plain", "delta eksik");
        return;
    }
    float delta = constrain(server->arg("delta").toFloat(), -2.0, 2.0);
    settingsLog->markWeb();
    ui->applyRemoteSettings(ui->getNewTargetTemp() + delta, thermostat->getHysteresis(),
                            thermostat->getTemperatureOffset(), ui->getOperatingMode());

    char json[64];
    snprintf(json, sizeof(json), "{\"target\":%.1f,\"mode\":\"%s\"}",
             ui->getNewTargetTemp(), modeName(ui->getOperatingMode()));
    server->send(200, "application/json", json);
}

// ==========================================
// AYAR KAYDETME
// ==========================================

// Alan gönderilmediyse ya da sayfa açıldığındaki değeriyle (orig_<ad>) aynıysa
// kullanıcı onu değiştirmemiştir: cihazdaki GÜNCEL değer korunur.
// (toFloat() boş metne 0 döndüğü için "gönderilmedi" ayrıca kontrol edilir.)
float WebManager::editedFloat(const char* name, float current) {
    if (!server->hasArg(name)) return current;
    float submitted = server->arg(name).toFloat();
    String origName = String("orig_") + name;
    if (server->hasArg(origName) && fabs(submitted - server->arg(origName).toFloat()) < 0.05) {
        return current;
    }
    return submitted;
}

void WebManager::handleSettings() {
    if (!portal->requireAdmin()) return;

    float target = editedFloat("target", ui->getNewTargetTemp());
    float hysteresis = editedFloat("hysteresis", thermostat->getHysteresis());
    float offset = editedFloat("offset", thermostat->getTemperatureOffset());

    OperatingMode mode = ui->getOperatingMode(); // Gönderilmezse/değişmediyse mevcut mod korunur
    bool modeEdited = server->hasArg("mode") &&
                      !(server->hasArg("orig_mode") && server->arg("mode") == server->arg("orig_mode"));
    if (modeEdited) {
        String m = server->arg("mode");
        if (m == "auto") mode = MODE_AUTO;
        else if (m == "summer") mode = MODE_SUMMER;
        else if (m == "manual") mode = MODE_MANUAL;
    }

    settingsLog->markWeb();
    ui->applyRemoteSettings(target, hysteresis, offset, mode);
    Serial.println("SISTEM BILGISI: Ayarlar web arayuzunden guncellendi.");

    server->sendHeader("Location", "/ayarlar?kaydedildi=1");
    server->send(303, "text/plain", "");
}

// ==========================================
// KALİBRASYON: termometre değerinden ofset
// ==========================================
void WebManager::handleCalibrate() {
    if (!portal->requireAdmin()) return;

    if (thermostat->isSensorFault()) {
        server->send(409, "text/plain; charset=utf-8", "Sensor okunamiyor, kalibrasyon yapilamaz.");
        return;
    }
    if (!server->hasArg("ref") || server->arg("ref").length() == 0) {
        server->send(400, "text/plain; charset=utf-8", "Termometre degeri eksik.");
        return;
    }

    // Sayfadaki önizleme birkaç saniye eski olabilir: cihaz kendi o anki ham değerini kullanır
    float ref = server->arg("ref").toFloat();
    float raw = thermostat->getRawTemp();
    float offset = roundf((ref - raw) * 10.0f) / 10.0f;
    if (offset < -10.0f || offset > 10.0f) {
        server->send(400, "text/plain; charset=utf-8",
                     "Hesaplanan ofset +-10 derece sinirinin disinda; sensorun yerini kontrol edin.");
        return;
    }

    settingsLog->markReference(); // Ana döngü bu ofset değişikliğini "termometre" diye günlüğe yazar
    ui->applyRemoteSettings(ui->getNewTargetTemp(), thermostat->getHysteresis(), offset,
                            ui->getOperatingMode());
    Serial.printf("[KAL] termometre=%.1f ham=%.2f -> ofset=%.1f\n", ref, raw, offset);

    server->sendHeader("Location", "/ayarlar?kaydedildi=1");
    server->send(303, "text/plain", "");
}
