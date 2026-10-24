#pragma once
#include <Arduino.h>
#include "esp_partition.h"

// Flash'ta tutulan tek bir 10 dakikalık kayıt (8 bayt)
struct __attribute__((packed)) HistoryRecord {
    uint32_t time;     // Aralığın başlangıcı, Unix zamanı (UTC). 0xFFFFFFFF = boş yuva
    int16_t temp10;    // Aralıktaki ortalama sıcaklık ×10 (bit7'ye göre ham ya da ofsetli). INT16_MIN = arıza
    uint8_t target2;   // Hedef sıcaklık ×2
    uint8_t packed;    // bit0-3: kombi çalışma oranı (0-15), bit4-5: mod, bit6: kombi bağlantısı,
                       // bit7: temp10 HAM (ofsetsiz). 0 = eski kayıt, o anki ofsetle yazılmış.
};
// 2026-10-20'den sonraki kayıtlar ham sıcaklık saklar; grafik onları GÜNCEL ofsetle çizer,
// böylece kalibrasyon değişince çizgide basamak oluşmaz. Eski kayıtlar (bit7 = 0) olduğu gibi.
static const uint8_t HISTORY_RAW_FLAG = 0x80;
static_assert(sizeof(HistoryRecord) == 8, "HistoryRecord 8 bayt olmali");

// "DVR mantığıyla" sıcaklık geçmişi: 10 dakikada bir kayıt, flash'taki
// veri bölümünde halka tampon; yer dolunca en eski sektör silinip üzerine yazılır.
// Bölüm: varsayılan bölüm tablosundaki "spiffs" (kod başka hiçbir yerde kullanmaz).
class HistoryLog {
private:
    const esp_partition_t* part;
    bool ready;
    uint32_t totalSlots;     // Veri sektörlerindeki toplam kayıt yuvası
    uint32_t writeSlot;      // Sıradaki yazılacak yuva

    // O anki 10 dakikalık aralığın birikimi
    uint32_t currentBucket;  // now / INTERVAL; 0 = zaman henüz yok
    float tempSum;
    uint16_t tempCount;
    uint16_t relayOn;
    uint16_t relaySamples;

    static const uint32_t SECTOR = 4096;
    static const uint32_t SLOTS_PER_SECTOR = SECTOR / sizeof(HistoryRecord);
    static const uint32_t INTERVAL = 600;          // 10 dakika
    static const uint32_t MIN_VALID_TIME = 1700000000; // 2023-11: NTP gelmeden önceki saat geçersiz

    bool readSlot(uint32_t slot, HistoryRecord& r);
    bool isValid(const HistoryRecord& r);
    void format();
    void appendRecord(const HistoryRecord& r);
    void resetBucket();

public:
    HistoryLog();

    bool begin();  // setup() içinde: bölümü bulur, gerekirse hazırlar, kaldığı yeri bulur

    // Her sensör okumasında çağrılır (ortalamaya ve çalışma oranına katılır).
    // rawTemp: OFSETSİZ ham sıcaklık
    void addSample(float rawTemp, bool sensorFault, bool relayActive);

    // loop() içinde: 10 dakikalık aralık dolunca kaydı flash'a yazar
    void handle(float target, uint8_t mode, bool linkOk);

    // Son 'rangeSeconds' saniyeyi en fazla 'maxPoints' noktaya seyrelterek JSON döner.
    // offset: GÜNCEL kalibrasyon ofseti; ham kayıtlara eklenir
    String toJson(uint32_t rangeSeconds, uint16_t maxPoints, float offset);

    bool isReady();
    bool hasTime(); // NTP saati geldi mi? (gelmeden kayıt yapılmaz)
};
