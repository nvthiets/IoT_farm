#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <time.h> 
#include <PubSubClient.h> 

// --- CẤU HÌNH WIFI ---
const char* ssid = "nvthiets"; 
const char* password = "79831204@";

// --- CẤU HÌNH MQTT BROKER ---
const char* mqtt_server = "172.20.10.2"; 
const int mqtt_port = 1883; 
WiFiClient espClient;
PubSubClient mqttClient(espClient);
unsigned long lastMqttReconnectAttempt = 0;

// --- CẤU HÌNH THỜI GIAN ---
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7 * 3600; 
const int   daylightOffset_sec = 0;

// --- ĐỊNH NGHĨA CHÂN (PINS) ---
#define DHTPIN 32
#define DHTTYPE DHT11
#define LDR_PIN 33
#define SOIL_PIN 34    
#define RELAY1_PIN 14  // Quạt
#define RELAY2_PIN 26  // Bơm
#define BUZZER_PIN 13  // Còi
#define BTN1_PIN 5     // Nút MENU
#define BTN2_PIN 17    // Nút UP
#define BTN3_PIN 16    // Nút DOWN

DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 20, 4); 

// --- BIẾN TOÀN CỤC ---
float currentTemp = 0, currentHum = 0;
int currentLight = 0, currentSoil = 0;

int tempThreshold = 30; 
int soilThreshold = 40; 

int menuMode = 0; 
bool updateDisplay = true; 
bool isAutoMode = true; 

// Biến cho Còi báo 5 giây
bool isBuzzerActive = false;
unsigned long buzzerStartTime = 0;
bool lastTempExceeded = false;
bool lastSoilExceeded = false;

unsigned long lastSensorRead = 0;
unsigned long lastClockTick = 0; 

void publishData(const char* timeStr);

// --- XỬ LÝ LỆNH TỪ MQTT ---
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (int i = 0; i < length; i++) { msg += (char)payload[i]; }
  String topicStr = String(topic);

  if (topicStr == "smartfarm/control/mode") {
    if (msg == "AUTO") isAutoMode = true;
    else if (msg == "MANUAL") isAutoMode = false;

    updateDisplay = true; // Cập nhật màn hình LCD
    
    // THÊM DÒNG NÀY: Phát ngược trạng thái lại lên MQTT để đồng bộ TẤT CẢ các giao diện Web (Laptop, Điện thoại...)
    mqttClient.publish("smartfarm/status/mode", isAutoMode ? "AUTO" : "MANUAL");
  }
  if (topicStr == "smartfarm/control/threshold/temp") { tempThreshold = msg.toInt(); updateDisplay = true; }
  if (topicStr == "smartfarm/control/threshold/soil") { soilThreshold = msg.toInt(); updateDisplay = true; }

  if (!isAutoMode) {
    if (topicStr == "smartfarm/control/relay1") {
      digitalWrite(RELAY1_PIN, msg == "ON" ? HIGH : LOW); 
      updateDisplay = true;
      mqttClient.publish("smartfarm/status/relay1", msg == "ON" ? "ON" : "OFF");
    }
    if (topicStr == "smartfarm/control/relay2") {
      digitalWrite(RELAY2_PIN, msg == "ON" ? HIGH : LOW); 
      updateDisplay = true;
      mqttClient.publish("smartfarm/status/relay2", msg == "ON" ? "ON" : "OFF");
    }
  }
}

boolean reconnectMQTT() {
  if (mqttClient.connect("ESP32_SmartFarm_Client")) {
    mqttClient.subscribe("smartfarm/control/mode");
    mqttClient.subscribe("smartfarm/control/threshold/temp");
    mqttClient.subscribe("smartfarm/control/threshold/soil");
    mqttClient.subscribe("smartfarm/control/relay1");
    mqttClient.subscribe("smartfarm/control/relay2");
    return true;
  }
  return false;
}

// --- XỬ LÝ NÚT NHẤN ---
void handleButtons() {
  unsigned long now = millis();
  static unsigned long btnMenuPressTime = 0;
  static bool isMenuPressed = false;
  static bool longPressHandled = false;

  // 1. NÚT MENU
  if (digitalRead(BTN1_PIN) == LOW) {
    if (!isMenuPressed) {
      delay(30); 
      if (digitalRead(BTN1_PIN) == LOW) { 
        isMenuPressed = true;
        btnMenuPressTime = millis();
        longPressHandled = false;
      }
    } else {
      if (!longPressHandled && (millis() - btnMenuPressTime >= 3000)) {
        isAutoMode = !isAutoMode; 
        lcd.clear(); 
        updateDisplay = true;
        longPressHandled = true; 
        if (mqttClient.connected()) mqttClient.publish("smartfarm/status/mode", isAutoMode ? "AUTO" : "MANUAL");
      }
    }
  } else {
    if (isMenuPressed) {
      delay(30); 
      if (digitalRead(BTN1_PIN) == HIGH) {
        isMenuPressed = false;
        if (!longPressHandled) {
          menuMode++; 
          if (menuMode > 2) menuMode = 0; 
          lcd.clear(); 
          updateDisplay = true;
        }
      }
    }
  }

  // 2. NÚT UP (Chỉnh ngưỡng hoặc bật Relay 1)
  static unsigned long lastBtnUp = 0;
  if (digitalRead(BTN2_PIN) == LOW && (now - lastBtnUp > 250)) {
    if (menuMode == 1) tempThreshold++; 
    else if (menuMode == 2) soilThreshold++;
    else if (menuMode == 0 && !isAutoMode) {
      bool state = digitalRead(RELAY1_PIN);
      digitalWrite(RELAY1_PIN, !state);
      if (mqttClient.connected()) mqttClient.publish("smartfarm/status/relay1", !state ? "ON" : "OFF");
    }
    updateDisplay = true; lastBtnUp = now;
  }

  // 3. NÚT DOWN (Chỉnh ngưỡng hoặc bật Relay 2)
  static unsigned long lastBtnDown = 0;
  if (digitalRead(BTN3_PIN) == LOW && (now - lastBtnDown > 250)) {
    if (menuMode == 1) tempThreshold--; 
    else if (menuMode == 2) soilThreshold--;
    else if (menuMode == 0 && !isAutoMode) {
      bool state = digitalRead(RELAY2_PIN);
      digitalWrite(RELAY2_PIN, !state);
      if (mqttClient.connected()) mqttClient.publish("smartfarm/status/relay2", !state ? "ON" : "OFF");
    }
    updateDisplay = true; lastBtnDown = now;
  }
}

// --- ĐIỀU KHIỂN HỆ THỐNG ---
void controlSystem() {
  if (isAutoMode) {
    bool isTempHigh = (currentTemp >= tempThreshold && currentTemp != 0);
    bool isSoilDry = (currentSoil <= soilThreshold && currentSoil != 0);

    if (digitalRead(RELAY1_PIN) != isTempHigh) {
      digitalWrite(RELAY1_PIN, isTempHigh); updateDisplay = true; 
    }
    if (digitalRead(RELAY2_PIN) != isSoilDry) {
      digitalWrite(RELAY2_PIN, isSoilDry); updateDisplay = true;
    }

    // Logic Còi hú 5 giây
    if ((isTempHigh && !lastTempExceeded) || (isSoilDry && !lastSoilExceeded)) {
      isBuzzerActive = true;
      buzzerStartTime = millis();
      ledcWrite(BUZZER_PIN, 5);
      if (mqttClient.connected()) mqttClient.publish("smartfarm/status/buzzer", "ON");
    }
    
    lastTempExceeded = isTempHigh;
    lastSoilExceeded = isSoilDry;
  }
}

// --- HIỂN THỊ LCD ---
void drawLCD() {
  if (!updateDisplay) return;

  if (menuMode == 0) {
    String stQ = digitalRead(RELAY1_PIN) ? "ON" : "OFF";
    String stB = digitalRead(RELAY2_PIN) ? "ON" : "OFF";
    String tStr = String((int)currentTemp) + (char)223 + "C";
    String hStr = String((int)currentHum) + "%";
    String lStr = String(currentLight) + "%";
    String sStr = String(currentSoil) + "%";

    struct tm timeinfo;
    char timeStr[9] = "--:--:--"; 
    if (getLocalTime(&timeinfo, 10)) sprintf(timeStr, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    lcd.setCursor(0, 0); lcd.print(isAutoMode ? "[A]" : "[M]"); 
    lcd.setCursor(6, 0); lcd.print(timeStr);
    lcd.setCursor(19, 0); lcd.print(mqttClient.connected() ? "W" : "!"); 

    char rowBuf[21];
    sprintf(rowBuf, "Temp:%-5s|Humi:%-4s", tStr.c_str(), hStr.c_str());
    lcd.setCursor(0, 1); lcd.print(rowBuf);
    
    sprintf(rowBuf, "Quat:%-5s|Bom :%-4s", stQ.c_str(), stB.c_str());
    lcd.setCursor(0, 2); lcd.print(rowBuf);
    
    sprintf(rowBuf, "Light:%-4s|Soil:%-4s", lStr.c_str(), sStr.c_str());
    lcd.setCursor(0, 3); lcd.print(rowBuf);
  } 
  else if (menuMode == 1) {
    lcd.setCursor(0, 0); lcd.print("--- CAI DAT NGUONG -");
    lcd.setCursor(0, 1); lcd.print("Che do: BAT QUAT    ");
    lcd.setCursor(0, 2); lcd.print("Nhiet do: "); lcd.print(tempThreshold); lcd.write(223); lcd.print("C      ");
    lcd.setCursor(0, 3); lcd.print("[UP/DOWN] Thay doi  ");
  }
  else if (menuMode == 2) {
    lcd.setCursor(0, 0); lcd.print("--- CAI DAT NGUONG -");
    lcd.setCursor(0, 1); lcd.print("Che do: BAT BOM     ");
    lcd.setCursor(0, 2); lcd.print("Am dat  : "); lcd.print(soilThreshold); lcd.print("%       ");
    lcd.setCursor(0, 3); lcd.print("[UP/DOWN] Thay doi  ");
  }
  updateDisplay = false; 
}

void setup_wifi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY1_PIN, OUTPUT); pinMode(RELAY2_PIN, OUTPUT); pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BTN1_PIN, INPUT_PULLUP); pinMode(BTN2_PIN, INPUT_PULLUP); pinMode(BTN3_PIN, INPUT_PULLUP);
  pinMode(LDR_PIN, INPUT); pinMode(SOIL_PIN, INPUT);
  
  ledcAttach(BUZZER_PIN, 2000, 8);

  lcd.init(); lcd.backlight(); lcd.print("Khoi dong...");
  dht.begin();
  setup_wifi(); 

  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);
  lcd.clear();
}

void loop() {
  handleButtons(); 
  unsigned long now = millis();

  // Kiểm tra tắt còi sau 5 giây
  if (isBuzzerActive && (now - buzzerStartTime >= 5000)) {
    isBuzzerActive = false;
    ledcWrite(BUZZER_PIN, 0);
    if (mqttClient.connected()) mqttClient.publish("smartfarm/status/buzzer", "OFF");
  }

  if (!mqttClient.connected()) {
    if (now - lastMqttReconnectAttempt > 5000) {
      lastMqttReconnectAttempt = now;
      if (reconnectMQTT()) lastMqttReconnectAttempt = 0;
    }
  } else {
    mqttClient.loop();
  }

  if (now - lastClockTick >= 1000) {
    lastClockTick = now;
    if (menuMode == 0) updateDisplay = true; 
  }

  if (now - lastSensorRead >= 2000) { 
    lastSensorRead = now;
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    
    if (!isnan(t) && !isnan(h)) {
      currentTemp = t; currentHum = h;
      currentLight = map(analogRead(LDR_PIN), 0, 4095, 0, 100);
      currentSoil = map(analogRead(SOIL_PIN), 0, 4095, 0, 100);
      updateDisplay = true; 
      controlSystem();      

      struct tm timeinfo;
      char timeStr[9] = "00:00:00";
      if (getLocalTime(&timeinfo, 10)) sprintf(timeStr, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      if (mqttClient.connected()) publishData(timeStr);
    }
  }
  drawLCD();
}

void publishData(const char* timeStr) {
  char payload[150];
  sprintf(payload, "{\"temp\":%.1f, \"hum\":%.1f, \"light\":%d, \"soil\":%d, \"time\":\"%s\"}", currentTemp, currentHum, currentLight, currentSoil, timeStr);
  mqttClient.publish("smartfarm/sensors", payload);
  mqttClient.publish("smartfarm/status/relay1", digitalRead(RELAY1_PIN) ? "ON" : "OFF");
  mqttClient.publish("smartfarm/status/relay2", digitalRead(RELAY2_PIN) ? "ON" : "OFF");
  
  char threshPayload[10];
  itoa(tempThreshold, threshPayload, 10);
  mqttClient.publish("smartfarm/status/threshold/temp", threshPayload);
  itoa(soilThreshold, threshPayload, 10);
  mqttClient.publish("smartfarm/status/threshold/soil", threshPayload);
}