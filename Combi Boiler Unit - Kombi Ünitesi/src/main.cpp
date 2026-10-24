#include <Arduino.h>
#include <esp_now.h>
#include "Protocol.h"
#include "BoilerController.h"
#include "NetworkManager.h"
#include "WifiPortal.h"
#include "WebManager.h"

// ==========================================
// 1. DONANIM PİNLERİ
// ==========================================
#define RELAY_PIN 26 // BC546 Transistörünün Base bacağına giden pin (Kendi tasarımınıza göre güncelleyin)

// Sadece bu adresten gelen paketler kabul edilir. Oda ünitesi açılışta
// "Oda Cihazi MAC Adresi" satırını seri porta yazar; o değeri buraya girin.
// Hepsi 0x00 bırakılırsa filtre KAPALI olur (her gönderen kabul edilir).
uint8_t odaMacAddress[] = {0x78, 0xE3, 0x6D, 0x64, 0x1C, 0xC4}; // 78:E3:6D:64:1C:C4 (2026-10-19, açılış logu + esptool)

// ==========================================
// 2. NESNELER
// ==========================================
BoilerController boiler(RELAY_PIN);
NetworkManager network;

// ==========================================
// 3. GLOBAL HABERLEŞME DEĞİŞKENLERİ (YENİ)
// ==========================================
volatile bool newPacketReceived = false;
DataPacket lastReceivedPacket;
portMUX_TYPE packetMux = portMUX_INITIALIZER_UNLOCKED; // Wi-Fi görevi ile loop arasında kilit
bool macFilterEnabled = false;

// Web arayüzü için son paketin kilit dışı kopyası (sadece loop() yazar)
DataPacket webPacket;
bool packetSeen = false;

WebServer server(80);
WifiPortal portal(&server, "kombi", "Kombi-Kurulum");
WebManager web(&server, &boiler, &webPacket, &packetSeen, &macFilterEnabled);

// ==========================================
// 4. ESP-NOW ALICI KESMESİ (Sadece Hamallık Yapar)
// ==========================================
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
    // Yabancı bir ESP-NOW cihazı kombiyi açıp kapatamasın
    if (macFilterEnabled && memcmp(mac, odaMacAddress, 6) != 0) return;

    if (len == sizeof(DataPacket)) {
        // SADECE veriyi güvenli bölgeye kopyala ve bayrağı kaldır.
        // ASLA donanım tetikleme veya Serial.print yapma!
        portENTER_CRITICAL(&packetMux);
        memcpy((void*)&lastReceivedPacket, incomingData, sizeof(DataPacket));
        newPacketReceived = true;
        portEXIT_CRITICAL(&packetMux);
    }
}

// ==========================================
// 4. SETUP VE LOOP
// ==========================================
void setup() {
    Serial.begin(115200);
    
    // 1. Kombi donanımını ve güvenliğini başlat
    boiler.begin(); 
    
    // 2. Gönderen filtresi: adres girilmişse aç
    for (int i = 0; i < 6; i++) {
        if (odaMacAddress[i] != 0x00) macFilterEnabled = true;
    }
    if (!macFilterEnabled) {
        Serial.println("UYARI: Oda MAC adresi girilmemis, gonderen filtresi KAPALI!");
    }

    // 3. Wi-Fi ve web arayüzü (ESP-NOW'dan ÖNCE: ESP-NOW modemin kanalını kullanır)
    web.begin();
    portal.begin();

    // 4. ESP-NOW ağını başlat
    if (network.initESPNow()) {
        // Ağ başarılıysa, Alıcı Kesmesini (OnDataRecv) ESP-NOW çekirdeğine kaydet
        esp_now_register_recv_cb(OnDataRecv);
    }
}

void loop() {
    // Web arayüzü isteklerini karşıla
    portal.handle();

    // Kombi ünitesi sürekli bekçi köpeğini kontrol eder
    boiler.checkWatchdog();
    
    // Eğer Wi-Fi kesmesi yeni bir paket bıraktıysa, ağır işleri burada güvenle yap
    if (newPacketReceived) {
        // Paketi kilit altında yerel kopyaya al; okurken üzerine yazılmasın
        portENTER_CRITICAL(&packetMux);
        DataPacket packet = lastReceivedPacket;
        newPacketReceived = false; // İşleme başladık, bayrağı indir
        portEXIT_CRITICAL(&packetMux);

        boiler.feedWatchdog();
        boiler.setRelay(packet.isRelayActive);
        webPacket = packet;
        packetSeen = true;

        Serial.printf("Paket Alindi! Hedef: %.1f, Mevcut: %.1f, Role Emri: %d\n",
                      packet.targetTemperature,
                      packet.currentTemperature,
                      packet.isRelayActive);
    }
}