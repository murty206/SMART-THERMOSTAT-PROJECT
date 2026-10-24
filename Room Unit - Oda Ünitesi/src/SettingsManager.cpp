#include "SettingsManager.h"

SettingsManager::SettingsManager() {
    this->dirty = false;
    this->lastChangeTime = 0;
    this->lastSeen = {0.0, 0.0, 0.0, 0};
}

Settings SettingsManager::load(float defaultTarget, float defaultHysteresis) {
    prefs.begin("thermostat", true); // Salt okunur aç
    Settings s;
    s.targetTemperature = prefs.getFloat("target", defaultTarget);
    s.hysteresis        = prefs.getFloat("hyst", defaultHysteresis);
    s.offset            = prefs.getFloat("offset", 0.0);
    s.mode              = prefs.getUChar("mode", 0);
    prefs.end();
    if (s.mode > 2) s.mode = 0; // Tanınmayan mod: otomatik

    // Bozuk/eski bir kayıt sistemi saçma bir değerle başlatmasın
    s.targetTemperature = constrain(s.targetTemperature, 5.0, 35.0);
    s.hysteresis        = constrain(s.hysteresis, 0.1, 2.0);
    s.offset            = constrain(s.offset, -10.0, 10.0);

    lastSeen = s;
    dirty = false;
    Serial.printf("SettingsManager: Hedef %.1f, Tolerans %.1f, Ofset %.1f, Mod %d\n",
                  s.targetTemperature, s.hysteresis, s.offset, s.mode);
    return s;
}

void SettingsManager::handle(const Settings& current) {
    if (current.targetTemperature != lastSeen.targetTemperature ||
        current.hysteresis != lastSeen.hysteresis ||
        current.offset != lastSeen.offset ||
        current.mode != lastSeen.mode) {
        lastSeen = current;
        dirty = true;
        lastChangeTime = millis();
    }

    if (dirty && (millis() - lastChangeTime >= SAVE_DELAY)) {
        save();
        dirty = false;
    }
}

void SettingsManager::save() {
    prefs.begin("thermostat", false);
    prefs.putFloat("target", lastSeen.targetTemperature);
    prefs.putFloat("hyst", lastSeen.hysteresis);
    prefs.putFloat("offset", lastSeen.offset);
    prefs.putUChar("mode", lastSeen.mode);
    prefs.end();
    Serial.println("SettingsManager: Ayarlar kalici hafizaya yazildi.");
}
