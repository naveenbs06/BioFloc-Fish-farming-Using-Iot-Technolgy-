#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP32Servo.h>
#include "time.h"

// ---------------- PIN CONFIG ----------------
const int PIN_ONEWIRE     = 5;    // DS18B20 Temperature
const int PIN_TURBIDITY   = 34;   // Turbidity Sensor
const int PIN_SERVO       = 13;   // Servo Motor
const int PIN_PUMP_OUT    = 35;   // Drain Pump Relay
const int PIN_PUMP_IN     = 32;   // Refill Pump Relay
const int PIN_BUTTON_FEED = 14;   // Manual Feed Button
const int PIN_FLOAT       = 27;   // Float Switch (Pressed = full)

// ---------------- LCD CONFIG ----------------
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ---------------- DS18B20 ----------------
OneWire oneWire(PIN_ONEWIRE);
DallasTemperature sensors(&oneWire);

// ---------------- Servo ----------------
Servo feederServo;
const int SERVO_OPEN_ANGLE = 0;
const int SERVO_CLOSED_ANGLE = 85;

// ---------------- WiFi + Time ----------------
const char* ssid = "Pavan";
const char* password = "12345678";
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800; // GMT +5:30 (India)
const int daylightOffset_sec = 0;

// ---------------- ThingSpeak ----------------
String apiKey = "V8TD2OS1AMJ9CZ3M";
const char* server = "http://api.thingspeak.com/update";

// ---------------- Feeding Schedule ----------------
struct FeedTime {
  int hour;
  int minute;
  bool hasFed;
};

// 🕒 Two scheduled feeding times
FeedTime feeds[] = {
  {9, 0, false},   // Feed 1 at 09:00
  {17, 8, false}   // Feed 2 at 18:00 (6 PM)
};
const int NUM_FEEDS = 2;

// ---------------- Turbidity Threshold ----------------
const int TURBIDITY_THRESHOLD = 1500; // Adjust based on your sensor

// ---------------- Display & Upload Timing ----------------
unsigned long lastDisplayUpdate = 0;
unsigned long lastThingSpeakUpdate = 0;
const unsigned long displayInterval = 2000;
const unsigned long thingSpeakInterval = 20000; // 20 seconds

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);

  pinMode(PIN_TURBIDITY, INPUT);
  pinMode(PIN_PUMP_OUT, OUTPUT);
  pinMode(PIN_PUMP_IN, OUTPUT);
  pinMode(PIN_BUTTON_FEED, INPUT_PULLUP);
  pinMode(PIN_FLOAT, INPUT_PULLUP);

  // ✅ Set relays OFF initially (Active LOW relays)
  digitalWrite(PIN_PUMP_OUT, HIGH); 
  digitalWrite(PIN_PUMP_IN, HIGH);

  feederServo.attach(PIN_SERVO);
  feederServo.write(SERVO_CLOSED_ANGLE);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Connecting...");

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  lcd.clear();
  lcd.print("WiFi Connected");
  delay(1000);
  lcd.clear();

  sensors.begin();

  Serial.println("Setup Complete ✅");
}

// ---------------- Feeding Function ----------------
void performFeeding(bool manual = false) {
  Serial.println(manual ? "Manual Feeding..." : "Scheduled Feeding...");
  feederServo.write(SERVO_OPEN_ANGLE);
  delay(700);
  feederServo.write(SERVO_CLOSED_ANGLE);
  delay(200);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(manual ? "Manual Feed" : "Auto Feed");
  lcd.setCursor(0, 1);
  lcd.print("Done!");
  delay(1500);
  lcd.clear();
}

// ---------------- Upload to ThingSpeak ----------------
void uploadToThingSpeak(float temperature, int turbidity) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = server;
    url += "?api_key=" + apiKey;
    url += "&field1=" + String(temperature, 2);
    url += "&field2=" + String(turbidity);

    http.begin(url);
    int httpCode = http.GET();
    if (httpCode > 0) {
      Serial.println("ThingSpeak Updated ✅ Code: " + String(httpCode));
    } else {
      Serial.println("ThingSpeak Update Failed ❌");
    }
    http.end();
  }
}

// ---------------- Loop ----------------
void loop() {
  // ---- Temperature ----
  sensors.requestTemperatures();
  float temperature = sensors.getTempCByIndex(0);

  // ---- Turbidity ----
  int turbidity = analogRead(PIN_TURBIDITY);

  // ---- Time ----
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to get time");
    return;
  }

  int currentHour = timeinfo.tm_hour;
  int currentMinute = timeinfo.tm_min;

  // ---- Scheduled Feeding ----
  for (int i = 0; i < NUM_FEEDS; i++) {
    if (currentHour == feeds[i].hour && currentMinute == feeds[i].minute) {
      if (!feeds[i].hasFed) {
        performFeeding(false);
        feeds[i].hasFed = true;
      }
    } else {
      feeds[i].hasFed = false;
    }
  }

  // ---- Manual Feed Button ----
  if (digitalRead(PIN_BUTTON_FEED) == LOW) {
    delay(50);
    if (digitalRead(PIN_BUTTON_FEED) == LOW) {
      performFeeding(true);
      while (digitalRead(PIN_BUTTON_FEED) == LOW);
      delay(100);
    }
  }

  // ---- Float Switch ----
  bool waterFull = (digitalRead(PIN_FLOAT) == LOW); // Pressed = full

  // ---- Pump Control (fixed logic) ----
  if (turbidity > TURBIDITY_THRESHOLD) {
    // Water is dirty → Drain ON
    digitalWrite(PIN_PUMP_OUT, LOW);  // Active LOW → ON
    digitalWrite(PIN_PUMP_IN, HIGH);  // Refill OFF
  } else {
    // Water is clean → Drain OFF
    digitalWrite(PIN_PUMP_OUT, HIGH);

    // Refill only if tank not full
    if (!waterFull)
      digitalWrite(PIN_PUMP_IN, LOW); // ON
    else
      digitalWrite(PIN_PUMP_IN, HIGH); // OFF
  }

  // ---- LCD Update ----
  if (millis() - lastDisplayUpdate > displayInterval) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("T:");
    lcd.print(temperature, 1);
    lcd.print("C Tu:");
    lcd.print(turbidity);

    lcd.setCursor(0, 1);
    lcd.printf("Time:%02d:%02d", currentHour, currentMinute);
    lastDisplayUpdate = millis();
  }

  // ---- ThingSpeak Upload ----
  if (millis() - lastThingSpeakUpdate > thingSpeakInterval) {
    uploadToThingSpeak(temperature, turbidity);
    lastThingSpeakUpdate = millis();
  }

  delay(200);
}