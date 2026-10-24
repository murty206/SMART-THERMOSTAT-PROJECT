#include "NetworkManager.h"

NetworkManager::NetworkManager() {
    // Yapıcı metot şimdilik boş
}

bool NetworkManager::initESPNow() {
    // Wi-Fi modunu WifiPortal ayarlar; burada sıfırlanırsa modem bağlantısı kopar
    if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
    
    if (esp_now_init() != ESP_OK) {
        Serial.println("HATA: ESP-NOW baslatilamadi!");
        return false;
    }
    
    Serial.println("NetworkManager: ESP-NOW Alici olarak baslatildi.");
    Serial.print("Kombi Cihazi MAC Adresi: ");
    Serial.println(WiFi.macAddress());
    
    return true;
}