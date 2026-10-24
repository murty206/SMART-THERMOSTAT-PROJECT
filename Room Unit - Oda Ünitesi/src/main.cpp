#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>

#include "NetworkManager.h"
#include "ThermostatController.h"
#include "ButtonManager.h"
#include "UIManager.h" // Arayüz Yöneticimiz
#include "SettingsManager.h"
#include "WifiPortal.h"
#include "WebManager.h"
#include "HistoryLog.h"
#include "SettingsLog.h"
#include <time.h>
#include "driver/gpio.h"

// ==========================================
// 1. DONANIM PİNLERİ VE AYARLAR
// ==========================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define ENCODER_CLK 25
#define ENCODER_DT 26
#define ENCODER_SW 27

// Adafruit_BMP280 kütüphanesi BME280'in sıcaklık/basınç kısmını da okuyabilir,
// yeter ki doğru çip kimliğiyle başlatılsın
#define BME280_CHIPID_COMPAT 0x60

const unsigned long SENSOR_READ_INTERVAL = 2000;
const float DEFAULT_TARGET = 23.0;
const float DEFAULT_HYSTERESIS = 0.5;

// ==========================================
// 2. NESNELER (OBJECTS)
// ==========================================
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
ThermostatController thermostat(DEFAULT_TARGET, DEFAULT_HYSTERESIS);
Adafruit_BMP280 bmp;
uint8_t kombiMacAddress[] = {0x28, 0x05, 0xA5, 0x2D, 0x03, 0x9C};

NetworkManager network(kombiMacAddress);
ButtonManager btn(ENCODER_SW);
UIManager ui(&display, DEFAULT_TARGET, DEFAULT_HYSTERESIS);
SettingsManager settings;
WebServer server(80);
WifiPortal portal(&server, "termostat", "Termostat-Kurulum");
HistoryLog history;
SettingsLog settingsLog;
WebManager web(&server, &portal, &thermostat, &ui, &history, &settingsLog);

// ==========================================
// 3. GLOBAL VE VOLATILE DEĞİŞKENLER
// ==========================================
volatile int encoderTicks = 0;
portMUX_TYPE encoderMux = portMUX_INITIALIZER_UNLOCKED; // ISR ile loop arasında kilit

// Enkoder: tam adım durum makinesi (Ben Buxton). Kontak titreşimi aynı iki komşu
// durum arasında gidip gelir ve hiçbir zaman tam döngü oluşturmaz; yalnızca
// dinlenme → 4 geçerli geçiş → dinlenme sırası bir tık sayılır. Zaman filtresi yok.
// Ölçüm (2026-10-19): eski kodda tek tık 20-119 kenar üretiyordu, aralık < 1 ms.
#define R_START     0x0
#define R_CW_FINAL  0x1
#define R_CW_BEGIN  0x2
#define R_CW_NEXT   0x3
#define R_CCW_BEGIN 0x4
#define R_CCW_FINAL 0x5
#define R_CCW_NEXT  0x6
#define DIR_CW      0x10
#define DIR_CCW     0x20
// DRAM_ATTR: kesme flash erişimi kapalıyken (ör. geçmiş yazılırken) de tabloyu okuyabilsin
static const DRAM_ATTR uint8_t ENC_TABLE[7][4] = {
    {R_START,    R_CW_BEGIN,  R_CCW_BEGIN, R_START},           // R_START
    {R_CW_NEXT,  R_START,     R_CW_FINAL,  R_START | DIR_CW},  // R_CW_FINAL
    {R_CW_NEXT,  R_CW_BEGIN,  R_START,     R_START},           // R_CW_BEGIN
    {R_CW_NEXT,  R_CW_BEGIN,  R_CW_FINAL,  R_START},           // R_CW_NEXT
    {R_CCW_NEXT, R_START,     R_CCW_BEGIN, R_START},           // R_CCW_BEGIN
    {R_CCW_NEXT, R_CCW_FINAL, R_START,     R_START | DIR_CCW}, // R_CCW_FINAL
    {R_CCW_NEXT, R_CCW_FINAL, R_CCW_BEGIN, R_START},           // R_CCW_NEXT
};
volatile uint8_t encState = R_START;

// Enkoder sorun giderme kaydı: kesme yalnızca sayar, seri porta loop() yazar
// (kesme içinden Serial güvenli değil). 0 yapılırsa kayıt kapanır.
#define ENCODER_DEBUG 0
volatile uint32_t encEdges = 0;        // Örneklerde görülen ham değişimler (gürültü dahil)
volatile int32_t encCW = 0, encCCW = 0; // Durum makinesinin saydığı tıklar
unsigned long lastSensorReadTime = 0;
bool sensorReady = false;
uint8_t sensorAddress = 0;

// ESP-NOW gönderim geri bildirimi (Wi-Fi görevinden gelir)
volatile bool deliveryReported = false;
volatile bool deliveryOk = false;

// ==========================================
// ENKODER ÖRNEKLEME (1 kHz zamanlayıcı kesmesi)
// ==========================================
// Ölçüm (2026-10-19): kenar kesmesiyle hızlı çevirmede 200 ms'de 3000-5000 değişim
// (tık başına ~400) ve %25'e varan ters yön sayımı. Pinler zayıf dahili pull-up'la
// bağlı ve ESP32 girişinde histerezis yok: yavaş kenar eşiği yüzlerce kez geçiyor.
// Çözüm: pinleri 1 ms'de bir oku; bir değer ENC_STABLE_SAMPLES ardışık örnekte
// aynı kalırsa geçerli say ve durum makinesine yalnızca bunu ver. Yük sabit 1 kHz.
#define ENC_STABLE_SAMPLES 3   // 3 ms kararlılık; hızlı çevirmede geçişler ~6 ms arayla
hw_timer_t* encTimer = nullptr;
volatile uint8_t encRawPrev = 0x03;     // Son ham örnek (CLK<<1 | DT)
volatile uint8_t encStablePins = 0x03;  // Son kabul edilen kararlı değer
volatile uint8_t encStableCount = 0;
volatile uint32_t encStableChanges = 0; // Kabul edilen geçişler (sorun giderme)

void IRAM_ATTR encoderSampleISR() {
    // CLK önce düşerse (eski kodda "DT yüksekken CLK düştü") saat yönü = +1
    uint8_t pins = (gpio_get_level((gpio_num_t)ENCODER_CLK) << 1) |
                    gpio_get_level((gpio_num_t)ENCODER_DT);
    portENTER_CRITICAL_ISR(&encoderMux);
    if (pins != encRawPrev) {
        encRawPrev = pins;          // Değişti: kararlılık sayımı baştan
        encStableCount = 0;
        encEdges++;                 // Ham değişim (gürültü dahil)
    } else if (encStableCount < ENC_STABLE_SAMPLES) {
        if (++encStableCount == ENC_STABLE_SAMPLES && pins != encStablePins) {
            encStablePins = pins;
            encStableChanges++;
            encState = ENC_TABLE[encState & 0x0F][pins];
            uint8_t dir = encState & 0x30;
            if (dir == DIR_CW) { encoderTicks++; encCW++; }
            else if (dir == DIR_CCW) { encoderTicks--; encCCW++; }
        }
    }
    portEXIT_CRITICAL_ISR(&encoderMux);
}

// ==========================================
// ESP-NOW GÖNDERİM SONUCU (Sadece bayrak kaldırır)
// ==========================================
void OnDataSent(const uint8_t *mac, esp_now_send_status_t status) {
    deliveryOk = (status == ESP_NOW_SEND_SUCCESS);
    deliveryReported = true;
}

// ==========================================
// SENSÖRÜ BUL VE BAŞLAT (BMP280 veya BME280, 0x76 veya 0x77)
// ==========================================
bool initSensor() {
    const uint8_t addresses[] = {0x76, 0x77};
    for (uint8_t addr : addresses) {
        if (bmp.begin(addr, BMP280_CHIPID)) {
            Serial.printf("Sensor: BMP280 bulundu (0x%02X)\n", addr);
            sensorAddress = addr;
            return true;
        }
        if (bmp.begin(addr, BME280_CHIPID_COMPAT)) {
            Serial.printf("Sensor: BME280 bulundu (0x%02X)\n", addr);
            sensorAddress = addr;
            return true;
        }
    }
    Serial.println("HATA: Sicaklik sensoru bulunamadi! Kombi KAPALI tutuluyor.");
    return false;
}

// Kablo koparsa kütüphane hata vermez, rastgele bir sayı döner.
// Bu yüzden okumadan önce sensörün I2C hattında hâlâ cevap verdiği yoklanır.
bool sensorResponds() {
    Wire.beginTransmission(sensorAddress);
    return (Wire.endTransmission() == 0);
}

// ==========================================
// KURULUM (BOOT SEQUENCE)
// ==========================================
void setup() {
    Serial.begin(115200);
    delay(2000); // Voltaj stabilizasyonu

    if(!display.begin(0x3C, true)) {
        Serial.println("HATA: OLED bulunamadi!");
    } else {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(1);
        display.setCursor(0,0);
        display.print("Sistem Basliyor...");
        display.display();
    }

    sensorReady = initSensor();

    // Elektrik kesintisinden önceki ayarları geri yükle
    Settings saved = settings.load(DEFAULT_TARGET, DEFAULT_HYSTERESIS);
    thermostat.setTargetTemperature(saved.targetTemperature);
    thermostat.setHysteresis(saved.hysteresis);
    thermostat.setTemperatureOffset(saved.offset);
    ui.restoreSettings(saved.targetTemperature, saved.hysteresis, saved.offset,
                       (OperatingMode)saved.mode);
    settingsLog.begin({saved.targetTemperature, saved.mode, saved.hysteresis, saved.offset});

    // Sıcaklık geçmişi (DVR). İlk açılışta bölüm bir kez silinir (~15 sn sürebilir)
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Gecmis kaydi");
    display.println("hazirlaniyor...");
    display.display();
    history.begin();

    pinMode(ENCODER_CLK, INPUT_PULLUP);
    pinMode(ENCODER_DT, INPUT_PULLUP);
    btn.begin(); // SW pini pull-up yapıldı

    // Enkoder 1 kHz'de örneklenir (kenar kesmesi gürültüde binlerce kez tetikleniyordu)
    encTimer = timerBegin(0, 80, true);              // 80 MHz / 80 = 1 µs adım
    timerAttachInterrupt(encTimer, &encoderSampleISR, true);
    timerAlarmWrite(encTimer, 1000, true);           // 1000 µs = 1 ms, tekrarlı
    timerAlarmEnable(encTimer);

    // Wi-Fi ESP-NOW'dan ÖNCE kurulur: ESP-NOW, modemin kanalını kullanır
    web.begin();
    portal.begin();
    // Geçmiş kaydı için saat: internetten (NTP), Türkiye saati UTC+3 (yaz saati yok).
    // Bağlantı yoksa SNTP arka planda denemeye devam eder; saat gelene kadar kayıt yapılmaz.
    configTzTime("<+03>-3", "pool.ntp.org", "time.google.com");
    display.clearDisplay();
    display.setCursor(0, 0);
    if (portal.isConnected()) {
        display.println("Web arayuzu:");
        display.println("termostat.local");
        display.println(portal.address());
    } else {
        display.println("Kurulum agi:");
        display.println("Termostat-Kurulum");
        display.print("Sifre: ");
        display.println(WifiPortal::setupPassword());
    }
    display.display();
    delay(3000); // Adresi okumaya fırsat ver; sonra ana ekran çizilir

    if (network.initESPNow()) {
        esp_now_register_send_cb(OnDataSent);
    } else {
        Serial.println("HATA: Ag kurulamadi!");
    }
}

// ==========================================
// ANA DÖNGÜ (ORKESTRA ŞEFİ)
// ==========================================
void loop() {
    // 0. WEB ARAYÜZÜ (gelen istekler UIManager bayraklarını kaldırabilir)
    portal.handle();

    // 1. DONANIM GİRDİLERİNİ TOPLA
    ButtonEvent event = btn.read();
    portENTER_CRITICAL(&encoderMux); // Okuma ile sıfırlama arasında tık kaybolmasın
    int currentTicks = encoderTicks;
    encoderTicks = 0;
    portEXIT_CRITICAL(&encoderMux);

    // 2. GİRDİLERİ ARAYÜZE (DURUM MAKİNESİNE) YÖNLENDİR
    ui.processInput(event, currentTicks);

#if ENCODER_DEBUG
    // 2b. SORUN GİDERME: düğme olayları anında, enkoder özeti 200 ms'de bir
    if (event != BTN_NONE) {
        Serial.printf("[BTN] %s basis\n", event == BTN_SHORT_PRESS ? "kisa" : "uzun");
    }
    static unsigned long lastEncLog = 0;
    if (millis() - lastEncLog >= 200) {
        lastEncLog = millis();
        portENTER_CRITICAL(&encoderMux);
        uint32_t edges = encEdges, stable = encStableChanges;
        int32_t cw = encCW, ccw = encCCW;
        uint8_t state = encState;
        encEdges = 0; encStableChanges = 0; encCW = 0; encCCW = 0;
        portEXIT_CRITICAL(&encoderMux);
        if (edges > 0) {
            // gurultu: 1 ms örneklerde görülen ham değişim; gecis: 3 ms kararlı kalıp kabul edilen
            Serial.printf("[ENC] gurultu:%u gecis:%u tik:+%d/-%d durum:%u CLK=%d DT=%d hedef=%.1f\n",
                          (unsigned)edges, (unsigned)stable, (int)cw, (int)ccw,
                          (unsigned)(state & 0x0F), digitalRead(ENCODER_CLK), digitalRead(ENCODER_DT),
                          ui.getNewTargetTemp());
        }
    }
#endif

    // 3. KULLANICI ARAYÜZDE BİR DEĞERİ DEĞİŞTİRDİYSE YAKALA VE TERMOSTATA BİLDİR
    if (ui.isTargetChanged()) {
        thermostat.setTargetTemperature(ui.getNewTargetTemp());
        ui.clearTargetChangeFlag();
    }

    if (ui.isHysteresisChanged()) {
        thermostat.setHysteresis(ui.getNewHysteresis());
        ui.clearHysteresisChangeFlag();
        Serial.println("SISTEM BILGISI: Tolerans Guncellendi!");
    }

    if (ui.isOffsetChanged()) {
        thermostat.setTemperatureOffset(ui.getNewOffset());
        ui.clearOffsetChangeFlag();
        Serial.println("SISTEM BILGISI: Kalibrasyon Ofseti Guncellendi!");
    }

    // 4. SENSÖR OKUMA VE TERMOSTAT GÜNCELLEME
    unsigned long currentMillis = millis();
    if (currentMillis - lastSensorReadTime >= SENSOR_READ_INTERVAL) {
        lastSensorReadTime = currentMillis;

        // Sensör kopmuşsa her okuma aralığında yeniden bağlanmayı dene
        if (!sensorReady) sensorReady = initSensor();
        if (sensorReady && !sensorResponds()) {
            Serial.println("HATA: Sensor I2C hattinda cevap vermiyor!");
            sensorReady = false;
        }

        float rawTemp = sensorReady ? bmp.readTemperature() : NAN; // Ham değer
        thermostat.updateCurrentTemperature(rawTemp); // Ofset ve arıza kontrolü termostat içinde
        if (thermostat.isSensorFault()) sensorReady = false;

        // Ekrana termostatın hesapladığı OFSETLİ sıcaklığı gönderin
        ui.updateSensorData(thermostat.getCurrentTemp(), thermostat.getRelayState(),
                            thermostat.isSensorFault());

        // Geçmiş: 10 dakikalık ortalamaya ve kombi çalışma oranına katılır.
        // HAM değer yazılır; grafik güncel ofseti ekler (kalibrasyon basamak yaratmaz)
        history.addSample(thermostat.getRawTemp(), thermostat.isSensorFault(),
                          thermostat.getRelayState());
    }

    // 4b. GEÇMİŞ: 10 dakikalık aralık dolduysa kaydı flash'a yaz
    history.handle(ui.getNewTargetTemp(), (uint8_t)ui.getOperatingMode(), thermostat.isLinkOk());

    // 5. YAZ MODU GÜVENLİK KİLİDİ (KILL SWITCH)
    // Hedef 5 dereceye çekilir: kombi yalnızca donma tehlikesinde yanar
    if (ui.getCurrentState() == STATE_SUMMER_MODE) {
        thermostat.setTargetTemperature(5.0);
    }

    // 5b. MANUEL MOD: termostat devre dışı, röle köprü (kombi kendi panelinden).
    // Watchdog aynen çalışır: haberleşme koparsa kombi ünitesi kombiyi kapatır.
    thermostat.setManualMode(ui.getCurrentState() == STATE_MANUAL_MODE);

    // 6. EKRANI GÜNCELLE (Wi-Fi ekranı kombi bağlantısını da gösterir)
    ui.setLinkStatus(thermostat.isLinkOk());
    ui.updateScreen();

    // 7. AYARLARI KALICI HAFIZAYA YAZ (gerekirse, gecikmeli)
    settings.handle({ui.getNewTargetTemp(), thermostat.getHysteresis(),
                     thermostat.getTemperatureOffset(),
                     (uint8_t)ui.getOperatingMode()});

    // 7b. AYAR GÜNLÜĞÜ: enkoder, web ve termometre formu değişiklikleri hepsi buradan
    // görünür; 5 sn durulunca eski → yeni tek satır yazılır
    settingsLog.handle({ui.getNewTargetTemp(), (uint8_t)ui.getOperatingMode(),
                        thermostat.getHysteresis(), thermostat.getTemperatureOffset()},
                       thermostat.getRawTemp(), thermostat.isSensorFault());

    // 8. AĞ İLETİŞİMİ VE KARGO (ESP-NOW)
    if (deliveryReported) {
        deliveryReported = false;
        thermostat.reportDeliveryResult(deliveryOk);
        // Paket havaya çıktı ama kombiden MAC katmanı onayı gelmedi
        if (!deliveryOk) Serial.println("UYARI: Kombi onayi gelmedi, tekrar denenecek.");
    }

    if (thermostat.requiresNetworkUpdate()) {
        DataPacket packet = thermostat.generatePacket();
        if (network.sendData(packet)) {
            Serial.println("Bilgi: Durum Kombi Unitesine gonderildi.");
        } else {
            // esp_now_send hiç kabul etmedi (radyo/kuyruk meşgul): paket havaya çıkmadı
            Serial.println("UYARI: ESP-NOW gonderimi baslatilamadi, tekrar denenecek.");
            thermostat.reportDeliveryResult(false, true);
        }
    }
}
