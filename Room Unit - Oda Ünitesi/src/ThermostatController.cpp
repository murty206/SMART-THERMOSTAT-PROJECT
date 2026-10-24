#include "ThermostatController.h"

// 1. Kurucu Fonksiyon (Constructor)
// Sistem ilk açıldığında hedefleri ve başlangıç durumlarını belirleriz.
ThermostatController::ThermostatController(float initialTarget, float hysteresis) {
    targetTemperature = initialTarget;
    hysteresisBand = hysteresis;
    rawTemperature = 0.0;
    currentTemperature = 0.0;
    isRelayActive = false; // Kombi kapalı başlar
    lastHeartbeatTime = 0; // 0 olması, sistem açılır açılmaz ağa ilk paketin fırlatılmasını sağlar
    stateChanged = true;
    this->temperatureOffset = 0.0; // Varsayılan ofset 0
    sensorFault = true;    // İlk geçerli okuma gelene kadar sensöre güvenme
    retryPending = false;
    retryStartTime = 0;
    lastDeliveryOk = false;
    consecutiveFailures = 0;
    txOk = 0;
    txAckFail = 0;
    txSendErr = 0;
    manualMode = false;
}

// 2. Kapsüllenmiş (Gizli) Histerezis Algoritması
void ThermostatController::evaluateTemperature() {
    // MANUEL: Karar kombinin kendi panelinde; röle köprü gibi sürekli kapalı.
    // Sensör kararı devre dışı olduğu için sensör arızası da röleyi etkilemez.
    if (manualMode) {
        isRelayActive = true;
        return;
    }

    // GÜVENLİK: Sensör okunamıyorsa kör karar verme, kombiyi kapat
    if (sensorFault) {
        isRelayActive = false;
        return;
    }

    // Kombiyi açma şartı (Alt Limit)
    if (currentTemperature <= (targetTemperature - hysteresisBand)) {
        isRelayActive = true;
    }
    // Kombiyi kapatma şartı (Üst Limit)
    else if (currentTemperature >= (targetTemperature + hysteresisBand)) {
        isRelayActive = false;
    }
}

// 3. Sensörden Gelen Veriyi Alma Kapısı
void ThermostatController::updateCurrentTemperature(float sensorTemp) {
    bool oldRelayState = isRelayActive; // Eski durumu kaydet

    // NAN veya fiziksel olarak anlamsız bir değer = sensör arızası
    if (isnan(sensorTemp) || sensorTemp < MIN_VALID_TEMP || sensorTemp > MAX_VALID_TEMP) {
        sensorFault = true;
    } else {
        sensorFault = false;
        rawTemperature = sensorTemp;
        currentTemperature = rawTemperature + temperatureOffset;
    }
    evaluateTemperature();

    // Eğer sıcaklık değişimi röleyi açıp/kapatmışsa, anında haber ver!
    if (oldRelayState != isRelayActive) {
        stateChanged = true;
    }
}

void ThermostatController::setTemperatureOffset(float offset) {
    this->temperatureOffset = offset;
    // Ofset değiştiği için mevcut sıcaklık ham değerden yeniden hesaplanır
    currentTemperature = rawTemperature + temperatureOffset;
    evaluateTemperature();
    stateChanged = true;
}
float ThermostatController::getTemperatureOffset() { return temperatureOffset; }
float ThermostatController::getCurrentTemp() { return currentTemperature; }
float ThermostatController::getRawTemp() { return rawTemperature; }
bool ThermostatController::isSensorFault() { return sensorFault; }

void ThermostatController::setManualMode(bool enabled) {
    if (manualMode == enabled) return;
    manualMode = enabled;
    // Manuelden çıkışta histerezis bandının içindeysek röle manuelden kalan
    // "açık" durumunda takılmasın: kapalıdan başlayıp sıcaklığa göre karar ver
    if (!enabled) isRelayActive = false;
    evaluateTemperature();
    stateChanged = true; // Kombiye hemen haber ver
}

// 4. Enkoderden Gelen Hedef Verisini Alma Kapısı
void ThermostatController::setTargetTemperature(float newTemp) {
    float constrainedTemp = constrain(newTemp, 5.0, 35.0);

    if (this->targetTemperature != constrainedTemp) {
        this->targetTemperature = constrainedTemp;
        evaluateTemperature(); // Yeni hedefe göre röle durumunu anında hesapla!
        stateChanged = true;   // Acil gönderim bayrağını kaldır!
    }
}

// 5. Durum Bildirme (Ekran için)
bool ThermostatController::getRelayState() {
    return isRelayActive;
}

// 6. Zaman ve Olay Yönetimi (Orkestra Şefine "Fırlat" emri veren fonksiyon)
bool ThermostatController::requiresNetworkUpdate() {
    unsigned long currentTime = millis();

    // ŞART: 60 saniye dolduysa VEYA acil durum bayrağı kalktıysa
    // VEYA son paket teslim edilemediyse ve tekrar deneme zamanı geldiyse
    bool retryDue = retryPending && (currentTime - retryStartTime >= RETRY_INTERVAL);
    // Hızlı enkoder çevirmede her tık için ayrı paket gönderme: değişiklikler
    // en fazla MIN_SEND_GAP'te bir gider (son durum her zaman iletilir, bayrak kalkık kalır)
    bool changeDue = stateChanged && (currentTime - lastHeartbeatTime >= MIN_SEND_GAP);
    if (currentTime - lastHeartbeatTime >= HEARTBEAT_INTERVAL || changeDue || retryDue) {
        lastHeartbeatTime = currentTime;
        stateChanged = false; // Gönderim izni verildi, bayrağı indir
        retryPending = false;
        return true;
    }

    return false;
}

// 6b. Kombi paketi aldı mı? (ESP-NOW gönderim geri bildirimi)
void ThermostatController::reportDeliveryResult(bool delivered, bool sendError) {
    if (delivered) txOk++;
    else if (sendError) txSendErr++;
    else txAckFail++;

    if (delivered) {
        lastDeliveryOk = true;
        consecutiveFailures = 0;
        retryPending = false;
    } else {
        if (consecutiveFailures < 255) consecutiveFailures++;
        retryPending = true;          // Kısa süre sonra aynı durumu tekrar gönder
        retryStartTime = millis();
    }
}

// Tek paket kaybı "bağlantı yok" sayılmaz: 2 sn'de bir tekrar denendiği için
// ancak arka arkaya LINK_FAIL_LIMIT kayıp gerçek bir kopukluk demektir.
bool ThermostatController::isLinkOk() {
    return lastDeliveryOk && consecutiveFailures < LINK_FAIL_LIMIT;
}
uint32_t ThermostatController::getTxOk() { return txOk; }
uint32_t ThermostatController::getTxAckFail() { return txAckFail; }
uint32_t ThermostatController::getTxSendErr() { return txSendErr; }

// 7. Kargo Paketini Hazırlama
DataPacket ThermostatController::generatePacket() {
    DataPacket packet;
    packet.messageType = manualMode ? 2 : 1; // 1: Termostat kontrolü, 2: Manuel (kombi paneli)
    packet.currentTemperature = currentTemperature;
    packet.targetTemperature = targetTemperature;
    packet.isRelayActive = isRelayActive;
    return packet; // Hazırlanan paketi teslim et
}

void ThermostatController::setHysteresis(float newHysteresis) {
    // Sınırlandırma (Örn: 0.1 ile 2.0 derece arası)
    this->hysteresisBand = constrain(newHysteresis, 0.1, 2.0);
    evaluateTemperature(); // Tolerans değiştiği için röle durumunu anında tekrar hesapla
    stateChanged = true;   // Ağ üzerinden Kombi'ye hemen haber ver
}

float ThermostatController::getHysteresis() {
    return this->hysteresisBand;
}
