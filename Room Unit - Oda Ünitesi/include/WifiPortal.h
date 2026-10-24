#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>

// NOT: Bu dosya iki ünitede de birebir aynıdır. Birinde değiştirirseniz
// diğerine de kopyalayın.

// Ev Wi-Fi'sine bağlanır; bağlanamazsa kurulum hotspot'u açar.
// /wifi sayfasından Wi-Fi bilgileri ve yönetici şifresi girilir.
class WifiPortal {
private:
    WebServer* server;
    DNSServer dns;          // Hotspot'ta her adresi kurulum sayfasına yönlendirir
    Preferences prefs;
    const char* hostname;   // http://<hostname>.local
    const char* apName;     // Kurulum hotspot'unun adı

    String ssid;
    String password;
    // Yönetici şifresinin kendisi hiçbir yerde saklanmaz: SHA256(tuz + şifre)
    String adminHash;       // Boşsa henüz kurulum yapılmamıştır
    String adminSalt;

    // Son bağlanılan Wi-Fi kanalı (NVS "chan"). Modem/repeater kapanınca kurulum ağı
    // bu kanalda açılır: ESP-NOW aynı kanalda kalır, kombiyle haberleşme kopmaz.
    uint8_t lastChannel;

    bool apActive;
    bool mdnsStarted;
    unsigned long lastRetryTime;
    unsigned long disconnectedSince;
    const unsigned long CONNECT_TIMEOUT = 10000;   // Açılışta bağlantı bekleme
    const unsigned long RETRY_INTERVAL = 30000;    // Kopunca tekrar deneme
    const unsigned long AP_FALLBACK_DELAY = 60000; // Bu kadar kopuk kalırsa hotspot aç

    void startAP();
    void stopAP();
    void connectSta();
    void onConnected();
    void handleWifiPage();
    void handleWifiSave();
    void handleNotFound();
    bool checkAdminHeader();

    static String hashPassword(const String& salt, const String& password);
    static bool constantTimeEquals(const String& a, const String& b);

public:
    WifiPortal(WebServer* server, const char* hostname, const char* apName);

    void begin();   // setup() içinde, ESP-NOW'dan ÖNCE çağrılmalı
    void handle();  // loop() içinde sürekli

    // Ayar değiştiren her sayfa bunu çağırır; false dönerse şifre istenmiştir
    bool requireAdmin();

    bool isConnected();
    bool isApActive();
    String address();  // Ekranda/seri portta göstermek için
    static const char* setupPassword(); // secrets.h'teki kurulum ağı şifresi

    // Sayfalar arasında ortak HTML parçaları
    static String pageHeader(const char* title);
    static String pageFooter();
    static String htmlEscape(const String& text);
};
