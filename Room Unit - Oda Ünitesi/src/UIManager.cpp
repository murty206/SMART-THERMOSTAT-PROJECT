#include "UIManager.h"
#include <WiFi.h>

UIManager::UIManager(Adafruit_SH1106G* disp, float initialTarget, float initialHysteresis) {
    this->display = disp;
    this->currentState = STATE_HOME;
    this->menuIndex = 0;
    this->uiTargetTemp = initialTarget;
    this->currentEnvTemp = 0.0;
    this->isRelayActive = false;
    this->sensorFault = false;
    this->needsRedraw = true;
    this->displayOn = true;
    this->lastActivityTime = 0; // İlk çizimde başlatılır
    this->editBackup = 0.0;
    this->linkOk = false;
    this->lastInfoDraw = 0;
    this->targetTempChanged = false;
    this->uiHysteresis = initialHysteresis;
    this->hysteresisChanged = false;
    this->uiOffset = 0.0; 
    this->offsetChanged = false;
}

// ==========================================
// KULLANICI GİRDİSİ (BUTON VE ENKODER) YÖNETİMİ
// ==========================================
void UIManager::processInput(ButtonEvent event, int encoderTicks) {
    // Eğer hiçbir şey yapılmadıysa çık
    if (event == BTN_NONE && encoderTicks == 0) return;

    // Bir şey yapıldıysa ekranı kesinlikle yenilememiz gerekecek
    needsRedraw = true;
    lastActivityTime = millis();

    // Ekran kapalıyken ilk dokunuş sadece uyandırır; karanlıkta yanlışlıkla
    // hedef sıcaklık değişmesin
    if (!displayOn) {
        setDisplayPower(true);
        return;
    }

    // HANGİ EKRANDAYIZ? Ona göre tepki ver
    switch (currentState) {
        
        case STATE_HOME:
            if (event == BTN_SHORT_PRESS) {
                currentState = STATE_MAIN_MENU; // Menüye geç
                menuIndex = 0;
            } 
            else if (event == BTN_LONG_PRESS) {
                currentState = STATE_SUMMER_MODE; // Yaz moduna geç
            }
            else if (encoderTicks != 0) {
                // Ana ekranda çevrilirse sıcaklığı ayarla
                uiTargetTemp += (encoderTicks * 0.5);
                uiTargetTemp = constrain(uiTargetTemp, 5.0, 35.0);
                targetTempChanged = true; // Termostata haber vermek için bayrağı kaldır
            }
            break;

        case STATE_MAIN_MENU:
            if (encoderTicks != 0) {
                menuIndex += encoderTicks;
                if (menuIndex < 0) menuIndex = 0;
                if (menuIndex >= MAX_MENU_ITEMS) menuIndex = MAX_MENU_ITEMS - 1;
            }
            else if (event == BTN_SHORT_PRESS) {
                if (menuIndex == 0) {
                    currentState = STATE_SET_HYSTERESIS;
                    editBackup = uiHysteresis;
                } else if (menuIndex == 1) {
                    currentState = STATE_SET_OFFSET; // YENİ: Kalibrasyon sayfasına geç
                    editBackup = uiOffset;
                } else if (menuIndex == 2) {
                    currentState = STATE_MANUAL_MODE; // Kombi kendi panelinden
                } else if (menuIndex == 3) {
                    currentState = STATE_WIFI_INFO;  // Wi-Fi ve kombi bağlantısı
                } else if (menuIndex == 4) {
                    currentState = STATE_HOME;       // 5. Seçenek Çıkış
                }
            }
            break;

        case STATE_SET_HYSTERESIS: 
            if (encoderTicks != 0) {
                uiHysteresis += (encoderTicks * 0.1);
                uiHysteresis = constrain(uiHysteresis, 0.1, 2.0);
            } 
            else if (event == BTN_SHORT_PRESS) {
                hysteresisChanged = true; 
                currentState = STATE_HOME;
            }
            break;

        case STATE_SET_OFFSET: // MİMARİDEN EKSİK OLAN BLOK EKLENDİ
            if (encoderTicks != 0) {
                uiOffset += (encoderTicks * 0.1); 
                uiOffset = constrain(uiOffset, -10.0, 10.0);
            } 
            else if (event == BTN_SHORT_PRESS) {
                offsetChanged = true; 
                currentState = STATE_HOME;
            }
            break;
        case STATE_SUMMER_MODE:
            // Yaz modundayken çevirmenin etkisi yok
            if (event == BTN_LONG_PRESS) {
                currentState = STATE_HOME; // Kış moduna geri dön
                // Yaz modu termostat hedefini 5 dereceye çekmişti; kullanıcının
                // ekranda gördüğü hedefi termostata geri yüklet
                targetTempChanged = true;
            }
            break;

        case STATE_WIFI_INFO:
            // Salt okunur bilgi ekranı: herhangi bir basış ana ekrana döner
            if (event == BTN_SHORT_PRESS || event == BTN_LONG_PRESS) {
                currentState = STATE_HOME;
            }
            break;

        case STATE_MANUAL_MODE:
            // Manuelde çevirmenin etkisi yok; uzun basış otomatiğe döndürür
            if (event == BTN_LONG_PRESS) {
                currentState = STATE_HOME;
            }
            break;
    }
}

// ==========================================
// SENSÖR VERİSİ GÜNCELLEMESİ
// ==========================================
void UIManager::updateSensorData(float envTemp, bool relayState, bool sensorFault) {
    // Sadece sıcaklık, röle veya sensör durumu gerçekten değiştiyse ekranı yenile
    if (abs(this->currentEnvTemp - envTemp) >= 0.1 || this->isRelayActive != relayState
        || this->sensorFault != sensorFault) {
        this->currentEnvTemp = envTemp;
        this->isRelayActive = relayState;
        this->sensorFault = sensorFault;
        this->needsRedraw = true;
    }

    // Sensör arızası karanlıkta fark edilmeden kalmasın: ekranı açık tut
    if (sensorFault) {
        lastActivityTime = millis();
        if (!displayOn) setDisplayPower(true);
    }
}

// ==========================================
// KAYITLI AYARLARI GERİ YÜKLEME (Açılışta bir kez)
// ==========================================
void UIManager::restoreSettings(float target, float hysteresis, float offset, OperatingMode mode) {
    this->uiTargetTemp = target;
    this->uiHysteresis = hysteresis;
    this->uiOffset = offset;
    if (mode == MODE_SUMMER) this->currentState = STATE_SUMMER_MODE;
    else if (mode == MODE_MANUAL) this->currentState = STATE_MANUAL_MODE;
    else this->currentState = STATE_HOME;
    this->needsRedraw = true;
}

// ==========================================
// ÇİZİM MOTORU (Sadece ihtiyaç varsa çizer)
// ==========================================
void UIManager::updateScreen() {
    unsigned long now = millis();
    if (lastActivityTime == 0) lastActivityTime = now; // Açılış ekranından sonra say

    if (displayOn && (now - lastActivityTime >= SCREEN_TIMEOUT)) {
        // Yarım kalan menü kapanır, onaylanmamış değişiklik geri alınır
        if (currentState == STATE_SET_HYSTERESIS) uiHysteresis = editBackup;
        if (currentState == STATE_SET_OFFSET) uiOffset = editBackup;
        // Modlar (yaz/manuel) korunur; yalnızca menü ve ayar ekranları kapanır
        if (currentState != STATE_SUMMER_MODE && currentState != STATE_MANUAL_MODE) {
            currentState = STATE_HOME;
        }
        setDisplayPower(false);
        needsRedraw = true; // Uyanınca güncel ekran çizilsin
        return;
    }
    if (!displayOn) return; // Kapalı ekrana çizmeye gerek yok

    // Wi-Fi ekranındaki değerler (sinyal, bağlantı) kendiliğinden değişir: saniyede bir çiz
    if (currentState == STATE_WIFI_INFO && now - lastInfoDraw >= 1000) {
        lastInfoDraw = now;
        needsRedraw = true;
    }

    if (!needsRedraw) return;

    display->clearDisplay();
    display->setTextSize(1);
    display->setTextColor(1);

    if (currentState == STATE_HOME) drawHome();
    else if (currentState == STATE_MAIN_MENU) drawMainMenu();
    else if (currentState == STATE_SUMMER_MODE) drawSummerMode();
    else if (currentState == STATE_SET_HYSTERESIS) drawSetHysteresis();
    else if (currentState == STATE_SET_OFFSET) drawSetOffset(); // EKSİK OLAN SATIR EKLENDİ
    else if (currentState == STATE_MANUAL_MODE) drawManualMode();
    else if (currentState == STATE_WIFI_INFO) drawWifiInfo();

    display->display();
    needsRedraw = false; 
}

// ==========================================
// ÖZEL EKRAN TASARIMLARI
// ==========================================
void UIManager::drawHome() {
    if (sensorFault) {
        // Sensör okunamıyor: sıcaklık yerine uyarı göster (kombi zaten kapalı tutulur)
        display->setCursor(0, 5);
        display->setTextSize(2);
        display->println("SENSOR");
        display->print("HATASI");
    } else {
        display->setCursor(0, 5);
        display->setTextSize(3);
        display->print(currentEnvTemp, 1); 

        display->setTextSize(1);
        display->setCursor(75, 5);
        display->print((char)247); // DERECE SEMBOLÜ
        display->print("C");
    }

    display->setCursor(0, 45);
    display->setTextSize(1);
    display->print("Hedef: ");
    display->print(uiTargetTemp, 1);
    display->print((char)247); // DERECE SEMBOLÜ
    display->print("C");

    if (isRelayActive) {
        display->drawBitmap(105, 5, icon_flame_16x16, 16, 16, 1);
        display->setCursor(105, 25);
        display->print("ON");
    } else {
        display->drawBitmap(105, 5, icon_snow_16x16, 16, 16, 1);
        display->setCursor(102, 25);
        display->print("OFF"); 
    }
}

void UIManager::drawMainMenu() {
    display->setCursor(0, 0);
    display->println("--- ANA MENU ---");
    
    if (menuIndex == 0) display->print("> "); else display->print("  ");
    display->println("Tolerans Ayari");
    
    if (menuIndex == 1) display->print("> "); else display->print("  ");
    display->println("Kalibrasyon"); // EKSİK OLAN MENÜ EKLENDİ
    
    if (menuIndex == 2) display->print("> "); else display->print("  ");
    display->println("Manuel Mod");

    if (menuIndex == 3) display->print("> "); else display->print("  ");
    display->println("Wi-Fi Durumu");

    if (menuIndex == 4) display->print("> "); else display->print("  ");
    display->println("Cikis");
}

// Satır başına en fazla 21 karakter (6 px × 21 = 126 px, ekran 128)
void UIManager::drawWifiInfo() {
    display->setCursor(0, 0);
    display->println("--- WI-FI DURUMU ---");

    if (WiFi.status() == WL_CONNECTED) {
        display->println(WiFi.SSID().substring(0, 21));
        int rssi = WiFi.RSSI();
        display->printf("Sinyal:%d dBm %s\n", rssi,  // "-100 dBm zayif" bile 21 karaktere sığar
                        rssi >= -60 ? "iyi" : (rssi >= -75 ? "orta" : "zayif"));
        display->print("IP: ");
        display->println(WiFi.localIP());
        display->printf("Kanal: %d\n", (int)WiFi.channel());
    } else {
        display->println("Bagli degil");
        if (WiFi.getMode() & WIFI_MODE_AP) {
            display->println("Kurulum agi acik");
            display->print("IP: ");
            display->println(WiFi.softAPIP());
        }
        display->println("");
    }
    display->print("Kombi: ");
    display->println(linkOk ? "bagli" : "YOK");
    display->println("Cikis: bas");
}

void UIManager::setLinkStatus(bool ok) {
    if (ok != linkOk) {
        linkOk = ok;
        if (currentState == STATE_WIFI_INFO) needsRedraw = true;
    }
}

void UIManager::drawManualMode() {
    display->setCursor(0, 0);
    display->println("--- MANUEL MOD ---");
    display->setCursor(0, 20);
    display->setTextSize(2);
    if (sensorFault) {
        display->println("SENSOR ?"); // Manuelde röleyi etkilemez, yalnızca bilgi
    } else {
        display->print(currentEnvTemp, 1);
        display->print((char)247); // DERECE SEMBOLÜ
        display->println("C");
    }

    display->setTextSize(1);
    display->setCursor(0, 45);
    display->println("Kontrol: kombi paneli"); // 21 karakter = 126 px (ekran 128)
    display->println("Cikis: uzun bas");
}

// Sade yaz ekranı: büyük sıcaklık + güneş, altta "YAZ MODU" (kullanıcı tercihi)
void UIManager::drawSummerMode() {
    if (sensorFault) {
        display->setCursor(0, 5);
        display->setTextSize(2);
        display->print("SENSOR ?");
    } else {
        display->setCursor(0, 5);
        display->setTextSize(3);
        display->print(currentEnvTemp, 1);
        display->setTextSize(1);
        display->setCursor(75, 5);
        display->print((char)247); // DERECE SEMBOLÜ
        display->print("C");
    }
    display->drawBitmap(105, 5, icon_sun_16x16, 16, 16, 1);

    // "YAZ MODU" 8 karakter × 12 px = 96 px: ortalamak için x = (128 - 96) / 2
    display->setTextSize(2);
    display->setCursor(16, 42);
    display->print("YAZ MODU");
}

// ==========================================
// BİLGİ AKTARIM KAPILARI
// ==========================================
bool UIManager::isTargetChanged() { return targetTempChanged; }
float UIManager::getNewTargetTemp() { return uiTargetTemp; }
void UIManager::clearTargetChangeFlag() { targetTempChanged = false; }
AppState UIManager::getCurrentState() { return currentState; }

OperatingMode UIManager::getOperatingMode() {
    if (currentState == STATE_SUMMER_MODE) return MODE_SUMMER;
    if (currentState == STATE_MANUAL_MODE) return MODE_MANUAL;
    return MODE_AUTO;
}

void UIManager::drawSetHysteresis() {
    display->setCursor(0, 0); 
    display->println("--- TOLERANS ---");
    display->setCursor(0, 20); 
    display->setTextSize(2);
    display->print("+- "); 
    display->print(uiHysteresis, 1); 
    display->print((char)247); // DERECE SEMBOLÜ
    display->println("C");
    
    display->setTextSize(1); 
    display->setCursor(0, 45); 
    display->println("Onaylamak icin bas");
}

bool UIManager::isHysteresisChanged() { return hysteresisChanged; }
float UIManager::getNewHysteresis() { return uiHysteresis; }
void UIManager::clearHysteresisChangeFlag() { hysteresisChanged = false; }

void UIManager::drawSetOffset() {
    display->setCursor(0, 0); 
    display->println("--- KALIBRASYON ---");
    display->setCursor(0, 20); 
    display->setTextSize(2);
    if (uiOffset > 0) display->print("+"); 
    display->print(uiOffset, 1); 
    display->print((char)247); // DERECE SEMBOLÜ
    display->println("C");
    
    display->setTextSize(1); 
    display->setCursor(0, 45); 
    display->println("Onaylamak icin bas");
}

// Dış dünyanın ofset değişiminden haberdar olma kapıları
bool UIManager::isOffsetChanged() { return offsetChanged; }
float UIManager::getNewOffset() { return uiOffset; }
void UIManager::clearOffsetChangeFlag() { offsetChanged = false; }

// ==========================================
// WEB ARAYÜZÜNDEN GELEN AYARLAR
// ==========================================
void UIManager::applyRemoteSettings(float target, float hysteresis, float offset, OperatingMode mode) {
    target = constrain(target, 5.0, 35.0);
    hysteresis = constrain(hysteresis, 0.1, 2.0);
    offset = constrain(offset, -10.0, 10.0);

    // Bayraklar kaldırılır; main.cpp bunları enkoder değişikliği gibi işler
    if (target != uiTargetTemp) { uiTargetTemp = target; targetTempChanged = true; }
    if (hysteresis != uiHysteresis) { uiHysteresis = hysteresis; hysteresisChanged = true; }
    if (offset != uiOffset) { uiOffset = offset; offsetChanged = true; }

    OperatingMode current = getOperatingMode();
    if (mode != current) {
        // Yaz modu termostat hedefini 5 dereceye çekmişti; çıkışta geri yükle
        if (current == MODE_SUMMER) targetTempChanged = true;

        if (mode == MODE_SUMMER) currentState = STATE_SUMMER_MODE;
        else if (mode == MODE_MANUAL) currentState = STATE_MANUAL_MODE;
        else currentState = STATE_HOME;
    }
    needsRedraw = true;
}

void UIManager::setDisplayPower(bool on) {
    display->oled_command(on ? SH110X_DISPLAYON : SH110X_DISPLAYOFF);
    displayOn = on;
    if (on) needsRedraw = true;
}
