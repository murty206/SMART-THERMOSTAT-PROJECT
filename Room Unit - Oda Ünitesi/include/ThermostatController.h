#pragma once
#include <Arduino.h>
#include "Protocol.h"

class ThermostatController {
private:
    // Kasanın İçindeki Sırlar (State Variables)
    float rawTemperature;      // Sensörden gelen HAM değer (ofsetsiz)
    float currentTemperature;  // Ofset uygulanmış değer
    float targetTemperature;
    float hysteresisBand;
    bool isRelayActive;
    bool stateChanged;

    // Kalibrasyon Ofseti (Dışarıdan doğrudan erişilemez)
    float temperatureOffset;

    // Sensör Arıza Durumu: Geçersiz okuma gelirse kombi ZORLA kapatılır
    bool sensorFault;
    const float MIN_VALID_TEMP = -20.0;
    const float MAX_VALID_TEMP = 60.0;

    // Manuel mod: termostat devre dışı, röle sürekli kapalı kontak (kombi kendi panelinden)
    bool manualMode;

    // Zaman Yönetimi
    unsigned long lastHeartbeatTime;
    const unsigned long HEARTBEAT_INTERVAL = 60000;
    const unsigned long MIN_SEND_GAP = 250; // Değişiklik paketleri arası en kısa süre (ms)

    // Teslim edilemeyen paket için tekrar deneme
    bool retryPending;
    bool lastDeliveryOk;   // Açılıştan beri en az bir paket teslim edildi mi?
    uint8_t consecutiveFailures; // Arka arkaya teslim edilemeyen paket sayısı
    const uint8_t LINK_FAIL_LIMIT = 3; // Bu kadar ardışık kayıpta "bağlantı yok" (~4-6 sn)

    // Teşhis sayaçları (açılıştan beri): /api/status'ta görünür
    uint32_t txOk;        // Kombi MAC katmanında onayladı
    uint32_t txAckFail;   // Gönderildi, onay gelmedi
    uint32_t txSendErr;   // esp_now_send hiç başlatamadı
    unsigned long retryStartTime;
    const unsigned long RETRY_INTERVAL = 2000;

    // İç Hesaplama Fonksiyonu
    void evaluateTemperature();

public:
    // Kurucu Fonksiyon
    ThermostatController(float initialTarget, float hysteresis);

    // Dış Dünyadan Veri Alma Kapıları
    void updateCurrentTemperature(float sensorTemp); // NAN = sensör okunamadı
    void setTargetTemperature(float newTarget);

    // Histerezis (Tolerans) Ayar Kapıları
    void setHysteresis(float newHysteresis);
    float getHysteresis();

    // Kalibrasyon (Ofset) Ayar Kapıları
    void setTemperatureOffset(float offset);
    float getTemperatureOffset();
    float getCurrentTemp(); // Ofset uygulanmış gerçek sıcaklığı okumak için
    float getRawTemp();     // Ofsetsiz ham sensör değeri (kalibrasyon ölçümü için)
    bool isSensorFault();

    // Manuel mod kapısı (her döngüde çağrılabilir; yalnızca değişince işlem yapar)
    void setManualMode(bool enabled);

    // Dış Dünyaya Veri ve Emir Verme Kapıları
    bool getRelayState();
    bool requiresNetworkUpdate();
    void reportDeliveryResult(bool delivered, bool sendError = false); // ESP-NOW teslim sonucu
    uint32_t getTxOk();
    uint32_t getTxAckFail();
    uint32_t getTxSendErr();
    bool isLinkOk();
    DataPacket generatePacket();
};
