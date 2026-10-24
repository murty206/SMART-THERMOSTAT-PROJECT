#include "BoilerController.h"
#include "driver/gpio.h"

// 1. YAPICI METOT (Constructor)
// Nesne global olduğu için bu kod setup()'tan ÖNCE çalışır: pini kodun
// ulaşabildiği en erken anda LOW'a sabitler. Bootloader aralığını (kodumuz
// henüz çalışmazken) yalnızca karttaki harici pull-down direnci korur.
BoilerController::BoilerController(uint8_t pin) {
    this->relayPin = pin;
    this->currentRelayState = false; // Başlangıçta kombi her zaman kapalı olmalı
    this->lastHeartbeatTime = 0;
    forcePinLow();
}

// Pini LOW çıkış yapar ve dahili pull-down'ı da açar (harici dirence ek katman).
// Arduino'nun pinMode(OUTPUT) çağrısı pull dirençlerini kapattığı için
// doğrudan ESP-IDF GPIO fonksiyonları kullanılır; bu aşamada Arduino
// katmanı henüz kurulmamış da olabilir. digitalWrite() pinMode'suz çalışır.
void BoilerController::forcePinLow() {
    gpio_num_t gpio = (gpio_num_t)relayPin;
    gpio_set_level(gpio, 0);                  // Önce seviye, sonra yön: çıkış olduğu an LOW
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    gpio_pullup_dis(gpio);
    gpio_pulldown_en(gpio);
}

// 2. DONANIMI BAŞLATMA (Active-HIGH: transistör HIGH ile iletime geçer)
void BoilerController::begin() {
    forcePinLow(); // Transistörü kapalı başlat (Röle Durur)
    Serial.println("BoilerController: Donanim baslatildi. Role KAPALI.");
    feedWatchdog(); 
}

// 3. RÖLE KONTROLÜ (Active-HIGH: transistör HIGH ile iletime geçer)
void BoilerController::setRelay(bool state) {
    if (this->currentRelayState != state) {
        this->currentRelayState = state;
        digitalWrite(relayPin, state ? HIGH : LOW); // HIGH = YANAR, LOW = DURUR
        Serial.print("BoilerController: Kombi Durumu -> ");
        Serial.println(state ? "YANIYOR [ON]" : "DURDU [OFF]");
    }
}

// 4. BEKÇİ KÖPEĞİNİ BESLEME (Zaman Damgasını Güncelleme)
void BoilerController::feedWatchdog() {
    // Oda ünitesinden her paket geldiğinde bu fonksiyon çağrılacak
    this->lastHeartbeatTime = millis(); 
}

// 5. GÜVENLİK DENETÇİSİ (Sürekli saate bakan fonksiyon)
void BoilerController::checkWatchdog() {
    unsigned long currentMillis = millis();
    
    // Eğer şu anki zaman ile son haberleşme arasındaki fark 5 dakikayı (WATCHDOG_TIMEOUT) geçtiyse...
    if (currentMillis - lastHeartbeatTime > WATCHDOG_TIMEOUT) {
        
        // Ve eğer kombi o an YANIYORSA...
        if (currentRelayState == true) {
            Serial.println("KRITIK ALARM: 5 dakikadir Oda Unitesinden haber alinamiyor!");
            Serial.println("KRITIK ALARM: Iletisim koptu. Watchdog kombiyi ZORLA KAPATIYOR!");
            
            setRelay(false); // Evi fırına çevirmemek için sistemi acil durdur!
        }
    }
}

// 6. DURUM BİLGİSİ (Web arayüzü için)
bool BoilerController::getRelayState() { return currentRelayState; }
unsigned long BoilerController::getMillisSinceHeartbeat() { return millis() - lastHeartbeatTime; }
unsigned long BoilerController::getWatchdogTimeout() { return WATCHDOG_TIMEOUT; }
