#include <WiFi.h>
#include <SPI.h>
#include <mcp2515.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WebServer.h>

const char* ssid = "Prateeksin";
const char* password = "1234512345";

// MCP2515 setup - SPI CS on GPIO 5 (change if needed)
MCP2515 mcp2515(5);

#define CAN_NODE_1_ID 0x036
#define YELLOW_LED_PIN 27
#define BUZZER_PIN 14
#define ALARM_LED_PIN 12 // Alarm LED pin

// LCD (Make sure to use an ESP32-compatible LiquidCrystal_I2C library)
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Web server on port 80
WebServer server(80);

// Sensor variables
float temp_C = 0.0;
bool vibrationDetected = false;
float voltage_V = 0.0;
float current_mA = 0.0;

// CAN connection monitoring
unsigned long lastCANReceived = 0;
const unsigned long CAN_TIMEOUT_MS = 5000;  // If no CAN msg in 5 seconds => disconnected

// Temperature alarm variables
float tempThreshold = 30.0; // Default temperature threshold
bool alarmActive = false;

// Serve main webpage
void handleRoot() {
  String htmlPage = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>CAN Sensor Data Monitor</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
<style>
  body { font-family: Arial, sans-serif; margin: 20px; background: #121212; color: #e0e0e0; }
  h2 { color: #00bcd4; text-align:center; }
  #status { font-size: 1.4em; margin: 15px 0; text-align:center; }
  .status-connected { color: #4CAF50; }
  .status-disconnected { color: #F44366; }
  .status-alarm { color: #FF0000; }
  .container { display: flex; flex-wrap: wrap; justify-content: center; gap: 30px; }
  .chart-container { background: #1e1e1e; border-radius: 8px; padding: 15px; box-shadow: 0 0 10px #00bcd4aa; }
  canvas { display: block; max-width: 600px; width: 100%; height: 200px; }
  #summary { background: #222; border-radius: 8px; padding: 15px; margin-top: 30px; max-width: 700px; margin-left: auto; margin-right: auto; }
  #summary p { font-size: 1.1em; margin: 8px 0; }
  .alarm-config { background: #222; border-radius: 8px; padding: 15px; margin-top: 20px; max-width: 700px; margin-left: auto; margin-right: auto; }
  .alarm-config label, .alarm-config input, .alarm-config button { margin-right: 10px; }
</style>
</head>
<body>
  <h2>CAN Sensor Data Monitor</h2>
  <div id="status" class="status-disconnected">CAN Status: DISCONNECTED</div>

  <div class="container">
    <div class="chart-container">
      <canvas id="tempChart"></canvas>
    </div>
    <div class="chart-container">
      <canvas id="vibrationChart" style="height:120px;"></canvas>
    </div>
    <div class="chart-container">
      <canvas id="voltageChart"></canvas>
    </div>
    <div class="chart-container">
      <canvas id="currentChart"></canvas>
    </div>
  </div>

  <div id="summary">
    <p><strong>Latest Readings:</strong></p>
    <p>Temperature: <span id="tempVal">-- °C</span></p>
    <p>Vibration: <span id="vibVal">--</span></p>
    <p>Voltage: <span id="voltVal">-- V</span></p>
    <p>Current: <span id="currVal">-- mA</span></p>
  </div>

  <div class="alarm-config">
    <p><strong>Temperature Alarm Configuration:</strong></p>
    <label for="temp-threshold">Set Threshold (°C):</label>
    <input type="number" id="temp-threshold" step="0.1" value="30.0" />
    <button onclick="setThreshold()">Set</button>
    <p>Current Threshold: <span id="currentThreshold">30.0 °C</span></p>
  </div>

<script>
const maxDataPoints = 30;
let labels = Array(maxDataPoints).fill('');
let tempData = [];
let vibrationData = [];
let voltageData = [];
let currentData = [];

const tempCtx = document.getElementById('tempChart').getContext('2d');
const vibrationCtx = document.getElementById('vibrationChart').getContext('2d');
const voltageCtx = document.getElementById('voltageChart').getContext('2d');
const currentCtx = document.getElementById('currentChart').getContext('2d');
const statusDiv = document.getElementById('status');

const tempChart = new Chart(tempCtx, {
  type: 'line',
  data: { labels: labels, datasets: [{ label: 'Temperature (°C)', borderColor: '#FF6384', fill: false, data: tempData }] },
  options: { animation: false, responsive: true, scales: { y: { beginAtZero: true, suggestedMax: 60 } } }
});

const vibrationChart = new Chart(vibrationCtx, {
  type: 'bar',
  data: { labels: labels, datasets: [{ label: 'Vibration', backgroundColor: '#FFCE56', data: vibrationData }] },
  options: {
    animation: false, responsive: true,
    scales: {
      y: { min: 0, max: 1, ticks: { stepSize: 1, callback: v => v ? 'YES' : 'NO'} }
    }
  }
});

const voltageChart = new Chart(voltageCtx, {
  type: 'line',
  data: { labels: labels, datasets: [{ label: 'Voltage (V)', borderColor: '#36A2EB', fill: false, data: voltageData }] },
  options: { animation: false, responsive: true, scales: { y: { beginAtZero: true, suggestedMax: 15 } } }
});

const currentChart = new Chart(currentCtx, {
  type: 'line',
  data: { labels: labels, datasets: [{ label: 'Current (mA)', borderColor: '#4BC0C0', fill: false, data: currentData }] },
  options: { animation: false, responsive: true, scales: { y: { beginAtZero: true } } }
});

function addData(label, temp, vibration, voltage, current) {
  if (tempData.length >= maxDataPoints) {
    tempData.shift();
    vibrationData.shift();
    voltageData.shift();
    currentData.shift();
    labels.shift();
  }
  tempData.push(temp);
  vibrationData.push(vibration ? 1 : 0);
  voltageData.push(voltage);
  currentData.push(current);
  labels.push(label);

  tempChart.update();
  vibrationChart.update();
  voltageChart.update();
  currentChart.update();

  // Update latest readings
  document.getElementById('tempVal').textContent = temp.toFixed(2) + " °C";
  document.getElementById('vibVal').textContent = vibration ? "YES" : "NO";
  document.getElementById('voltVal').textContent = voltage.toFixed(3) + " V";
  document.getElementById('currVal').textContent = current.toFixed(1) + " mA";
}

async function fetchData() {
  try {
    const response = await fetch('/data');
    if(!response.ok) throw new Error("Network response was not OK");
    const data = await response.json();

    const now = new Date();
    const label = now.getHours().toString().padStart(2,'0') + ':' +
                  now.getMinutes().toString().padStart(2,'0') + ':' +
                  now.getSeconds().toString().padStart(2,'0');
    addData(label, data.temp_C, data.vibrationDetected, data.voltage_V, data.current_mA);

    // Update CAN status display based on JSON flag
    if (data.canConnected) {
      if (data.alarmActive) {
        statusDiv.textContent = "CAN Status: CONNECTED (ALARM!)";
        statusDiv.classList.remove('status-disconnected', 'status-connected');
        statusDiv.classList.add('status-alarm');
      } else {
        statusDiv.textContent = "CAN Status: CONNECTED";
        statusDiv.classList.remove('status-disconnected', 'status-alarm');
        statusDiv.classList.add('status-connected');
      }
    } else {
      statusDiv.textContent = "CAN Status: DISCONNECTED";
      statusDiv.classList.remove('status-connected', 'status-alarm');
      statusDiv.classList.add('status-disconnected');
    }
  } catch (error) {
    console.error('Error fetching data:', error);
    statusDiv.textContent = "CAN Status: DISCONNECTED";
    statusDiv.classList.remove('status-connected', 'status-alarm');
    statusDiv.classList.add('status-disconnected');
  }
}

async function getThreshold() {
  try {
    const response = await fetch('/getThreshold');
    if(!response.ok) throw new Error("Network response was not OK");
    const data = await response.json();
    document.getElementById('temp-threshold').value = data.threshold;
    document.getElementById('currentThreshold').textContent = data.threshold + " °C";
  } catch (error) {
    console.error('Error fetching threshold:', error);
  }
}

async function setThreshold() {
  const newThreshold = document.getElementById('temp-threshold').value;
  try {
    const response = await fetch('/setThreshold?value=' + newThreshold, { method: 'POST' });
    if(!response.ok) throw new Error("Network response was not OK");
    alert("Threshold set successfully!");
    getThreshold(); // Refresh the displayed threshold
  } catch (error) {
    console.error('Error setting threshold:', error);
    alert("Failed to set threshold.");
  }
}

setInterval(fetchData, 2000);
getThreshold(); // Initial fetch for the threshold
</script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", htmlPage);
}


// Serve sensor data and CAN connection state as JSON
void handleData() {
  bool connected = (millis() - lastCANReceived < CAN_TIMEOUT_MS);

  String json = "{";
  json += "\"temp_C\":";
  json += String(temp_C, 2);
  json += ",";
  json += "\"vibrationDetected\":";
  json += vibrationDetected ? "true" : "false";
  json += ",";
  json += "\"voltage_V\":";
  json += String(voltage_V, 3);
  json += ",";
  json += "\"current_mA\":";
  json += String(current_mA, 1);
  json += ",";
  json += "\"canConnected\":";
  json += connected ? "true" : "false";
  json += ",";
  json += "\"alarmActive\":";
  json += alarmActive ? "true" : "false";
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
  json += "\"threshold\":";
  json += String(tempThreshold, 2);
  json += "}";
  server.send(200, "application/json", json);
}

void checkAlarm() {
  if (temp_C > tempThreshold) {
    if (!alarmActive) {
      Serial.println("Temperature alarm triggered!");
      alarmActive = true;
    }
    digitalWrite(ALARM_LED_PIN, HIGH);
    // Beep the buzzer
    digitalWrite(BUZZER_PIN, HIGH);
  } else {
    if (alarmActive) {
      Serial.println("Temperature alarm cleared.");
      alarmActive = false;
    }
    digitalWrite(ALARM_LED_PIN, LOW);
    // Turn off the buzzer
    digitalWrite(BUZZER_PIN, LOW);
  }
}

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

  // Connect to WiFi
  WiFi.begin(ssid, password);
  lcd.setCursor(0, 0);
  lcd.print("Connecting to WiFi");
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

  // Setup web server
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
      lastCANReceived = millis();  // Update last received timestamp

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

      checkAlarm(); // Check if temperature exceeds threshold
    }
  } else {
    // No CAN message received right now
    if (millis() - lastCANReceived > CAN_TIMEOUT_MS) {
      digitalWrite(YELLOW_LED_PIN, LOW);  // turn off LED if disconnected
    }
    // If no CAN message is received, we should also clear the alarm state.
    // This assumes the remote node is responsible for sending sensor data.
    if(alarmActive) {
      alarmActive = false;
      digitalWrite(ALARM_LED_PIN, LOW);
      digitalWrite(BUZZER_PIN, 1);
    }
  }
}