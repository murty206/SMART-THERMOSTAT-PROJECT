#ifndef BOILER_CONTROLLER_H
#define BOILER_CONTROLLER_H

#include <Arduino.h>

class BoilerController {
private:
    uint8_t relayPin;
    bool currentRelayState;
    
    // Watchdog (Güvenlik) Değişkenleri
    unsigned long lastHeartbeatTime; 
    // Oda ünitesi 60 sn'de bir paket yollar; 5 paket üst üste kaçarsa kombi kapanır
    const unsigned long WATCHDOG_TIMEOUT = 5 * 60 * 1000; // 5 Dakika (milisaniye cinsinden)

    void forcePinLow(); // Röle pinini LOW çıkış + dahili pull-down yapar

public:
    BoilerController(uint8_t pin);
    
    void begin(); // Pin modlarını (OUTPUT) ayarlamak için
    void setRelay(bool state); // Ağdan gelen emri röleye basmak için
    
    void feedWatchdog(); // Yeni paket geldiğinde köpeği beslemek (zamanı sıfırlamak) için
    void checkWatchdog(); // loop() içinde sürekli süreyi kontrol etmek için

    // Web arayüzü için durum bilgisi (salt okunur)
    bool getRelayState();
    unsigned long getMillisSinceHeartbeat();
    unsigned long getWatchdogTimeout();
};

#endif