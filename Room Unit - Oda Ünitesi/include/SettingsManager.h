#pragma once
#include <Arduino.h>
#include <Preferences.h>

// Elektrik kesintisinde kaybolmaması gereken kullanıcı ayarları
struct Settings {
    float targetTemperature;
    float hysteresis;
    float offset;
    uint8_t mode;          // 0: Otomatik, 1: Yaz, 2: Manuel (UIManager.h OperatingMode)
};

class SettingsManager {
private:
    Preferences prefs;
    Settings lastSeen;     // Ana döngüden en son gelen değerler
    bool dirty;            // Flash'a yazılmayı bekleyen değişiklik var mı?
    unsigned long lastChangeTime;

    // Enkoder her tıkta değeri değiştirir; flash'ı yıpratmamak için
    // kullanıcı ayarı bıraktıktan 5 saniye sonra tek seferde yazılır
    const unsigned long SAVE_DELAY = 5000;

    void save();

public:
    SettingsManager();

    // setup() içinde bir kez: kayıtlı ayarları okur, yoksa varsayılanları döner
    Settings load(float defaultTarget, float defaultHysteresis);

    // loop() içinde sürekli: güncel ayarları verir, gerekirse gecikmeli kaydeder
    void handle(const Settings& current);
};
