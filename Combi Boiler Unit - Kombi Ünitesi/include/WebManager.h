#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include "WifiPortal.h"
#include "BoilerController.h"
#include "Protocol.h"

// Kombi ünitesinin web arayüzü (SALT OKUNUR — karar oda ünitesindedir):
//   GET /            Durum sayfası
//   GET /api/status  Durum (JSON)
class WebManager {
private:
    WebServer* server;
    BoilerController* boiler;
    const DataPacket* lastPacket;  // main.cpp'nin kilit dışı kopyası
    const bool* packetSeen;        // Açılıştan beri hiç paket geldi mi?
    const bool* macFilterEnabled;

    void handleRoot();
    void handleStatus();

public:
    WebManager(WebServer* server, BoilerController* boiler, const DataPacket* lastPacket,
               const bool* packetSeen, const bool* macFilterEnabled);
    void begin();
};
