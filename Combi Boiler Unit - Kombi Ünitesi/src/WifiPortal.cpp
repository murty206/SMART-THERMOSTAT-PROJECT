#include "WifiPortal.h"
#include "mbedtls/md.h"
#include "mbedtls/base64.h"

// Kurulum hotspot'unun şifresi GitHub'a gitmeyen secrets.h dosyasındadır.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "include/secrets.h yok: include/secrets.example.h dosyasini secrets.h adiyla kopyalayip sifreyi girin."
#endif

constexpr bool sameText(const char* a, const char* b) {
    return (*a == *b) && (*a == '\0' || sameText(a + 1, b + 1));
}
static_assert(sizeof(SETUP_AP_PASSWORD) - 1 >= 8,
              "secrets.h: SETUP_AP_PASSWORD en az 8 karakter olmali (WPA2)");
static_assert(!sameText(SETUP_AP_PASSWORD, "DEGISTIR_BENI"),
              "secrets.h: SETUP_AP_PASSWORD hala sablondaki deger, degistirin");

WifiPortal::WifiPortal(WebServer* server, const char* hostname, const char* apName) {
    this->server = server;
    this->hostname = hostname;
    this->apName = apName;
    this->lastChannel = 0;
    this->apActive = false;
    this->mdnsStarted = false;
    this->lastRetryTime = 0;
    this->disconnectedSince = 0;
}

// ==========================================
// BAŞLATMA
// ==========================================
void WifiPortal::begin() {
    prefs.begin("wifi", true);
    ssid = prefs.getString("ssid", "");
    password = prefs.getString("pass", "");
    adminHash = prefs.getString("ahash", "");
    adminSalt = prefs.getString("asalt", "");
    lastChannel = prefs.getUChar("chan", 0);
    prefs.end();

    WiFi.persistent(false);        // Bilgiler zaten kendi NVS alanımızda
    WiFi.setHostname(hostname);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);          // Uyku modu ESP-NOW paketlerini kaçırtır!
    // Çekirdeğin kendi yeniden bağlanması modem yokken arka arkaya tarama yapar ve
    // radyoyu kanal kanal gezdirir (ESP-NOW paketleri kaçar). Yeniden deneme
    // yalnızca handle() içinde, RETRY_INTERVAL aralıkla yapılır.
    WiFi.setAutoReconnect(false);

    if (ssid.length() > 0) {
        Serial.printf("WifiPortal: '%s' agina baglaniliyor...\n", ssid.c_str());
        connectSta();
        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < CONNECT_TIMEOUT) {
            delay(100);
        }
    }

    if (WiFi.status() == WL_CONNECTED) {
        onConnected();
    } else {
        startAP();
    }

    server->on("/wifi", HTTP_GET, [this]() { handleWifiPage(); });
    server->on("/wifi", HTTP_POST, [this]() { handleWifiSave(); });
    server->onNotFound([this]() { handleNotFound(); });
    server->begin();
}

// ==========================================
// ANA DÖNGÜ
// ==========================================
void WifiPortal::handle() {
    if (apActive) dns.processNextRequest();
    server->handleClient();

    if (WiFi.status() == WL_CONNECTED) {
        disconnectedSince = 0;
        if (apActive || !mdnsStarted) onConnected();
        return;
    }

    // Bağlantı yok: belli aralıklarla tekrar dene, uzun sürerse hotspot aç
    unsigned long now = millis();
    if (disconnectedSince == 0) disconnectedSince = now;

    if (!apActive && (now - disconnectedSince >= AP_FALLBACK_DELAY)) {
        startAP();
    }
    if (ssid.length() > 0 && (now - lastRetryTime >= RETRY_INTERVAL)) {
        lastRetryTime = now;
        connectSta();
    }
}

// Bilinen kanaldan başlayarak bağlan: modem oradaysa tarama tek kanalda biter.
// Kanal bilinmiyorsa (0) ya da modem kanal değiştirdiyse tüm kanallar taranır.
void WifiPortal::connectSta() {
    WiFi.begin(ssid.c_str(), password.c_str(), lastChannel);
}

void WifiPortal::onConnected() {
    if (apActive) stopAP();
    uint8_t ch = WiFi.channel();
    if (ch != 0 && ch != lastChannel) { // Yalnızca değişince yaz (NVS yıpranmasın)
        lastChannel = ch;
        prefs.begin("wifi", false);
        prefs.putUChar("chan", ch);
        prefs.end();
    }
    Serial.print("WifiPortal: Baglandi. Adres: http://");
    Serial.print(WiFi.localIP());
    Serial.printf("  (http://%s.local)  Kanal: %d\n", hostname, WiFi.channel());

    if (!mdnsStarted && MDNS.begin(hostname)) {
        MDNS.addService("http", "tcp", 80);
        mdnsStarted = true;
    }
}

void WifiPortal::startAP() {
    WiFi.mode(WIFI_AP_STA); // STA tarafı arka planda modemi denemeye devam eder
    WiFi.setSleep(false);
    // Kanal verilmezse softAP kanal 1'de açılır ve ESP-NOW da kanal 1'e geçer: kombi
    // ünitesi modemin kanalında kaldığı için haberleşme kopar. Son bilinen kanal kullanılır.
    uint8_t ch = (lastChannel >= 1 && lastChannel <= 13) ? lastChannel : 1;
    WiFi.softAP(apName, SETUP_AP_PASSWORD, ch);
    dns.start(53, "*", WiFi.softAPIP());
    apActive = true;
    // Şifre loga yazılmaz: seri port çıktısı bir yere yapıştırılabilir
    Serial.printf("WifiPortal: Kurulum agi acildi: '%s' kanal %u (sifre: secrets.h) -> http://%s/wifi\n",
                  apName, ch, WiFi.softAPIP().toString().c_str());
}

void WifiPortal::stopAP() {
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    apActive = false;
    Serial.println("WifiPortal: Kurulum agi kapatildi.");
}

// ==========================================
// YETKİ KONTROLÜ
// ==========================================
bool WifiPortal::requireAdmin() {
    // İlk kurulumda henüz şifre yok: sadece hotspot'a bağlanan (şifresini bilen) girebilir
    if (adminHash.length() == 0) return true;

    if (!checkAdminHeader()) {
        server->requestAuthentication(BASIC_AUTH, "Termostat");
        return false;
    }
    return true;
}

// "Authorization: Basic base64(admin:sifre)" başlığını çözüp hash'le karşılaştırır.
// WebServer::authenticate() düz şifre istediği için kendi kontrolümüzü yapıyoruz.
bool WifiPortal::checkAdminHeader() {
    String header = server->header("Authorization");
    if (!header.startsWith("Basic ")) return false;
    String encoded = header.substring(6);

    unsigned char decoded[128];
    size_t decodedLen = 0;
    if (mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &decodedLen,
                              (const unsigned char*)encoded.c_str(), encoded.length()) != 0) {
        return false;
    }
    decoded[decodedLen] = '\0';

    String credentials = String((const char*)decoded);
    int colon = credentials.indexOf(':');
    if (colon < 0) return false;
    if (credentials.substring(0, colon) != "admin") return false;

    String candidate = hashPassword(adminSalt, credentials.substring(colon + 1));
    return constantTimeEquals(candidate, adminHash);
}

String WifiPortal::hashPassword(const String& salt, const String& password) {
    String input = salt + password;
    unsigned char digest[32];
    mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
               (const unsigned char*)input.c_str(), input.length(), digest);

    String hex;
    char part[3];
    for (int i = 0; i < 32; i++) {
        snprintf(part, sizeof(part), "%02x", digest[i]);
        hex += part;
    }
    return hex;
}

// Karşılaştırma süresi, şifrenin kaçıncı karakterde yanlış olduğunu ele vermesin
bool WifiPortal::constantTimeEquals(const String& a, const String& b) {
    if (a.length() != b.length()) return false;
    unsigned char diff = 0;
    for (unsigned int i = 0; i < a.length(); i++) diff |= a[i] ^ b[i];
    return diff == 0;
}

// ==========================================
// WI-FI KURULUM SAYFASI
// ==========================================
void WifiPortal::handleWifiPage() {
    if (!requireAdmin()) return;

    int found = WiFi.scanNetworks();   // ~2 sn sürer, döngüyü kısa süre bekletir
    String options;
    for (int i = 0; i < found; i++) {
        options += "<option value=\"" + htmlEscape(WiFi.SSID(i)) + "\">";
    }
    WiFi.scanDelete();

    String html = pageHeader("Wi-Fi Kurulumu");
    html += "<form method='POST' action='/wifi'>";
    html += "<label>Wi-Fi a&#287;&#305;<input name='ssid' list='nets' required value=\"" + htmlEscape(ssid) + "\"></label>";
    html += "<datalist id='nets'>" + options + "</datalist>";
    html += "<label>Wi-Fi &#351;ifresi<input name='pass' type='password'></label>";
    html += "<label>Y&ouml;netici &#351;ifresi (kullan&#305;c&#305;: admin)<input name='admin' type='password' minlength='4'";
    html += adminHash.length() == 0 ? " required" : " placeholder='De&#287;i&#351;meyecekse bo&#351; b&#305;rak&#305;n'";
    html += "></label>";
    html += "<button type='submit'>Kaydet ve yeniden ba&#351;lat</button></form>";
    html += pageFooter();
    server->send(200, "text/html; charset=utf-8", html);
}

void WifiPortal::handleWifiSave() {
    if (!requireAdmin()) return;

    String newSsid = server->arg("ssid");
    String newPass = server->arg("pass");
    String newAdmin = server->arg("admin");
    newSsid.trim();

    if (newSsid.length() == 0) {
        server->send(400, "text/plain; charset=utf-8", "Wi-Fi agi adi bos olamaz.");
        return;
    }
    bool keepOldAdmin = (newAdmin.length() == 0 && adminHash.length() > 0); // Boş = eskisi kalsın
    if (!keepOldAdmin && newAdmin.length() < 4) {
        server->send(400, "text/plain; charset=utf-8", "Yonetici sifresi en az 4 karakter olmali.");
        return;
    }

    prefs.begin("wifi", false);
    prefs.putString("ssid", newSsid);
    prefs.putString("pass", newPass);
    if (!keepOldAdmin) {
        // Her şifre değişikliğinde yeni rastgele tuz
        char salt[17];
        snprintf(salt, sizeof(salt), "%08lx%08lx",
                 (unsigned long)esp_random(), (unsigned long)esp_random());
        prefs.putString("asalt", salt);
        prefs.putString("ahash", hashPassword(String(salt), newAdmin));
    }
    prefs.end();

    String html = pageHeader("Kaydedildi");
    html += "<p>Cihaz yeniden ba&#351;l&#305;yor ve <b>" + htmlEscape(newSsid) + "</b> a&#287;&#305;na ba&#287;lanacak.</p>";
    html += "<p>Birka&ccedil; saniye sonra <b>http://" + String(hostname) + ".local</b> adresini deneyin.</p>";
    html += pageFooter();
    server->send(200, "text/html; charset=utf-8", html);

    delay(1000); // Cevabın tarayıcıya ulaşması için
    ESP.restart();
}

void WifiPortal::handleNotFound() {
    // Hotspot'ta telefonun "ağda oturum aç" ekranı her adresi buraya düşürür
    if (apActive) {
        server->sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/wifi");
        server->send(302, "text/plain", "");
        return;
    }
    server->send(404, "text/plain; charset=utf-8", "Sayfa bulunamadi");
}

// ==========================================
// DURUM
// ==========================================
bool WifiPortal::isConnected() { return WiFi.status() == WL_CONNECTED; }
bool WifiPortal::isApActive() { return apActive; }

const char* WifiPortal::setupPassword() { return SETUP_AP_PASSWORD; }

String WifiPortal::address() {
    if (isConnected()) return WiFi.localIP().toString();
    if (apActive) return String(apName);
    return String("-");
}

// ==========================================
// ORTAK HTML PARÇALARI
// ==========================================
String WifiPortal::pageHeader(const char* title) {
    String html = R"rawliteral(<!DOCTYPE html><html lang="tr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
:root{--bg:#f4f4f2;--card:#fff;--text:#1d1d1b;--muted:#6b6b66;--accent:#c2410c;--line:#ddddd8}
@media (prefers-color-scheme:dark){:root{--bg:#161615;--card:#22221f;--text:#ecece8;--muted:#a3a39c;--accent:#fb923c;--line:#3a3a36}}
body{margin:0;background:var(--bg);color:var(--text);font:16px/1.5 system-ui,sans-serif}
main{max-width:480px;margin:0 auto;padding:16px}
h1{font-size:1.3rem;margin:8px 0 16px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:16px;margin-bottom:16px}
.big{font-size:2.6rem;font-weight:600;line-height:1.1}
.row{display:flex;justify-content:space-between;padding:4px 0;border-bottom:1px solid var(--line)}
.row:last-child{border-bottom:0}
.muted{color:var(--muted)}
.on{color:var(--accent);font-weight:600}
.err{color:#dc2626;font-weight:600}
label{display:block;margin:10px 0;color:var(--muted)}
input{display:block;width:100%;box-sizing:border-box;margin-top:4px;padding:8px;font:inherit;
color:var(--text);background:var(--bg);border:1px solid var(--line);border-radius:6px}
input[type=checkbox],input[type=radio]{display:inline;width:auto;margin-right:8px}
button{margin-top:12px;width:100%;padding:10px;font:inherit;font-weight:600;color:#fff;
background:var(--accent);border:0;border-radius:6px}
a{color:var(--accent)}
</style><title>)rawliteral";
    html += title;
    html += "</title></head><body><main><h1>";
    html += title;
    html += "</h1>";
    return html;
}

String WifiPortal::pageFooter() {
    return "</main></body></html>";
}

String WifiPortal::htmlEscape(const String& text) {
    String out;
    out.reserve(text.length());
    for (unsigned int i = 0; i < text.length(); i++) {
        char c = text[i];
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '"') out += "&quot;";
        else if (c == '\'') out += "&#39;";
        else out += c;
    }
    return out;
}
