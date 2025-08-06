#include <SPI.h>
#include <mcp2515.h>
#include <Wire.h>
#include "max6675.h"
#include "MPU6050.h"
#include <Adafruit_INA219.h>  // INA219 library

MCP2515 mcp2515(5);

#define CAN_NODE_1_ID 0x036

unsigned long lastSendTime = 0;
#define SEND_INTERVAL 500  // in ms

// MAX6675 pins
int thermoDO = 12;
int thermoCS = 15;
int thermoCLK = 14;
MAX6675 thermocouple(thermoCLK, thermoCS, thermoDO);

MPU6050 mpu;
Adafruit_INA219 ina219;

bool canConnected = false;
bool vibrationDetected = false;

void setup() {
  Serial.begin(115200);
  Wire.begin();

  // Initialize MPU6050
  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("MPU6050 connection failed");
  } else {
    Serial.println("MPU6050 connected");
  }

  // Initialize INA219
  if (!ina219.begin()) {
    Serial.println("Failed to find INA219 chip");
  } else {
    Serial.println("INA219 found");
  }

  // Initialize CAN bus
  SPI.begin();
  mcp2515.reset();
  mcp2515.setBitrate(CAN_500KBPS, MCP_8MHZ);
  mcp2515.setNormalMode();
  canConnected = true; // Assumed true as per your logic

  if (canConnected) {
    Serial.println("CAN module assumed connected - GREEN LED ON");
  } else {
    Serial.println("CAN module NOT connected - RED LED ON");
  }

  Serial.println("Node 1 started");
}

void loop() {
  float temp_C = thermocouple.readCelsius();
  float temp_F = thermocouple.readFahrenheit();

  // Read MPU6050 acceleration values
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);

  // Simple vibration detection threshold example (adjust threshold accordingly)
  const int16_t vibrationThreshold = 15000;
  vibrationDetected = (abs(ax) > vibrationThreshold || abs(ay) > vibrationThreshold || abs(az) > vibrationThreshold);

  // Read INA219 voltage and current
  float busVoltage = ina219.getBusVoltage_V();  // volts
  float current_mA = ina219.getCurrent_mA();    // milliamps

  Serial.print("°C = ");
  Serial.println(temp_C);
  Serial.print("°F = ");
  Serial.println(temp_F);
  Serial.print("Vibration Detected: ");
  Serial.println(vibrationDetected ? "YES" : "NO");
  Serial.print("Bus Voltage: ");
  Serial.print(busVoltage);
  Serial.println(" V");
  Serial.print("Current: ");
  Serial.print(current_mA);
  Serial.println(" mA");

  unsigned long currentMillis = millis();

  // Send all data in one CAN message periodically
  if (currentMillis - lastSendTime >= SEND_INTERVAL) {
    lastSendTime = currentMillis;

    struct can_frame canMsg;
    canMsg.can_id = CAN_NODE_1_ID;
    canMsg.can_dlc = 7;  // total bytes to send

    // Pack temperature (2 bytes) scaled by 100 for 0.01 precision
    int16_t tempSend = (int16_t)(temp_C * 100);
    canMsg.data[0] = (tempSend >> 8) & 0xFF;
    canMsg.data[1] = tempSend & 0xFF;

    // Vibration detected: 1 byte
    canMsg.data[2] = vibrationDetected ? 1 : 0;

    // Pack voltage (2 bytes) scaled by 1000 (millivolts)
    uint16_t voltagePacked = (uint16_t)(busVoltage * 1000);
    canMsg.data[3] = (voltagePacked >> 8) & 0xFF;
    canMsg.data[4] = voltagePacked & 0xFF;

    // Pack current (2 bytes) scaled by 10 (tenths of mA)
    uint16_t currentPacked = (uint16_t)(current_mA * 10);
    canMsg.data[5] = (currentPacked >> 8) & 0xFF;
    canMsg.data[6] = currentPacked & 0xFF;

    if (mcp2515.sendMessage(&canMsg) == MCP2515::ERROR_OK) {
      Serial.print("Sent CAN: Temp=");
      Serial.print(temp_C, 2);
      Serial.print(" C, Vibration=");
      Serial.print(vibrationDetected ? "YES" : "NO");
      Serial.print(", Voltage=");
      Serial.print(busVoltage, 3);
      Serial.print(" V, Current=");
      Serial.print(current_mA, 1);
      Serial.println(" mA");
    } else {
      Serial.println("Failed to send CAN message");
    }
  }

  delay(1000);
}
