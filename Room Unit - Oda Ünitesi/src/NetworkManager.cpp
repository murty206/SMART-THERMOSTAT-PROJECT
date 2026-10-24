#include "NetworkManager.h"

// Kurucu Fonksiyon: Parametre olarak gelen adresi, sınıfın gizli değişkenine kopyalar
NetworkManager::NetworkManager(uint8_t* macAddress) {
    memcpy(this->targetMac, macAddress, 6);
}

bool NetworkManager::initESPNow() {
    // Wi-Fi modunu WifiPortal ayarlar; burada sıfırlanırsa modem bağlantısı kopar
    if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) return false;

    // Kombi ünitesi yalnızca bu adresten gelen paketleri kabul eder
    Serial.print("Oda Cihazi MAC Adresi: ");
    Serial.println(WiFi.macAddress());

    // Gölgeleme (Shadowing) hatası çözüldü. Sadece yerel değişken kullanıyoruz.
    esp_now_peer_info_t peerInfo = {}; 
    
    // Sınıfın sırrı olan targetMac adresini eşleştirme yapısına kopyala
    memcpy(peerInfo.peer_addr, this->targetMac, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false; 

    if (esp_now_add_peer(&peerInfo) != ESP_OK) return false;
    return true;
}

bool NetworkManager::sendData(DataPacket packet) {
    // Sınıfın içindeki adrese paketi fırlat
    esp_err_t result = esp_now_send(this->targetMac, (uint8_t *) &packet, sizeof(packet));
    return (result == ESP_OK);
}