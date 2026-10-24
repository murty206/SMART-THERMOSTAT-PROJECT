# SMART-THERMOSTAT-PROJECT
ESP32 tabanlı akıllı termostat donanım kontrol ve UI yönetim sistemi.

İki ESP32'den oluşur ve ikisi Wi-Fi ağına bağlanmadan ESP-NOW ile konuşur:

- **Oda Ünitesi** — sıcaklığı ölçer, kararı verir, ekran ve enkoder burada.
- **Kombi Ünitesi** — oda ünitesinden gelen emre göre kombinin rölesini çeker.

## Bağlantılar

| Ünite | Parça | ESP32 pini |
|---|---|---|
| Oda | SH1106 OLED (I2C, 0x3C) | SDA 21, SCL 22 |
| Oda | BMP280 / BME280 (I2C, 0x76 veya 0x77) | SDA 21, SCL 22 |
| Oda | Enkoder CLK / DT / SW | 25 / 26 / 27 |
| Kombi | Röle sürücü transistörü (BC546) base | 26 |

## Kurulum

Her iki klasör ayrı bir PlatformIO projesidir.

**Yüklerken BOOT düğmesi:** oda ünitesi kartı indirme moduna kendiliğinden
geçmiyor (`Wrong boot mode detected`). Yükleme "Connecting..." aşamasındayken
BOOT'u basılı tutup EN'e bir kez basın, BOOT'u 1-2 sn sonra bırakın. Windows'ta
kart görünmüyorsa (Aygıt Yöneticisi'nde hata 28) Silicon Labs CP210x sürücüsü
kurulmalıdır.

1. **Kombi ünitesini** yükleyin ve seri monitörde `Kombi Cihazi MAC Adresi`
   satırını okuyun.
2. Bu adresi `Room Unit - Oda Ünitesi/src/main.cpp` içindeki
   `kombiMacAddress` dizisine yazın.
3. **Oda ünitesini** yükleyin ve seri monitörde `Oda Cihazi MAC Adresi`
   satırını okuyun.
4. Bu adresi `Combi Boiler Unit - Kombi Ünitesi/src/main.cpp` içindeki
   `odaMacAddress` dizisine yazıp kombi ünitesini yeniden yükleyin.
   Boş bırakılırsa kombi her ESP-NOW gönderenini kabul eder.

**Eski yazılımdan geçerken:** iki üniteyi de yeni yazılıma geçirmeden Wi-Fi
kurulumu yapmayın. Eski kombi yazılımı ESP-NOW'u kanal 1'de dinler; yeni oda
ünitesi Wi-Fi kurulana kadar kurulum ağını kanal 1'de açtığı için eski kombiyle
konuşur, ama ev Wi-Fi'sine bağlandığı anda modemin kanalına geçer. Modem kanal
1'de değilse eski kombi onu duyamaz ve 15 dakika sonra kendini kapatır.

## Web arayüzü

| Ünite | Adres | Ne yapar |
|---|---|---|
| Oda | http://termostat.local | Durum, hedef, tolerans, kalibrasyon, çalışma modu |
| Kombi | http://kombi.local | Röle durumu, son paket, Wi-Fi (salt okunur) |

**İlk kurulum (her ünite için ayrı):**

0. **Derlemeden önce** her ünitede `include/secrets.example.h` dosyasını
   `include/secrets.h` adıyla kopyalayıp `SETUP_AP_PASSWORD` değerini kendi
   şifrenizle değiştirin (en az 8 karakter). `secrets.h` GitHub'a gitmez;
   dosya yoksa veya şablon değeri değiştirilmemişse derleme hata verir.
1. Ünite kayıtlı Wi-Fi bulamazsa kendi ağını açar: `Termostat-Kurulum` veya
   `Kombi-Kurulum`, şifresi `secrets.h` içindeki değer. Oda ünitesi bunu
   ekranında da gösterir.
2. Telefonla bu ağa bağlanın; kurulum sayfası açılmazsa `http://192.168.4.1/wifi`.
3. Ev Wi-Fi'sinin adını ve şifresini, ayrıca bir **yönetici şifresi** girin.
   Ünite yeniden başlar ve ev ağına bağlanır.

Ayar değiştirirken tarayıcı kullanıcı adı ve şifre sorar: kullanıcı `admin`,
şifre kurulumda girdiğiniz yönetici şifresi. Durum sayfası şifresizdir. Yönetici
şifresinin kendisi cihazda saklanmaz, yalnızca tuzlu SHA-256 özeti tutulur.

**ESP32 yalnızca 2,4 GHz Wi-Fi'ye bağlanabilir.** Modemin 2,4 GHz bandı açık
olmalı; yalnızca 5 GHz yayın yapan bir ağ kurulum sayfasında görünmez ve ünite
kurulum ağında kalır.

**İki ünite aynı modeme bağlı olmalı.** ESP-NOW, Wi-Fi ile aynı kanalı
kullanır; üniteler farklı ağlarda (veya biri kurulum ağında) kalırsa
birbirlerini duyamaz ve kombi 5 dakika sonra kendini kapatır. Modem 60 saniyeden
uzun süre bulunamazsa ünite kurulum ağını yeniden açar, bu arada modemi aramaya
devam eder.

Yönetici şifresi unutulursa: `pio run -t erase` ile flash silinip yeniden
yüklenmelidir (kayıtlı tüm ayarlar sıfırlanır).

## Çalışma modları

| Mod | Ne olur | Oda ünitesinden | Çıkış |
|---|---|---|---|
| Otomatik | Termostat hedefe göre kombiyi açıp kapatır | Varsayılan | — |
| Yaz | Hedef 5 °C: kombi yalnızca donma tehlikesinde yanar | Ana ekranda uzun bas | Uzun bas |
| Manuel | Termostat devre dışı; röle sürekli kapalı kontak, kombi **kendi panelindeki ayara** göre çalışır | Menü → Manuel Mod | Uzun bas |

Web arayüzünden de mod seçilebilir. Manuel mod, termostatın sensörü arızalı
olsa bile evi ısıtmak için kullanılabilir.

## Güvenlik davranışları

- Sensör okunamazsa oda ünitesi kombiyi kapatır ve ekranda **SENSOR HATASI**
  gösterir (manuel modda röleyi etkilemez, yalnızca uyarı gösterilir).
- Kombi ünitesi 5 dakika boyunca oda ünitesinden paket alamazsa kombiyi kapatır
  — **manuel modda da**.
- Hedef, tolerans, kalibrasyon ve çalışma modu elektrik kesintisinden sonra korunur.
