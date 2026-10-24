#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "ButtonManager.h" 
#include "Icons.h" 

enum AppState {
    STATE_HOME,             
    STATE_MAIN_MENU,        
    STATE_SUMMER_MODE,      
    STATE_SET_HYSTERESIS,   // DÜZELTİLDİ: Virgül eklendi
    STATE_SET_OFFSET,
    STATE_MANUAL_MODE,      // Termostat devre dışı, kombi kendi panelinden
    STATE_WIFI_INFO         // Wi-Fi ve kombi bağlantı durumu (salt okunur)
};

// Kalıcı hafızada ve web arayüzünde kullanılan çalışma modu
enum OperatingMode {
    MODE_AUTO = 0,    // Termostat karar verir
    MODE_SUMMER = 1,  // Hedef 5 °C: yalnızca donma koruması
    MODE_MANUAL = 2   // Röle sürekli kapalı kontak: kombi kendi panelinden
};

class UIManager {
private:
    Adafruit_SH1106G* display; 
    
    AppState currentState;
    int menuIndex;
    const int MAX_MENU_ITEMS = 5;

    bool linkOk;                    // Kombi bağlantısı (Wi-Fi ekranı için)
    unsigned long lastInfoDraw;     // Wi-Fi ekranı saniyede bir tazelenir
    
    bool needsRedraw;

    // Ekran zaman aşımı: dokunulmazsa ekran kapanır (OLED ömrü + gece karanlığı)
    bool displayOn;
    unsigned long lastActivityTime;
    const unsigned long SCREEN_TIMEOUT = 30000; // 30 sn
    float editBackup; // Onaylanmadan zaman aşımına uğrayan ayar bu değere döner
    void setDisplayPower(bool on);

    float uiTargetTemp;
    float currentEnvTemp;
    bool isRelayActive;
    bool sensorFault;
    bool targetTempChanged; 

    float uiHysteresis;
    bool hysteresisChanged;

    // MİMARİ DÜZELTME: Bu değişkenler private olmak ZORUNDADIR.
    float uiOffset; 
    bool offsetChanged;

    void drawHome();
    void drawMainMenu();
    void drawSummerMode();
    void drawManualMode();
    void drawWifiInfo();
    void drawSetHysteresis(); 
    void drawSetOffset(); // YENİ: Çizim fonksiyonunun deklarasyonu

public:
    UIManager(Adafruit_SH1106G* disp, float initialTarget, float initialHysteresis);

    void processInput(ButtonEvent event, int encoderTicks);
    void updateSensorData(float envTemp, bool relayState, bool sensorFault);
    void setLinkStatus(bool ok); // Kombi bağlantısı (Wi-Fi ekranında gösterilir)
    void restoreSettings(float target, float hysteresis, float offset, OperatingMode mode); // Açılışta NVS'ten
    void updateScreen(); 

    bool isTargetChanged();
    float getNewTargetTemp();
    void clearTargetChangeFlag();
    AppState getCurrentState();
    OperatingMode getOperatingMode(); // Menü/ayar ekranları otomatik moda sayılır

    bool isHysteresisChanged();
    float getNewHysteresis();
    void clearHysteresisChangeFlag();
    
    // YENİ: Kalibrasyon iletişim kapıları (Getter/Setter mantığı)
    bool isOffsetChanged();
    float getNewOffset();
    void clearOffsetChangeFlag();

    // Web arayüzünden gelen ayarlar: enkoderle aynı yoldan termostata ulaşır
    void applyRemoteSettings(float target, float hysteresis, float offset, OperatingMode mode);
};