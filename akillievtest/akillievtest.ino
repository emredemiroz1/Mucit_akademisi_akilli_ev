/*
  MUCİT AKADEMİSİ | Akıllı Ev IoT Sistem Testi
  ---------------------------------------------
  Bu kod, ESP32'yi internete ve Firebase'e bağlar.
  Web arayüzünden gelen buton komutlarıyla gerçek donanımları çalıştırır.
*/

#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <ESP32Servo.h>
#include "DHT.h"
#include "soc/soc.h"             // Brownout (Güç düşümü) koruması için kütüphane
#include "soc/rtc_cntl_reg.h"    // Brownout (Güç düşümü) koruması için kütüphane

// --- WiFi AYARLARI ---
// DİKKAT: ESP32 5GHz ağları göremez! Bağlanmazsa 2.4GHz ağınızın adını yazın.
#define WIFI_SSID "FiberHGW_ZT2K3R_2.4GHz"
#define WIFI_PASSWORD "cjsgsrDtyc"

// --- FİREBASE AYARLARI ---
#define API_KEY "AIzaSyAR1cyO7pFPM5RLvO308uAcSyf5HaqAizM"
#define FIREBASE_PROJECT_ID "akillisera-b71d4"
#define APP_ID "master-iot-final-v3"

// --- PİN TANIMLAMALARI ---
#define LED_SALON 19   // Alt Kat Işığı
#define LED_MUTFAK 21  // Üst Kat Işığı
#define LED_BAHCE 22   // Bahçe Işığı
#define SERVO_PIN 14   // Bahçe Kapısı (D13 JTAG çakışmasını önlemek için D14'e alındı!)
#define BUZZER_PIN 27  // Siren / Müzik
#define FAN_PIN 23     // Vantilatör (DC Fan)
#define IR_SENSOR_PIN 26 // Hırsız Alarmı
#define DHTPIN 4       // Ev Sıcaklık Sensörü (DHT11 Data)
#define DHTTYPE DHT11  // Sensör Tipi

// TRAFİK SIKIŞMASINI ÖNLEMEK İÇİN ÇİFT KANAL KULLANIMI:
FirebaseData fbdoRead;  // Sadece web sitesindeki butonları okur
FirebaseData fbdoWrite; // Sadece sıcaklık ve alarm durumunu yazar
FirebaseAuth auth;
FirebaseConfig config;
Servo bahceKapisi;
DHT dht(DHTPIN, DHTTYPE);

String documentPath = "artifacts/" + String(APP_ID) + "/public/data/home/status";

void setup() {
  // ÇOK ÖNEMLİ: Pille çalışırken anlık voltaj düşmelerinde ESP32'nin reset atmasını engeller.
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  
  // Regülatörün ve pillerin gücü toparlaması için Wi-Fi'ı açmadan önce 2 saniye bekle
  delay(2000);

  Serial.begin(115200);
  dht.begin();
  
  // Pin Modları
  pinMode(LED_SALON, OUTPUT);
  pinMode(LED_MUTFAK, OUTPUT);
  pinMode(LED_BAHCE, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(IR_SENSOR_PIN, INPUT);
  
  // Servoyu başlat ve kapıyı kapat (Kısıtlayıcı sinyal aralıkları kaldırıldı!)
  ESP32PWM::allocateTimer(0);
  bahceKapisi.setPeriodHertz(50); // Standart SG90 servolar için 50Hz frekans ayarı
  bahceKapisi.attach(SERVO_PIN);  // Sadece pini tanımladık, motor artık kilitlenmeyecek!
  bahceKapisi.write(0);

  // WiFi Bağlantısı
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("WiFi baglaniliyor");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi baglandi! IP Adresi: ");
  Serial.println(WiFi.localIP());

  // Firebase Bağlantısı
  config.api_key = API_KEY;
  config.signer.test_mode = true;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  Serial.println("Firebase sistemine hazir.");
}

// 🚀 GÜVENLİ VE HATASIZ OKUMA YÖNTEMİ (Çökme Korumalı)
bool checkDeviceState(String payload, String deviceKey) {
  int keyPos = payload.indexOf("\"" + deviceKey + "\"");
  if (keyPos == -1) return false; 

  // İlgili cihazın değer (booleanValue) kısmını bul
  int boolPos = payload.indexOf("\"booleanValue\"", keyPos);
  
  // Eğer bulamazsa veya başka bir cihaza taşmışsa (mesafe 60'tan büyükse) iptal et
  if (boolPos == -1 || (boolPos - keyPos) > 60) return false; 
  
  // Sadece gerektiği kadarını kes (Hafıza Taşması / Çökme Koruması!)
  int maxLen = boolPos + 30;
  if (maxLen > payload.length()) {
    maxLen = payload.length();
  }
  
  String valStr = payload.substring(boolPos, maxLen);
  
  if (valStr.indexOf("true") > -1) {
    return true;
  }
  return false;
}

void loop() {
  if (!Firebase.ready()) return;

  static unsigned long lastCheck = 0;

  // Her 2 saniyede bir veritabanını (Web Sitenizi) kontrol et
  if (millis() - lastCheck > 2000) {
    lastCheck = millis();

    // Veriyi fbdoRead (Okuma Kanalı) ile Çek
    if (Firebase.Firestore.getDocument(&fbdoRead, FIREBASE_PROJECT_ID, "", documentPath.c_str(), "")) {
      String payload = fbdoRead.payload();
      
      // Web arayüzündeki butonların durumunu öğren (Eski ve yeni web isimleriyle tam uyumlu!)
      bool light1 = checkDeviceState(payload, "light1") || checkDeviceState(payload, "lightSalon");
      bool light2 = checkDeviceState(payload, "light2") || checkDeviceState(payload, "lightKitchen");
      bool gardenLight = checkDeviceState(payload, "gardenLight") || checkDeviceState(payload, "lightGarden");
      bool gate = checkDeviceState(payload, "gate");
      bool fan = checkDeviceState(payload, "fan");
      bool alarm = checkDeviceState(payload, "alarm");
      bool music = checkDeviceState(payload, "music");

      // --- DONANIMLARI WEB'DEN GELEN VERİYE GÖRE ÇALIŞTIR ---
      
      // 1. Işıklar
      digitalWrite(LED_SALON, light1 ? HIGH : LOW);
      digitalWrite(LED_MUTFAK, light2 ? HIGH : LOW);
      digitalWrite(LED_BAHCE, gardenLight ? HIGH : LOW);
      
      // 2. Bahçe Kapısı (Servo)
      if (gate) {
        bahceKapisi.write(90); // Açık
      } else {
        bahceKapisi.write(0);  // Kapalı
      }

      // 3. Vantilatör (DC Motor / Fan)
      digitalWrite(FAN_PIN, fan ? HIGH : LOW);

      // 4. Hırsız Alarmı (IR Sensörü Aktif Et)
      if (alarm) {
        // IR Sensörden okuma yap (LOW = Engel var/Hareket algılandı, HIGH = Engel yok)
        int irDurum = digitalRead(IR_SENSOR_PIN);

        // Biri sensörün önüne gelirse (Sensör LOW verirse) Alarm Ötsün!
        if (irDurum == LOW) {
           Serial.println("!!! ALARM TETIKLENDI: YAKINLASMA ALGILANDI (IR SENSÖR) !!!");
           
           // 1. Web sitesine fbdoWrite kanalı ile bilgi gönder
           String triggerStr = "{\"fields\":{\"alarmTriggered\":{\"booleanValue\":true}}}";
           Firebase.Firestore.patchDocument(&fbdoWrite, FIREBASE_PROJECT_ID, "", documentPath.c_str(), triggerStr.c_str(), "alarmTriggered");

           // 2. 10 Saniye Boyunca Siren Çal (Yanıp Sönen Işıklar Eşliğinde)
           for(int i=0; i<20; i++) { 
              digitalWrite(BUZZER_PIN, HIGH);
              digitalWrite(LED_BAHCE, HIGH); 
              delay(250);
              digitalWrite(BUZZER_PIN, LOW);
              digitalWrite(LED_BAHCE, LOW);
              delay(250);
           }

           // 3. 10 saniye bitince sistemi normale döndür ve alarmı kapat
           String resetTrigger = "{\"fields\":{\"alarmTriggered\":{\"booleanValue\":false}}}";
           Firebase.Firestore.patchDocument(&fbdoWrite, FIREBASE_PROJECT_ID, "", documentPath.c_str(), resetTrigger.c_str(), "alarmTriggered");
           
           String resetAlarm = "{\"fields\":{\"alarm\":{\"booleanValue\":false}}}";
           Firebase.Firestore.patchDocument(&fbdoWrite, FIREBASE_PROJECT_ID, "", documentPath.c_str(), resetAlarm.c_str(), "alarm");
           
           Serial.println("10 saniye doldu. Alarm susturuldu ve deaktif edildi.");
        }
      }

      // 5. Müzik Çalar Efekti (Daha Dün Annemizin Melodisi)
      static bool lastMusicState = false;
      if (music && !lastMusicState) {
         Serial.println("Muzik Caliyor: Daha Dun Annemizin...");
         
         // Notalar (Frekanslar): Do(262), Sol(392), La(440), Fa(349), Mi(330), Re(294)
         int melody[] = {262, 262, 392, 392, 440, 440, 392, 349, 349, 330, 330, 294, 294, 262};
         // Notaların çalma süreleri (milisaniye)
         int noteDurations[] = {300, 300, 300, 300, 300, 300, 600, 300, 300, 300, 300, 300, 300, 600};
         
         for (int i = 0; i < 14; i++) {
            tone(BUZZER_PIN, melody[i], noteDurations[i]);
            delay(noteDurations[i] + 50); // Notaların birbirine karışmaması için minik bir es (boşluk)
         }
      }
      
      // Eğer web'den müzik düğmesi kapatılırsa sistemi garanti sustur
      if (!music && lastMusicState) {
         noTone(BUZZER_PIN);
      }
      lastMusicState = music;

      // Durumu Ekrana Yazdır
      Serial.println("--------------------------------");
      Serial.println("Salon: " + String(light1 ? "ACIK" : "KAPALI") + " | Mutfak: " + String(light2 ? "ACIK" : "KAPALI"));
      Serial.println("Kapi: " + String(gate ? "ACIK" : "KAPALI") + " | Vantilator: " + String(fan ? "ACIK" : "KAPALI"));
      Serial.println("Alarm Sistemi: " + String(alarm ? "AKTIF" : "KAPALI") + " | Muzik: " + String(music ? "ACIK" : "KAPALI"));
    } else {
      Serial.println("HATA: Firebase'den veri okunamadi!");
      Serial.println(fbdoRead.errorReason());
    }
  }

  // Her 10 saniyede bir gerçek sıcaklığı Firebase'e gönder
  static unsigned long lastTempUpdate = 5000; // Okuma (2sn) ile çarpışmasını önlemek için 5 saniye ofset!
  if (millis() - lastTempUpdate > 10000) {
    lastTempUpdate = millis();
    float t = dht.readTemperature();
    
    // SENSÖR ÇALIŞIYOR MU KONTROLÜ (Seri Porta Her Zaman Yazdır)
    Serial.print("[Sensör Okuması] Ham Sıcaklık: ");
    Serial.print(t);
    Serial.println(" C");

    // Sensör kopuksa veya okuyamıyorsa uyar ve işlemi iptal et
    if (isnan(t)) {
      Serial.println("HATA: DHT11'den veri okunamiyor! Baglantiyi veya pini kontrol edin.");
      return; 
    }

    // KÜTÜPHANE VERSİYONU HATASI ÇÖZÜMÜ
    if (t > 80) {
      t = t / 10.0; 
    }
    
    // FİLTRE GERİ EKLENDİ: Sadece sıcaklık değiştiğinde Web Sitesine gönder (Sistemi yormaz)
    static float lastSentTemp = -999.0;
    if (abs(t - lastSentTemp) >= 0.1) {
      lastSentTemp = t; // Yeni değeri hafızaya al
      String tempUpdateStr = "{\"fields\":{\"temp\":{\"doubleValue\":" + String(t) + "}}}";
      Firebase.Firestore.patchDocument(&fbdoWrite, FIREBASE_PROJECT_ID, "", documentPath.c_str(), tempUpdateStr.c_str(), "temp");
      Serial.println(">>> Ev Sicakligi Web Sitesine Guncellendi: " + String(t) + " C");
    } else {
      // Değişim yoksa arkada çalıştığını göstermek için log yazdır ama bulutu meşgul etme
      Serial.println(">>> Sicaklik degismedi, buluta gonderilmedi.");
    }
  }
}