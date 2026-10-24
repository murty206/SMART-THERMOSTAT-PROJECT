#pragma once
#include <Arduino.h>

// Butonun dış dünyaya verebileceği mutlak tepkiler
enum ButtonEvent {
    BTN_NONE,           // Hiçbir şey olmadı / Bekliyor
    BTN_SHORT_PRESS,    // Menü seçimi / Onay
    BTN_LONG_PRESS      // Yaz moduna geçiş / Geri Dönüş
};

class ButtonManager {
private:
    uint8_t buttonPin;
    
    // Zaman Yönetimi
    unsigned long pressStartTime;
    const unsigned long DEBOUNCE_DELAY = 50;     // 50ms (Titreşimleri yoksay)
    const unsigned long LONG_PRESS_TIME = 1500;  // 1.5 saniye (Uzun basış eşiği)
    
    // Durum Bayrakları (State Flags)
    bool isPressed;
    bool longPressHandled;

public:
    // Kurucu
    ButtonManager(uint8_t pin);
    
    // Pin modlarını ayarlamak için (setup içinde çağrılacak)
    void begin();
    
    // Ana döngüde sürekli olarak butonu dinleyecek fonksiyon
    ButtonEvent read(); 
};