/*
  MUCİT AKADEMİSİ | Akıllı Ev IoT Sistemi (RGB Mood Aydınlatma)
  -------------------------------------------------------------
  Bu kod, Ortak Katot RGB LED ile çalışacak şekilde optimize edilmiştir.
  Fan iptal edilmiş, yerine D23-D32-D15 pinleri üzerinden RGB LED eklenmiştir.
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
#define LED_SALON 19     
#define LED_RGB_ENABLE 21 // Üst kat butonu ile RGB'yi açıp kapatırız
#define LED_BAHCE 22     
#define SERVO_PIN 14     // Bahçe kapısı servosu
#define BUZZER_PIN 27    
#define IR_SENSOR_PIN 26 
#define DHTPIN 4         
#define DHTTYPE DHT11

// --- RGB LED PİNLERİ (Ortak Katot) ---
#define RGB_R 23  
#define RGB_G 32  
#define RGB_B 15  

FirebaseData fbdoRead, fbdoWrite;
FirebaseAuth auth;
FirebaseConfig config;
Servo bahceKapisi;
DHT dht(DHTPIN, DHTTYPE);

String documentPath = "artifacts/" + String(APP_ID) + "/public/data/home/status";

// Web sitesinden gelen HEX (#RRGGBB) kodunu LED'e yansıtan fonksiyon
void applyRGBColor(String hex) {
  if (hex.length() < 6) return;
  if (hex.startsWith("#")) hex = hex.substring(1);
  
  long number = strtol(hex.c_str(), NULL, 16);
  int r = (number >> 16) & 0xFF;
  int g = (number >> 8) & 0xFF;
  int b = number & 0xFF;
  
  // Ortak Katot olduğu için direkt PWM değerini (0-255) gönderiyoruz
  analogWrite(RGB_R, r);
  analogWrite(RGB_G, g);
  analogWrite(RGB_B, b);
}

// Firebase'den Mantıksal (True/False) veri okuma
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
  pinMode(LED_RGB_ENABLE, OUTPUT);
  pinMode(LED_BAHCE, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(IR_SENSOR_PIN, INPUT);
  
  pinMode(RGB_R, OUTPUT);
  pinMode(RGB_G, OUTPUT);
  pinMode(RGB_B, OUTPUT);

  ESP32PWM::allocateTimer(0);
  bahceKapisi.setPeriodHertz(50);
  bahceKapisi.attach(SERVO_PIN, 500, 2400); 
  bahceKapisi.write(0);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setSleep(false); // WiFi kopmalarını engeller
  WiFi.setAutoReconnect(true);

  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi Baglandi.");

  config.api_key = API_KEY;
  config.signer.test_mode = true;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) { WiFi.reconnect(); delay(1000); return; }
  if (!Firebase.ready()) return;

  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 2000) {
    lastCheck = millis();

    if (Firebase.Firestore.getDocument(&fbdoRead, FIREBASE_PROJECT_ID, "", documentPath.c_str(), "")) {
      String payload = fbdoRead.payload();
      
      bool l1 = checkState(payload, "light1");
      bool l2 = checkState(payload, "light2"); // RGB Mood Aydınlatma Kontrolü
      bool gl = checkState(payload, "gardenLight");
      bool gate = checkState(payload, "gate");
      bool alarm = checkState(payload, "alarm");
      bool music = checkState(payload, "music");
      String rgbHex = checkString(payload, "rgbHex");

      digitalWrite(LED_SALON, l1 ? HIGH : LOW);
      digitalWrite(LED_BAHCE, gl ? HIGH : LOW);
      
      // RGB KONTROLÜ
      if (l2) {
        applyRGBColor(rgbHex); // Web'den seçilen rengi yak
      } else {
        applyRGBColor("#000000"); // Kapalıyken söndür
      }
      
      // Servo (0 - 170 Derece)
      static bool lastG = false;
      if (gate != lastG) {
        bahceKapisi.write(gate ? 170 : 0);
        lastG = gate;
      }

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