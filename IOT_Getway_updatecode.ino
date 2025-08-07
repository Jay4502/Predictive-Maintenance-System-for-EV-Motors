#include <WiFi.h>
#include <SPI.h>
#include <mcp2515.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WebServer.h>
#include <HTTPClient.h>

// ThingSpeak settings
const char* thingspeakServer = "http://api.thingspeak.com/update";
const char* apiKey = "WRXT2NNTVLFSPRKO";  
const char* ssid = "Soham";
const char* password = "12341234";

MCP2515 mcp2515(5);  // CS pin

#define CAN_NODE_1_ID 0x036
#define YELLOW_LED_PIN 27
#define BUZZER_PIN 14
#define ALARM_LED_PIN 12

LiquidCrystal_I2C lcd(0x27, 20, 4);
WebServer server(80);

float temp_C = 0.0;
bool vibrationDetected = false;
float voltage_V = 0.0;
float current_mA = 0.0;

unsigned long lastCANReceived = 0;
const unsigned long CAN_TIMEOUT_MS = 5000;

float tempThreshold = 30.0;
bool alarmActive = false;
unsigned long lastThingSpeakUpdate = 0;

void handleRoot();
void handleData();
void handleSetThreshold();
void handleGetThreshold();
void checkAlarm();
void sendToThingSpeak();

void setup() {
  Serial.begin(115200);
  Wire.begin();

  pinMode(YELLOW_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(ALARM_LED_PIN, OUTPUT);

  digitalWrite(YELLOW_LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(ALARM_LED_PIN, LOW);

  SPI.begin();
  mcp2515.reset();
  mcp2515.setBitrate(CAN_500KBPS, MCP_8MHZ);
  mcp2515.setNormalMode();

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CAN Receiver Ready");
  delay(2000);
  lcd.clear();

  WiFi.begin(ssid, password);
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    lcd.print(".");
  }
  lcd.clear();
  lcd.print("WiFi Connected");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP().toString());

  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/setThreshold", HTTP_POST, handleSetThreshold);
  server.on("/getThreshold", handleGetThreshold);
  server.begin();
}

void loop() {
  server.handleClient();

  struct can_frame canMsg;
  if (mcp2515.readMessage(&canMsg) == MCP2515::ERROR_OK) {
    if (canMsg.can_id == CAN_NODE_1_ID && canMsg.can_dlc == 7) {
      lastCANReceived = millis();

      int16_t tempRecv = ((int16_t)canMsg.data[0] << 8) | canMsg.data[1];
      temp_C = tempRecv / 100.0;

      vibrationDetected = (canMsg.data[2] == 1);

      uint16_t voltagePacked = ((uint16_t)canMsg.data[3] << 8) | canMsg.data[4];
      voltage_V = voltagePacked / 1000.0;

      uint16_t currentPacked = ((uint16_t)canMsg.data[5] << 8) | canMsg.data[6];
      current_mA = currentPacked / 10.0;

      Serial.printf("Temp: %.2f C, Vibration: %s, Voltage: %.3f V, Current: %.1f mA\n",
                    temp_C, vibrationDetected ? "YES" : "NO", voltage_V, current_mA);

      digitalWrite(YELLOW_LED_PIN, vibrationDetected ? HIGH : LOW);

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.printf("Temp: %.2f%cC", temp_C, 223);

      lcd.setCursor(0, 1);
      lcd.printf("Vibration: %s", vibrationDetected ? "YES" : "NO ");

      lcd.setCursor(0, 2);
      lcd.printf("Voltage: %.3f V", voltage_V);

      lcd.setCursor(0, 3);
      lcd.printf("Current: %.1f mA", current_mA);

      checkAlarm();
      sendToThingSpeak();  // Send values
    }
  } else {
    if (millis() - lastCANReceived > CAN_TIMEOUT_MS) {
      digitalWrite(YELLOW_LED_PIN, LOW);
    }
    if (alarmActive) {
      alarmActive = false;
      digitalWrite(ALARM_LED_PIN, LOW);
      digitalWrite(BUZZER_PIN, LOW);
    }
  }
}

void sendToThingSpeak() {
  if (millis() - lastThingSpeakUpdate < 20000) return;  // Avoid flooding (ThingSpeak limit: 15 sec min)

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(thingspeakServer) + "?api_key=" + apiKey +
                 "&field1=" + String(temp_C) +
                 "&field2=" + String(vibrationDetected ? 1 : 0) +
                 "&field3=" + String(voltage_V) +
                 "&field4=" + String(current_mA) +
                 "&field5=" + String(alarmActive ? 1 : 0);

    http.begin(url);
    int httpResponseCode = http.GET();
    if (httpResponseCode > 0) {
      Serial.printf("ThingSpeak updated. Code: %d\n", httpResponseCode);
    } else {
      Serial.printf("Failed to send to ThingSpeak. Error: %s\n", http.errorToString(httpResponseCode).c_str());
    }
    http.end();
    lastThingSpeakUpdate = millis();
  }
}

void checkAlarm() {
  if (temp_C > tempThreshold) {
    if (!alarmActive) {
      Serial.println("Temperature alarm triggered!");
      alarmActive = true;
    }
    digitalWrite(ALARM_LED_PIN, HIGH);
    digitalWrite(BUZZER_PIN, HIGH);
  } else {
    if (alarmActive) {
      Serial.println("Temperature alarm cleared.");
      alarmActive = false;
    }
    digitalWrite(ALARM_LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
  }
}

// Web UI handlers
void handleRoot() {
  server.send(200, "text/html", "<h1>CAN Dashboard is Running</h1>");
}

void handleData() {
  bool connected = (millis() - lastCANReceived < CAN_TIMEOUT_MS);

  String json = "{";
  json += "\"temp_C\":" + String(temp_C, 2) + ",";
  json += "\"vibrationDetected\":" + String(vibrationDetected ? "true" : "false") + ",";
  json += "\"voltage_V\":" + String(voltage_V, 3) + ",";
  json += "\"current_mA\":" + String(current_mA, 1) + ",";
  json += "\"canConnected\":" + String(connected ? "true" : "false") + ",";
  json += "\"alarmActive\":" + String(alarmActive ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void handleSetThreshold() {
  if (server.hasArg("value")) {
    tempThreshold = server.arg("value").toFloat();
    Serial.printf("New temperature threshold set to: %.2f C\n", tempThreshold);
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleGetThreshold() {
  String json = "{";
  json += "\"threshold\":" + String(tempThreshold, 2);
  json += "}";
  server.send(200, "application/json", json);
}
