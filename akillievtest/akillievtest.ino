/*
  MUCİT AKADEMİSİ | Akıllı Ev IoT Sistemi (ORTAK ANOT RGB FİNAL)
  -------------------------------------------------------------
  Bu kod, Ortak Anot RGB LED ile çalışacak şekilde optimize edilmiştir.
  Pinler: RED=15, GREEN=32, BLUE=23 (Test koduna göre güncellendi)
  Alt Kat: Salon (D19) ve Oda 2 (D5) olmak üzere iki ayrı aydınlatma mevcuttur.
  D21 Pini iptal edilmiştir. RGB kontrolü 'light2' üzerinden yapılır.
*/

#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <ESP32Servo.h>
#include "DHT.h"
#include "soc/soc.h"             
#include "soc/rtc_cntl_reg.h"    

// --- WiFi AYARLARI ---
#define WIFI_SSID "FiberHGW_ZT2K3R_2.4GHz"
#define WIFI_PASSWORD "cjsgsrDtyc"

// --- FİREBASE AYARLARI ---
#define API_KEY "AIzaSyAR1cyO7pFPM5RLvO308uAcSyf5HaqAizM"
#define FIREBASE_PROJECT_ID "akillisera-b71d4"
#define APP_ID "master-iot-final-v3"

// --- PİN TANIMLAMALARI ---
#define LED_SALON 19     // Alt Kat 1. Oda (Salon)
#define LED_ODA2 5       // Alt Kat 2. Oda (Yatak Odası/Çocuk Odası)
#define LED_BAHCE 22     // Dış Aydınlatma
#define SERVO_PIN 33     // Servo için en stabil pin D33
#define BUZZER_PIN 27    
#define IR_SENSOR_PIN 26 
#define DHTPIN 4         
#define DHTTYPE DHT11

// --- RGB LED PİNLERİ (Ortak Anot Test Koduna Göre) ---
#define RGB_R 15  
#define RGB_G 32  
#define RGB_B 23  

FirebaseData fbdoRead, fbdoWrite;
FirebaseAuth auth;
FirebaseConfig config;
Servo bahceKapisi;
DHT dht(DHTPIN, DHTTYPE);

String documentPath = "artifacts/" + String(APP_ID) + "/public/data/home/status";

// Ortak Anot LED için renk uygulama fonksiyonu
void applyRGBColor(String hex) {
  if (hex.length() < 6) return;
  if (hex.startsWith("#")) hex = hex.substring(1);
  
  long number = strtol(hex.c_str(), NULL, 16);
  int r = (number >> 16) & 0xFF;
  int g = (number >> 8) & 0xFF;
  int b = number & 0xFF;
  
  // ORTAK ANOT olduğu için değerleri 255'ten çıkarıyoruz (Inverted Logic)
  // analogWrite ESP32'de PWM sinyali gönderir.
  analogWrite(RGB_R, 255 - r);
  analogWrite(RGB_G, 255 - g);
  analogWrite(RGB_B, 255 - b);
}

// Firebase'den Mantıksal (Boolean) veri okuma
bool checkState(String payload, String key) {
  int pos = payload.indexOf("\"" + key + "\"");
  if (pos == -1) return false;
  int boolPos = payload.indexOf("\"booleanValue\"", pos);
  if (boolPos == -1 || (boolPos - pos) > 60) return false;
  return payload.substring(boolPos, boolPos + 30).indexOf("true") > -1;
}

// Firebase'den Yazı (Renk Kodu) veri okuma
String checkString(String payload, String key) {
  int pos = payload.indexOf("\"" + key + "\"");
  if (pos == -1) return "";
  int valPos = payload.indexOf("\"stringValue\"", pos);
  if (valPos == -1) return "";
  int start = payload.indexOf("\"", valPos + 15);
  int end = payload.indexOf("\"", start + 1);
  return payload.substring(start + 1, end);
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  delay(2000);
  Serial.begin(115200);
  dht.begin();
  
  pinMode(LED_SALON, OUTPUT);
  pinMode(LED_ODA2, OUTPUT);
  pinMode(LED_BAHCE, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(IR_SENSOR_PIN, INPUT);
  
  pinMode(RGB_R, OUTPUT);
  pinMode(RGB_G, OUTPUT);
  pinMode(RGB_B, OUTPUT);

  // Ortak Anot LED: Başlangıçta hepsini HIGH yaparak söndür
  digitalWrite(RGB_R, HIGH);
  digitalWrite(RGB_G, HIGH);
  digitalWrite(RGB_B, HIGH);

  ESP32PWM::allocateTimer(0);
  bahceKapisi.setPeriodHertz(50);
  bahceKapisi.attach(SERVO_PIN, 500, 2400); 
  bahceKapisi.write(0);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setSleep(false); 
  WiFi.setAutoReconnect(true);

  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi Baglandi.");

  config.api_key = API_KEY;
  config.signer.test_mode = true;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  Serial.println("Firebase Hazir.");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) { WiFi.reconnect(); delay(1000); return; }
  if (!Firebase.ready()) return;

  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 2000) {
    lastCheck = millis();

    if (Firebase.Firestore.getDocument(&fbdoRead, FIREBASE_PROJECT_ID, "", documentPath.c_str(), "")) {
      String payload = fbdoRead.payload();
      
      bool l1 = checkState(payload, "light1");      // Salon Işığı
      bool l2 = checkState(payload, "light2");      // RGB Mood Aydınlatma
      bool l3 = checkState(payload, "light3");      // Alt Kat 2. Oda Işığı
      bool gl = checkState(payload, "gardenLight"); // Bahçe Işığı
      bool gate = checkState(payload, "gate");
      bool alarm = checkState(payload, "alarm");
      bool music = checkState(payload, "music");
      String rgbHex = checkString(payload, "rgbHex");

      // Işıkları Kontrol Et
      digitalWrite(LED_SALON, l1 ? HIGH : LOW);
      digitalWrite(LED_ODA2, l3 ? HIGH : LOW);
      digitalWrite(LED_BAHCE, gl ? HIGH : LOW);
      
      // RGB KONTROLÜ (Ortak Anot Mantığıyla)
      if (l2) {
        applyRGBColor(rgbHex); 
      } else {
        // Kapalıyken söndür (Ortak Anotta HIGH söndürür)
        digitalWrite(RGB_R, HIGH);
        digitalWrite(RGB_G, HIGH);
        digitalWrite(RGB_B, HIGH);
      }
      
      // Servo (0 - 170 Derece)
      static bool lastG = false;
      if (gate != lastG) { bahceKapisi.write(gate ? 170 : 0); lastG = gate; }

      // Hırsız Alarmı
      if (alarm && digitalRead(IR_SENSOR_PIN) == LOW) {
           Serial.println("!!! ALARM TETIKLENDI !!!");
           Firebase.Firestore.patchDocument(&fbdoWrite, FIREBASE_PROJECT_ID, "", documentPath.c_str(), "{\"fields\":{\"alarmTriggered\":{\"booleanValue\":true}}}", "alarmTriggered");
           for(int i=0; i<10; i++) { 
              tone(BUZZER_PIN, 1200); delay(500);
              tone(BUZZER_PIN, 800);  delay(500);
           }
           noTone(BUZZER_PIN);
           Firebase.Firestore.patchDocument(&fbdoWrite, FIREBASE_PROJECT_ID, "", documentPath.c_str(), "{\"fields\":{\"alarmTriggered\":{\"booleanValue\":false},\"alarm\":{\"booleanValue\":false}}}", "alarmTriggered,alarm");
      }

      // Müzik Çalar
      static bool lastM = false;
      if (music && !lastM) {
         int m[] = {262, 262, 392, 392, 440, 440, 392, 349, 349, 330, 330, 294, 294, 262};
         for (int i=0; i<14; i++) { tone(BUZZER_PIN, m[i], 300); delay(350); }
      }
      if (!music && lastM) noTone(BUZZER_PIN);
      lastM = music;
    }
  }

  // Sıcaklık Güncelleme
  static unsigned long lastT = 5000;
  if (millis() - lastT > 10000) {
    lastT = millis();
    float t = dht.readTemperature();
    if (!isnan(t)) {
      if (t > 80) t /= 10.0;
      Firebase.Firestore.patchDocument(&fbdoWrite, FIREBASE_PROJECT_ID, "", documentPath.c_str(), "{\"fields\":{\"temp\":{\"doubleValue\":" + String(t) + "}}}", "temp");
    }
  }
}