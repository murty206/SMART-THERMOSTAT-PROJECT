#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include "WifiPortal.h"
#include "ThermostatController.h"
#include "UIManager.h"
#include "HistoryLog.h"
#include "SettingsLog.h"

// Oda ünitesinin web arayüzü:
//   GET  /              Durum, hızlı hedef (− / +) ve geçmiş grafiği (herkese açık)
//   GET  /ayarlar       Mod, tolerans, kalibrasyon (yönetici şifresi ister)
//   GET  /giris         Şifreyi bir kez sorup ana sayfaya döner (hızlı hedef için)
//   GET  /api/status    Durum (JSON)
//   GET  /api/history   Geçmiş (JSON), ?range=86400|604800|2592000 saniye
//   POST /api/target    Hedefi delta kadar değiştir (şifre ister)
//   POST /settings      Ayarları kaydet (şifre ister)
//   POST /calibrate     ref=<termometre> → ofset = ref − ham (şifre ister)
//   GET  /api/events    Ayar günlüğü: hedef, mod, tolerans, ofset değişiklikleri (JSON)
class WebManager {
private:
    WebServer* server;
    WifiPortal* portal;
    ThermostatController* thermostat;
    UIManager* ui;
    HistoryLog* history;
    SettingsLog* settingsLog;

    void handleRoot();
    void handleSettingsPage();
    void handleLogin();
    void handleStatus();
    void handleHistory();
    void handleTarget();
    void handleSettings();
    void handleCalibrate(); // Termometre değerinden ofset hesaplar
    float editedFloat(const char* name, float current); // Yalnızca değiştirilen alanı uygular

public:
    WebManager(WebServer* server, WifiPortal* portal, ThermostatController* thermostat,
               UIManager* ui, HistoryLog* history, SettingsLog* settingsLog);
    void begin(); // Sayfaları sunucuya kaydeder (portal.begin()'den önce)
};
