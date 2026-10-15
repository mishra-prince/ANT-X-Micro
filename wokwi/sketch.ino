// ANT-X Micro — PC/ESP32-C3 link proof, NOT final walking firmware.
// Demonstrates Wi-Fi UDP, two DRV8833-style motor command pairs,
// left/right contact reflex, telemetry, and link-loss safe stop.
// Requires ArduinoJson 7 (listed in libraries.txt).
// In Wokwi, four LEDs stand in for the H-bridge input signals.

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <cstring>

// === CONFIGURATION: edit before loading on a real ESP32-C3 ===
constexpr bool WOKWI_MODE = true;
const char* REAL_WIFI_SSID = "CHANGE_ME";
const char* REAL_WIFI_PASSWORD = "CHANGE_ME";
const char* REAL_PC_HOST = "192.168.1.100"; // PC LAN IP on same Wi-Fi
const char* WOKWI_PC_HOST = "host.wokwi.internal"; // Private Gateway ONLY
constexpr uint16_t PC_PORT = 4210;
constexpr uint16_t ROBOT_PORT = 4211;

// Prototype board pins. Validate pinout and boot strapping on custom PCB.
constexpr uint8_t L_IN1 = 4;
constexpr uint8_t L_IN2 = 5;
constexpr uint8_t R_IN1 = 6;
constexpr uint8_t R_IN2 = 7;
constexpr uint8_t CONTACT_L = 0; // active LOW pushbutton to GND
constexpr uint8_t CONTACT_R = 1;

constexpr uint32_t TELEMETRY_INTERVAL_MS = 200;  // 5 Hz
constexpr uint32_t MAX_COMMAND_TTL_MS = 700;
constexpr uint8_t PROTOTYPE_PWM = 115;            // LOW DUTY, for unloaded bench testing

WiFiUDP udp;
IPAddress pcIp;
bool udpReady = false;
bool hostResolved = false;
uint32_t lastWiFiAttempt = 0;
uint32_t lastTelemetryMs = 0;
uint32_t lastCommandMs = 0;
uint32_t activeTtlMs = 600;
uint32_t txSeq = 0;
uint32_t rxSeq = 0;

String requestedMode = "STOP";

bool leftContact()  { return digitalRead(CONTACT_L) == LOW; }
bool rightContact() { return digitalRead(CONTACT_R) == LOW; }

void outputMotor(uint8_t in1, uint8_t in2, int speed) {
  speed = constrain(speed, -255, 255);
  if (speed > 0) {
    analogWrite(in1, speed);
    analogWrite(in2, 0);
  } else if (speed < 0) {
    analogWrite(in1, 0);
    analogWrite(in2, -speed);
  } else {
    analogWrite(in1, 0);
    analogWrite(in2, 0);
  }
}

void stopMotors() {
  outputMotor(L_IN1, L_IN2, 0);
  outputMotor(R_IN1, R_IN2, 0);
}

bool linkAlive() {
  return WiFi.status() == WL_CONNECTED && udpReady &&
         lastCommandMs != 0 && (uint32_t)(millis() - lastCommandMs) <= activeTtlMs;
}

const char* robotState() {
  if (leftContact() || rightContact()) return "CONTACT_STOP";
  if (!linkAlive()) return "WAIT_PC";
  if (requestedMode == "EXPLORE") return "EXPLORE";
  return "STOP"; // Other high-level commands not yet implemented on hardware
}

void processPackets() {
  int size;
  while ((size = udp.parsePacket()) > 0) {
    char packet[400];
    int n = udp.read(packet, sizeof(packet) - 1);
    if (n <= 0) continue;
    packet[n] = '\0';
    // Only accept commands from the configured computer.
    if (!hostResolved || udp.remoteIP() != pcIp) continue;

    JsonDocument doc;
    if (deserializeJson(doc, packet)) continue;
    if ((doc["v"] | 0) != 1) continue;
    if (strcmp(doc["type"] | "", "cmd") != 0) continue;

    const char* mode = doc["mode"] | "";
    bool valid = strcmp(mode, "STOP") == 0 ||
                 strcmp(mode, "EXPLORE") == 0 ||
                 strcmp(mode, "RETURN_HOME") == 0 ||
                 strcmp(mode, "INVESTIGATE_MARKER") == 0;
    int ttl = doc["ttl_ms"] | 0;
    if (!valid || ttl < 200 || ttl > (int)MAX_COMMAND_TTL_MS) continue;
    requestedMode = mode;
    rxSeq = doc["seq"] | 0u; // diagnostic only; command authorization is IP-based in this demo
    activeTtlMs = (uint32_t)ttl;
    lastCommandMs = millis();
    if (lastCommandMs == 0) lastCommandMs = 1;
  }
}

void sendTelemetry() {
  if (!udpReady || !hostResolved) return;
  JsonDocument doc;
  doc["v"] = 1;
  doc["type"] = "telemetry";
  doc["seq"] = ++txSeq;
  doc["uptime_ms"] = millis();
  doc["state"] = robotState();
  doc["mode_requested"] = requestedMode;
  doc["contact_l"] = leftContact();
  doc["contact_r"] = rightContact();
  doc["battery_mv"] = nullptr; // voltage sensing not installed in prototype
  doc["link_ok"] = linkAlive();
  doc["last_cmd_seq"] = rxSeq;

  udp.beginPacket(pcIp, PC_PORT);
  serializeJson(doc, udp);
  udp.endPacket();
}

void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!udpReady) {
      udpReady = udp.begin(ROBOT_PORT) == 1;
      hostResolved = WiFi.hostByName(WOKWI_MODE ? WOKWI_PC_HOST : REAL_PC_HOST, pcIp) == 1;
      Serial.print("Connected: "); Serial.println(WiFi.localIP());
      Serial.print("PC address resolved: "); Serial.println(hostResolved ? pcIp.toString() : "NO");
    }
    return;
  }
  stopMotors();
  if (udpReady) udp.stop();
  udpReady = false;
  hostResolved = false;
  requestedMode = "STOP";
  lastCommandMs = 0;
  if (millis() - lastWiFiAttempt >= 4000 || lastWiFiAttempt == 0) {
    lastWiFiAttempt = millis();
    WiFi.disconnect();
    WiFi.begin(WOKWI_MODE ? "Wokwi-GUEST" : REAL_WIFI_SSID,
               WOKWI_MODE ? "" : REAL_WIFI_PASSWORD);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(L_IN1, OUTPUT); pinMode(L_IN2, OUTPUT);
  pinMode(R_IN1, OUTPUT); pinMode(R_IN2, OUTPUT);
  pinMode(CONTACT_L, INPUT_PULLUP);
  pinMode(CONTACT_R, INPUT_PULLUP);
  stopMotors();
  WiFi.mode(WIFI_STA);
  ensureWiFi();
  Serial.println("ANT-X link proof started: motors stay stopped without valid PC command.");
}

void loop() {
  ensureWiFi();
  if (udpReady) processPackets();

  // Reflexes and watchdog always override computer commands.
  if (!linkAlive() || leftContact() || rightContact()) {
    stopMotors();
  } else if (requestedMode == "EXPLORE") {
    outputMotor(L_IN1, L_IN2, PROTOTYPE_PWM);
    outputMotor(R_IN1, R_IN2, PROTOTYPE_PWM);
  } else {
    // RETURN_HOME and INVESTIGATE_MARKER stay stopped until proper navigation exists.
    stopMotors();
  }

  if ((uint32_t)(millis() - lastTelemetryMs) >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryMs = millis();
    sendTelemetry();
    Serial.printf("State=%s, WiFi=%s, CmdSeq=%lu\n", robotState(),
                  WiFi.status() == WL_CONNECTED ? "ON" : "OFF",
                  (unsigned long)rxSeq);
  }
  delay(5);
}
