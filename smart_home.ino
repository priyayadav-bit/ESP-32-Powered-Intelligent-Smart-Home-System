/*
  ESP32 Powered Intelligent Smart Home System

  What it does
    1. AUTOMATIC : an LDR measures the room light. The light turns ON when it
                   gets dark and OFF when there is enough light.
    2. MANUAL    : push buttons toggle each light (works without internet).
    3. REMOTE    : mobile app and Google Assistant control the lights through
                   Sinric Pro (needs Wi-Fi and internet).

  Rule between the three: the LAST command wins. The automatic mode only acts
  when the room light changes from dark to bright or bright to dark, so a manual
  or app command stays until the next change in room light.

  Libraries (Arduino IDE -> Library Manager)
    - SinricPro  (also install its dependencies: ArduinoJson, WebSockets)
  Board: "ESP32 Dev Module" (ESP32 board package by Espressif)

  Setup
    - Copy secrets.h.example to secrets.h (same folder) and fill in your values.
    - Check the PIN and SETTINGS sections below and change them to match your wiring.

  SAFETY: the lights run on mains voltage. Test with an LED first and keep all
  mains wiring inside an insulated enclosure.
*/

#include <Arduino.h>
#include <WiFi.h>
#include "SinricPro.h"
#include "SinricProSwitch.h"
#include "secrets.h"          // WIFI_SSID, WIFI_PASS, APP_KEY, APP_SECRET, DEVICE_ID_1..3

// ======================== PINS (change to match your wiring) ========================
#define NUM_LIGHTS 3                              // 1, 2 or 3 (keep the arrays below the same size)
const int RELAY_PINS[NUM_LIGHTS]  = {26, 27, 25}; // relay module IN pins
const int SWITCH_PINS[NUM_LIGHTS] = {32, 33, 14}; // push buttons (other side of the button to GND)
const char* DEVICE_IDS[NUM_LIGHTS] = {DEVICE_ID_1, DEVICE_ID_2, DEVICE_ID_3};
const int LDR_PIN = 34;                           // use an ADC1 pin (32-39). ADC2 pins fail when Wi-Fi is ON

// ======================== SETTINGS ========================
const bool RELAY_ACTIVE_LOW = true;   // most relay modules switch ON with a LOW signal
const bool DARK_IS_HIGH     = true;   // true : LDR reading goes UP when it gets dark
                                      // false: LDR reading goes DOWN when it gets dark
const int  DARK_ON_LEVEL    = 2800;   // light turns ON  when the darkness level is above this (0-4095)
const int  BRIGHT_OFF_LEVEL = 2200;   // light turns OFF when the darkness level is below this
                                      // keep a gap between the two values so the light does not flicker
const int  AUTO_LIGHT       = 0;      // which light the LDR controls (0 = first light, -1 = disable automatic mode)
const bool PRINT_LDR        = true;   // print the LDR value every 2 seconds (useful to choose the levels)

// ======================== VARIABLES ========================
bool lightOn[NUM_LIGHTS];
bool sinricStarted = false;
bool wifiAnnounced = false;

enum LightLevel { LEVEL_UNKNOWN, LEVEL_DARK, LEVEL_BRIGHT };
LightLevel lastLevel = LEVEL_UNKNOWN;

bool lastSwitchReading[NUM_LIGHTS];
bool stableSwitchState[NUM_LIGHTS];
unsigned long lastSwitchChange[NUM_LIGHTS];
const unsigned long DEBOUNCE_MS = 50;

unsigned long lastLdrCheck = 0;
unsigned long lastLdrPrint = 0;

// ======================== RELAY CONTROL ========================
void writeRelay(int i, bool on) {
  bool level = RELAY_ACTIVE_LOW ? !on : on;
  digitalWrite(RELAY_PINS[i], level ? HIGH : LOW);
}

// Turn light i ON/OFF. If tellCloud is true the new state is also sent to Sinric Pro,
// so the app and Google Home always show the real state.
void setLight(int i, bool on, bool tellCloud) {
  lightOn[i] = on;
  writeRelay(i, on);
  Serial.printf("Light %d -> %s\n", i + 1, on ? "ON" : "OFF");
  if (tellCloud && sinricStarted) {
    SinricProSwitch &sw = SinricPro[DEVICE_IDS[i]];
    sw.sendPowerStateEvent(on);
  }
}

// ======================== SINRIC PRO ========================
// Called when the app or Google Assistant switches a light.
bool onPowerState(const String &deviceId, bool &state) {
  for (int i = 0; i < NUM_LIGHTS; i++) {
    if (deviceId == DEVICE_IDS[i]) {
      Serial.printf("[Sinric Pro] command for light %d\n", i + 1);
      setLight(i, state, false);   // the cloud already knows this state
      return true;
    }
  }
  return false;
}

void startSinric() {
  for (int i = 0; i < NUM_LIGHTS; i++) {
    SinricProSwitch &sw = SinricPro[DEVICE_IDS[i]];
    sw.onPowerState(onPowerState);
  }
  SinricPro.onConnected([]() { Serial.println("[Sinric Pro] connected"); });
  SinricPro.onDisconnected([]() { Serial.println("[Sinric Pro] disconnected"); });
  SinricPro.begin(APP_KEY, APP_SECRET);
  sinricStarted = true;
}

// ======================== AUTOMATIC MODE (LDR) ========================
int readDarknessLevel() {
  long sum = 0;
  for (int k = 0; k < 16; k++) {      // average 16 readings to ignore sudden changes
    sum += analogRead(LDR_PIN);
    delay(2);
  }
  int raw = sum / 16;                  // 0 - 4095
  return DARK_IS_HIGH ? raw : (4095 - raw);
}

void checkLdr() {
  if (AUTO_LIGHT < 0 || AUTO_LIGHT >= NUM_LIGHTS) return;

  int level = readDarknessLevel();
  if (PRINT_LDR && millis() - lastLdrPrint > 2000) {
    lastLdrPrint = millis();
    Serial.printf("Darkness level: %d  (dark above %d, bright below %d)\n", level, DARK_ON_LEVEL, BRIGHT_OFF_LEVEL);
  }

  if (level >= DARK_ON_LEVEL && lastLevel != LEVEL_DARK) {
    lastLevel = LEVEL_DARK;
    Serial.println("Room is DARK");
    setLight(AUTO_LIGHT, true, true);
  } else if (level <= BRIGHT_OFF_LEVEL && lastLevel != LEVEL_BRIGHT) {
    lastLevel = LEVEL_BRIGHT;
    Serial.println("Room is BRIGHT");
    setLight(AUTO_LIGHT, false, true);
  }
}

// ======================== MANUAL SWITCHES ========================
void checkSwitches() {
  for (int i = 0; i < NUM_LIGHTS; i++) {
    bool reading = digitalRead(SWITCH_PINS[i]);        // HIGH = released, LOW = pressed
    if (reading != lastSwitchReading[i]) {
      lastSwitchReading[i] = reading;
      lastSwitchChange[i] = millis();
    }
    if (millis() - lastSwitchChange[i] > DEBOUNCE_MS && reading != stableSwitchState[i]) {
      stableSwitchState[i] = reading;
      if (reading == LOW) {                             // button pressed -> toggle the light
        setLight(i, !lightOn[i], true);
      }
    }
  }
}

// ======================== SETUP AND LOOP ========================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32 Smart Home System ===");

  for (int i = 0; i < NUM_LIGHTS; i++) {
    writeRelay(i, false);                  // set the OFF level first so the relay does not click at start
    pinMode(RELAY_PINS[i], OUTPUT);
    writeRelay(i, false);
    lightOn[i] = false;

    pinMode(SWITCH_PINS[i], INPUT_PULLUP);
    lastSwitchReading[i] = HIGH;
    stableSwitchState[i] = HIGH;
    lastSwitchChange[i] = 0;
  }

  analogReadResolution(12);
  analogSetPinAttenuation(LDR_PIN, ADC_11db);

  // Wi-Fi is started in the background. Automatic and manual modes work even without it.
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.println("Connecting to Wi-Fi in the background...");
}

void loop() {
  checkSwitches();

  if (millis() - lastLdrCheck > 500) {
    lastLdrCheck = millis();
    checkLdr();
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiAnnounced) {
      wifiAnnounced = true;
      Serial.print("Wi-Fi connected. IP address: ");
      Serial.println(WiFi.localIP());
    }
    if (!sinricStarted) startSinric();
    SinricPro.handle();
  } else {
    wifiAnnounced = false;
  }
}
