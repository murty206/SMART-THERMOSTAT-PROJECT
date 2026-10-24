#include "SettingsLog.h"
#include <time.h>

static const uint32_t MIN_VALID_TIME = 1700000000UL; // NTP gelmeden önceki saat 1970'tir

static int16_t x10(float v) { return (int16_t)lroundf(v * 10.0f); }

bool SettingsLog::same(const Snapshot& a, const Snapshot& b) {
    return x10(a.target) == x10(b.target) && a.mode == b.mode &&
           x10(a.hysteresis) == x10(b.hysteresis) && x10(a.offset) == x10(b.offset);
}

void SettingsLog::begin(const Snapshot& current) {
    prefs.begin("ayarlog", false);
    count = prefs.getUChar("n", 0);
    head = prefs.getUChar("head", 0);
    if (count > CAPACITY || head >= CAPACITY) { count = 0; head = 0; } // Bozuk sayaç: baştan
    committed = current;
    lastSeen = current;
}

void SettingsLog::markWeb() { webMarked = true; webMarkedAt = millis(); }
void SettingsLog::markReference() { refMarked = true; refMarkedAt = millis(); }

void SettingsLog::handle(const Snapshot& current, float raw, bool sensorFault) {
    unsigned long now = millis();
    if (!same(current, lastSeen)) {
        if (!pending) {
            pending = true;
            pendingSince = now;
            // Ofset değişmeden ÖNCEKİ an değil, değişikliğin başladığı an: ham değer ofsetten bağımsız
            pendingRaw100 = sensorFault ? INT16_MIN : (int16_t)lroundf(raw * 100.0f);
        }
        lastSeen = current;
        lastChange = now;
    }
    if (!pending || now - lastChange < SETTLE_DELAY) return;

    // İşaret, bu değişiklik dizisi başlamadan en fazla 5 sn önce ya da dizi sırasında konduysa geçerli
    bool fromWeb = webMarked && (long)(webMarkedAt - pendingSince) >= -(long)SETTLE_DELAY;
    bool fromRef = refMarked && (long)(refMarkedAt - pendingSince) >= -(long)SETTLE_DELAY;
    uint8_t src = fromWeb ? SRC_WEB : SRC_DEVICE;

    if (x10(current.target) != x10(committed.target))
        record(KIND_TARGET, src, x10(committed.target), x10(current.target));
    if (current.mode != committed.mode)
        record(KIND_MODE, src, committed.mode, current.mode);
    if (x10(current.hysteresis) != x10(committed.hysteresis))
        record(KIND_HYSTERESIS, src, x10(committed.hysteresis), x10(current.hysteresis));
    if (x10(current.offset) != x10(committed.offset))
        record(KIND_OFFSET, fromRef ? SRC_REFERENCE : src, x10(committed.offset), x10(current.offset));

    committed = current;
    pending = false;
    webMarked = refMarked = false;
}

void SettingsLog::record(uint8_t kind, uint8_t source, int16_t oldV, int16_t newV) {
    time_t now = time(nullptr);
    Entry e;
    e.time = now >= (time_t)MIN_VALID_TIME ? (uint32_t)now : 0;
    e.kind = kind;
    e.source = source;
    e.oldV = oldV;
    e.newV = newV;
    e.raw100 = pendingRaw100;

    // Her satır kendi anahtarında: bir satır yazmak tüm günlüğü yeniden yazmaz
    char key[8];
    snprintf(key, sizeof(key), "e%u", head);
    prefs.putBytes(key, &e, sizeof(e));
    head = (head + 1) % CAPACITY;
    if (count < CAPACITY) count++;
    prefs.putUChar("head", head);
    prefs.putUChar("n", count);

    Serial.printf("[AYAR] tur=%u kaynak=%u %d -> %d (ham x100=%d)\n", kind, source, oldV, newV, e.raw100);
}

String SettingsLog::toJson() {
    static const char* KIND_NAMES[] = {"hedef", "mod", "tolerans", "ofset"};
    static const char* SOURCE_NAMES[] = {"cihaz", "web", "termometre"};
    String json;
    json.reserve(32 + count * 48);
    json = "{\"ok\":true,\"entries\":[";
    char buf[96];
    uint8_t start = (head + CAPACITY - count) % CAPACITY; // En eski satır
    bool first = true;
    for (uint8_t i = 0; i < count; i++) {
        char key[8];
        snprintf(key, sizeof(key), "e%u", (start + i) % CAPACITY);
        Entry e;
        if (prefs.getBytes(key, &e, sizeof(e)) != sizeof(e)) continue;
        if (e.kind > KIND_OFFSET || e.source > SRC_REFERENCE) continue;
        char raw[12];
        if (e.raw100 == INT16_MIN) strcpy(raw, "null");
        else snprintf(raw, sizeof(raw), "%.2f", e.raw100 / 100.0f);
        if (e.kind == KIND_MODE) {
            snprintf(buf, sizeof(buf), "%s[%u,\"%s\",%d,%d,\"%s\",%s]", first ? "" : ",", (unsigned)e.time,
                     KIND_NAMES[e.kind], e.oldV, e.newV, SOURCE_NAMES[e.source], raw);
        } else {
            snprintf(buf, sizeof(buf), "%s[%u,\"%s\",%.1f,%.1f,\"%s\",%s]", first ? "" : ",", (unsigned)e.time,
                     KIND_NAMES[e.kind], e.oldV / 10.0f, e.newV / 10.0f, SOURCE_NAMES[e.source], raw);
        }
        json += buf;
        first = false;
    }
    json += "]}";
    return json;
}
