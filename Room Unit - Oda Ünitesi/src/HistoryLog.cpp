#include "HistoryLog.h"
#include <time.h>

// Bölüm bizim biçimimizde mi? İlk sektörün başındaki imza.
// Yoksa (ör. eski yazılımdan kalma veri) bölüm bir kez silinir.
static const char MAGIC[8] = {'T', 'R', 'M', 'H', 'I', 'S', 'T', '1'};

HistoryLog::HistoryLog() {
    part = nullptr;
    ready = false;
    totalSlots = 0;
    writeSlot = 0;
    currentBucket = 0;
    resetBucket();
}

void HistoryLog::resetBucket() {
    tempSum = 0;
    tempCount = 0;
    relayOn = 0;
    relaySamples = 0;
}

bool HistoryLog::isReady() { return ready; }
bool HistoryLog::hasTime() { return time(nullptr) >= (time_t)MIN_VALID_TIME; }

// Veri sektörleri 1. sektörden başlar (0. sektör imza)
bool HistoryLog::readSlot(uint32_t slot, HistoryRecord& r) {
    return esp_partition_read(part, SECTOR + slot * sizeof(HistoryRecord), &r, sizeof(r)) == ESP_OK;
}

bool HistoryLog::isValid(const HistoryRecord& r) {
    return r.time >= MIN_VALID_TIME && r.time < 2100000000UL &&
           r.target2 >= 10 && r.target2 <= 70 &&
           ((r.packed >> 4) & 0x03) <= 2;
}

// ==========================================
// HAZIRLIK
// ==========================================
void HistoryLog::format() {
    unsigned long start = millis();
    uint32_t sectors = part->size / SECTOR;
    Serial.printf("HistoryLog: Bolum hazirlaniyor (%u sektor siliniyor)...\n", sectors);
    for (uint32_t i = 0; i < sectors; i++) {
        esp_partition_erase_range(part, i * SECTOR, SECTOR);
        delay(1); // Diğer çekirdek ve watchdog nefes alsın
    }
    esp_partition_write(part, 0, MAGIC, sizeof(MAGIC));
    Serial.printf("HistoryLog: Hazir (%lu ms).\n", millis() - start);
}

bool HistoryLog::begin() {
    part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
    if (part == nullptr || part->size < 2 * SECTOR) {
        Serial.println("HistoryLog: Veri bolumu bulunamadi, gecmis KAPALI.");
        return false;
    }
    totalSlots = (part->size / SECTOR - 1) * SLOTS_PER_SECTOR;

    char magic[sizeof(MAGIC)];
    esp_partition_read(part, 0, magic, sizeof(magic));
    if (memcmp(magic, MAGIC, sizeof(MAGIC)) != 0) format();

    // Kaldığı yeri bul: ilk kaydı en yeni olan sektör, son yazılan sektördür
    uint32_t sectors = totalSlots / SLOTS_PER_SECTOR;
    int32_t newest = -1;
    uint32_t newestTime = 0;
    HistoryRecord r;
    for (uint32_t s = 0; s < sectors; s++) {
        if (readSlot(s * SLOTS_PER_SECTOR, r) && isValid(r) && r.time >= newestTime) {
            newest = s;
            newestTime = r.time;
        }
    }

    writeSlot = 0;
    if (newest >= 0) {
        // O sektörde ilk boş yuva; sektör doluysa bir sonrakinin başı
        writeSlot = ((newest + 1) % sectors) * SLOTS_PER_SECTOR;
        for (uint32_t i = 0; i < SLOTS_PER_SECTOR; i++) {
            if (readSlot(newest * SLOTS_PER_SECTOR + i, r) && r.time == 0xFFFFFFFF) {
                writeSlot = newest * SLOTS_PER_SECTOR + i;
                break;
            }
        }
    }

    ready = true;
    Serial.printf("HistoryLog: %u yuva (~%u gun), yazma konumu %u\n",
                  totalSlots, totalSlots / (86400 / INTERVAL), writeSlot);
    return true;
}

// ==========================================
// KAYIT
// ==========================================
void HistoryLog::appendRecord(const HistoryRecord& r) {
    // Yeni sektöre girerken önce sil: halka tamponda en eski veri budur
    if (writeSlot % SLOTS_PER_SECTOR == 0) {
        esp_partition_erase_range(part, SECTOR + (writeSlot / SLOTS_PER_SECTOR) * SECTOR, SECTOR);
    }
    esp_partition_write(part, SECTOR + writeSlot * sizeof(HistoryRecord), &r, sizeof(r));
    writeSlot = (writeSlot + 1) % totalSlots;
}

void HistoryLog::addSample(float rawTemp, bool sensorFault, bool relayActive) {
    if (!ready || !hasTime() || currentBucket == 0) return;
    if (!sensorFault) {
        tempSum += rawTemp;
        tempCount++;
    }
    relaySamples++;
    if (relayActive) relayOn++;
}

void HistoryLog::handle(float target, uint8_t mode, bool linkOk) {
    if (!ready || !hasTime()) return;
    uint32_t bucket = (uint32_t)time(nullptr) / INTERVAL;

    if (currentBucket == 0) {         // Saat yeni geldi: ilk aralığı başlat
        currentBucket = bucket;
        resetBucket();
        return;
    }
    if (bucket == currentBucket) return;

    if (relaySamples > 0) {
        HistoryRecord r;
        r.time = currentBucket * INTERVAL;
        r.temp10 = tempCount ? (int16_t)lroundf(tempSum / tempCount * 10.0f) : INT16_MIN;
        r.target2 = (uint8_t)constrain(lroundf(target * 2.0f), 10L, 70L);
        uint8_t duty = (uint8_t)lroundf(relayOn * 15.0f / relaySamples);
        r.packed = (duty & 0x0F) | ((mode & 0x03) << 4) | (linkOk ? 0x40 : 0) | HISTORY_RAW_FLAG;
        appendRecord(r);
    }
    currentBucket = bucket;
    resetBucket();
}

// ==========================================
// OKUMA (web grafiği için)
// ==========================================
String HistoryLog::toJson(uint32_t rangeSeconds, uint16_t maxPoints, float offset) {
    if (!ready) return "{\"ok\":false,\"reason\":\"bolum yok\",\"points\":[]}";
    if (!hasTime()) return "{\"ok\":false,\"reason\":\"saat yok\",\"points\":[]}";

    uint32_t now = (uint32_t)time(nullptr);
    uint32_t from = now - rangeSeconds;
    uint32_t step = rangeSeconds / maxPoints;
    if (step < INTERVAL) step = INTERVAL;
    uint32_t n = (rangeSeconds + step - 1) / step;

    struct Acc { float temp; uint16_t tempN; uint16_t target2; uint16_t duty; uint16_t count;
                 uint16_t modeN[3]; uint16_t linkN; };
    Acc* acc = (Acc*)calloc(n, sizeof(Acc));
    if (acc == nullptr) return "{\"ok\":false,\"reason\":\"bellek\",\"points\":[]}";

    // Yazma konumundan geriye doğru: ilk geçersiz/eski kayıtta dur
    HistoryRecord r;
    for (uint32_t k = 1; k <= totalSlots; k++) {
        uint32_t slot = (writeSlot + totalSlots - k) % totalSlots;
        if (!readSlot(slot, r) || !isValid(r) || r.time < from) break;
        if (r.time > now) continue;
        uint32_t i = (r.time - from) / step;
        if (i >= n) continue;
        if (r.temp10 != INT16_MIN) {
            // Ham kayıt güncel ofsetle gösterilir; eski kayıt yazıldığı andaki ofsetle kalır
            float t = r.temp10 / 10.0f + ((r.packed & HISTORY_RAW_FLAG) ? offset : 0.0f);
            acc[i].temp += t;
            acc[i].tempN++;
        }
        acc[i].target2 += r.target2;
        acc[i].duty += r.packed & 0x0F;
        uint8_t mode = (r.packed >> 4) & 0x03;
        if (mode < 3) acc[i].modeN[mode]++;
        if (r.packed & 0x40) acc[i].linkN++;
        acc[i].count++;
    }

    String json;
    json.reserve(64 + n * 36);
    // Nokta: [zaman, sıcaklık, hedef, kombi %, mod (dilimde en sık: 0 oto, 1 yaz, 2 manuel),
    //         kombi bağlantısı % (dilimde bağlantının var olduğu kayıtların oranı)]
    json = "{\"ok\":true,\"now\":" + String(now) + ",\"step\":" + String(step) + ",\"points\":[";
    bool first = true;
    char buf[96];
    for (uint32_t i = 0; i < n; i++) {
        if (acc[i].count == 0) continue;
        uint32_t t = from + i * step;
        char temp[12];
        if (acc[i].tempN) snprintf(temp, sizeof(temp), "%.1f", acc[i].temp / acc[i].tempN);
        else strcpy(temp, "null");
        uint8_t mode = 0;
        for (uint8_t m = 1; m < 3; m++) if (acc[i].modeN[m] > acc[i].modeN[mode]) mode = m;
        snprintf(buf, sizeof(buf), "%s[%u,%s,%.1f,%d,%u,%d]", first ? "" : ",", t, temp,
                 acc[i].target2 / 2.0f / acc[i].count,
                 (int)lroundf(acc[i].duty * 100.0f / 15.0f / acc[i].count), mode,
                 (int)lroundf(acc[i].linkN * 100.0f / acc[i].count));
        json += buf;
        first = false;
    }
    json += "]}";
    free(acc);
    return json;
}
