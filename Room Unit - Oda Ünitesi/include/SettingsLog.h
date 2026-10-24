#pragma once
#include <Arduino.h>
#include <Preferences.h>

// Ayar günlüğü: hedef, mod, tolerans ve ofset her değiştiğinde bir satır (zaman,
// eski → yeni, kaynak) NVS'e yazılır; elektrik kesintisinde kaybolmaz.
// Enkoderle art arda yapılan değişiklikler 5 sn durulunca TEK satır olur
// (22,5 → 23,5; ara adımlar yazılmaz).
// Sensör kayması (drift), termometreyle yapılan ofset satırlarından okunur:
//   sapma = ham − termometre = −ofset  →  ofset 0,3 düştüyse sensör 0,3 daha yüksek okuyor.
class SettingsLog {
public:
    enum Kind : uint8_t { KIND_TARGET = 0, KIND_MODE = 1, KIND_HYSTERESIS = 2, KIND_OFFSET = 3 };
    enum Source : uint8_t { SRC_DEVICE = 0, SRC_WEB = 1, SRC_REFERENCE = 2 };

    struct Snapshot {
        float target;
        uint8_t mode;      // OperatingMode: 0 otomatik, 1 yaz, 2 manuel
        float hysteresis;
        float offset;
    };

    static const uint8_t CAPACITY = 200; // Dolunca en eski satırın üzerine yazılır

    void begin(const Snapshot& current); // Açılışta kayıtlı ayarlarla

    // Web isteği bir ayarı değiştirmeden hemen önce çağırır (kaynak "web")
    void markWeb();
    // Termometre formu ofseti değiştirmeden hemen önce çağırır (kaynak "termometre")
    void markReference();

    // loop() içinde her turda: değişiklik 5 sn durulunca günlüğe yazar.
    // raw: ofsetsiz o anki sıcaklık (ofset satırlarında saklanır)
    void handle(const Snapshot& current, float raw, bool sensorFault);

    // {"ok":true,"entries":[[zaman,"tur",eski,yeni,"kaynak",ham|null],...]}, eskiden yeniye.
    // zaman 0 = kayıt anında saat yoktu. Mod değerleri 0/1/2.
    String toJson();

private:
    struct __attribute__((packed)) Entry {
        uint32_t time;     // Unix zamanı (UTC), 0 = saat yok
        uint8_t kind;      // Kind
        uint8_t source;    // Source
        int16_t oldV;      // Eski değer × 10 (mod: 0/1/2)
        int16_t newV;      // Yeni değer × 10 (mod: 0/1/2)
        int16_t raw100;    // Değişiklik başladığında ham sıcaklık × 100, INT16_MIN = yok
    };

    void record(uint8_t kind, uint8_t source, int16_t oldV, int16_t newV);
    static bool same(const Snapshot& a, const Snapshot& b);

    Preferences prefs;
    uint8_t count = 0;     // Dolu satır sayısı
    uint8_t head = 0;      // Bir sonraki yazılacak satır
    Snapshot committed{};  // Günlüğe en son yazılmış hal
    Snapshot lastSeen{};
    bool pending = false;  // Yazılmayı bekleyen değişiklik var mı?
    unsigned long pendingSince = 0, lastChange = 0;
    unsigned long webMarkedAt = 0, refMarkedAt = 0;
    bool webMarked = false, refMarked = false;
    int16_t pendingRaw100 = INT16_MIN;

    const unsigned long SETTLE_DELAY = 5000;
};
