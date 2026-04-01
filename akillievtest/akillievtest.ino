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

// --- WiFi AYARLARI ---
// DİKKAT: ESP32 5GHz ağları göremez! Bağlanmazsa 2.4GHz ağınızın adını yazın.
#define WIFI_SSID "FiberHGW_ZT2K3R_5GHz"
#define WIFI_PASSWORD "cjsgsrDtyc"

// --- FİREBASE AYARLARI ---
#define API_KEY "AIzaSyAR1cyO7pFPM5RLvO308uAcSyf5HaqAizM"
#define FIREBASE_PROJECT_ID "akillisera-b71d4"
#define APP_ID "master-iot-final-v3"

// --- PİN TANIMLAMALARI ---
#define LED_SALON 19   // Alt Kat Işığı
#define LED_MUTFAK 21  // Üst Kat Işığı
#define LED_BAHCE 22   // Bahçe Işığı
#define SERVO_PIN 13   // Bahçe Kapısı
#define BUZZER_PIN 12  // Siren / Müzik
#define FAN_PIN 23     // Vantilatör (DC Fan)
#define IR_SENSOR_PIN 5 // Hırsız Alarmı (IR Engel Sensörü OUT pini)
#define DHTPIN 4       // Ev Sıcaklık Sensörü (DHT11 Data)
#define DHTTYPE DHT11  // Sensör Tipi

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
Servo bahceKapisi;
DHT dht(DHTPIN, DHTTYPE);

String documentPath = "artifacts/" + String(APP_ID) + "/public/data/home/status";

void setup() {
  Serial.begin(115200);
  dht.begin();
  
  // Pin Modları
  pinMode(LED_SALON, OUTPUT);
  pinMode(LED_MUTFAK, OUTPUT);
  pinMode(LED_BAHCE, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(IR_SENSOR_PIN, INPUT);
  
  // Servoyu başlat ve kapıyı kapat
  bahceKapisi.attach(SERVO_PIN);
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

// Firebase'den gelen verinin içindeki "true" / "false" bilgisini bulan fonksiyon
bool checkDeviceState(String payload, String deviceKey) {
  int pos = payload.indexOf("\"" + deviceKey + "\"");
  if (pos != -1) {
    String sub = payload.substring(pos, pos + 50); // İlgili kelimenin etrafını kes
    if (sub.indexOf("true") > -1) {
      return true;
    }
  }
  return false;
}

void loop() {
  if (!Firebase.ready()) return;

  static unsigned long lastCheck = 0;

  // Her 2 saniyede bir veritabanını (Web Sitenizi) kontrol et
  if (millis() - lastCheck > 2000) {
    lastCheck = millis();

    // Veriyi Firebase'den Çek
    if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), "")) {
      String payload = fbdo.payload();
      
      // Web arayüzündeki butonların durumunu öğren (İsimler web sitesi ile EŞLEŞTİRİLDİ!)
      bool light1 = checkDeviceState(payload, "lightSalon");
      bool light2 = checkDeviceState(payload, "lightKitchen");
      bool gardenLight = checkDeviceState(payload, "lightGarden");
      bool gate = checkDeviceState(payload, "gate");
      bool fan = checkDeviceState(payload, "fan");
      bool alarm = checkDeviceState(payload, "alarm");

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
           
           // 1. Web sitesine "alarm çaldı" bilgisini gönder (Tam ekran kırmızıyı tetikler)
           String triggerStr = "{\"fields\":{\"alarmTriggered\":{\"booleanValue\":true}}}";
           Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), triggerStr.c_str(), "alarmTriggered");

           // 2. 10 Saniye Boyunca Siren Çal (Yanıp Sönen Işıklar Eşliğinde)
           for(int i=0; i<20; i++) { // 20 adım * 500ms = 10 Saniye
              digitalWrite(BUZZER_PIN, HIGH);
              digitalWrite(LED_BAHCE, HIGH); // Bahçe ışığı da siren gibi flaş yapsın
              delay(250);
              digitalWrite(BUZZER_PIN, LOW);
              digitalWrite(LED_BAHCE, LOW);
              delay(250);
           }

           // 3. 10 saniye bitince sistemi normale döndür ve alarmı kapat
           String resetTrigger = "{\"fields\":{\"alarmTriggered\":{\"booleanValue\":false}}}";
           Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), resetTrigger.c_str(), "alarmTriggered");
           
           String resetAlarm = "{\"fields\":{\"alarm\":{\"booleanValue\":false}}}";
           Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), resetAlarm.c_str(), "alarm");
           
           Serial.println("10 saniye doldu. Alarm susturuldu ve deaktif edildi.");
        }
      }

      // Durumu Ekrana Yazdır
      Serial.println("--------------------------------");
      Serial.println("Salon: " + String(light1 ? "ACIK" : "KAPALI") + " | Mutfak: " + String(light2 ? "ACIK" : "KAPALI"));
      Serial.println("Kapi: " + String(gate ? "ACIK" : "KAPALI") + " | Vantilator: " + String(fan ? "ACIK" : "KAPALI"));
      Serial.println("Alarm Sistemi: " + String(alarm ? "AKTIF" : "KAPALI"));
    } else {
      Serial.println("HATA: Firebase'den veri okunamadi!");
      Serial.println(fbdo.errorReason());
    }
  }

  // Her 10 saniyede bir gerçek sıcaklığı Firebase'e gönder
  static unsigned long lastTempUpdate = 0;
  if (millis() - lastTempUpdate > 10000) {
    lastTempUpdate = millis();
    float t = dht.readTemperature();
    
    // Sensör doğru okuduysa veriyi buluta yolla
    if (!isnan(t)) {
      String tempUpdateStr = "{\"fields\":{\"temp\":{\"doubleValue\":" + String(t) + "}}}";
      Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), tempUpdateStr.c_str(), "temp");
      Serial.println(">>> Ev Sicakligi Guncellendi: " + String(t) + " C");
    }
  }
}