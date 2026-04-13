#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <time.h>
#include <math.h>
#include <stdlib.h>

#include <U8g2lib.h>
#include <Adafruit_BME680.h>
#include <Adafruit_NeoPixel.h>
#include <BH1750.h>
#include <AccelStepper.h>

#include "app_config.h"

enum class ControlMode : uint8_t {
  AutoSunlight = 0,
  Manual = 1,
};

enum class MoodMode : uint8_t {
  Off = 0,
  WarmWhite,
  SoftBlue,
  RomanticPurple,
  VitalGreen,
  SoftOrange,
};

struct BlindState {
  int liftPercent = 0;
  int tiltPercent = 0;
};

struct ScheduleState {
  bool wakeEnabled = true;
  bool sleepEnabled = true;
  int wakeHour = 7;
  int wakeMinute = 0;
  int sleepHour = 23;
  int sleepMinute = 30;
};

struct SensorSnapshot {
  float temperatureC = NAN;
  float humidityPct = NAN;
  float pressureHpa = NAN;
  float gasResistanceKOhm = NAN;
  float airScore = NAN;
  float lightLux = NAN;
  bool raining = false;
  float batteryVoltage = 0.0f;
  int batteryPct = 0;
  bool airPoor = false;
  bool ventilationRecommended = false;
};

struct RgbColor {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t brightness = APP_LED_DEFAULT_BRIGHTNESS;
};

WebServer server(APP_HTTP_PORT);
Preferences preferences;

#if APP_USE_SH1106_DISPLAY
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
#else
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
#endif

Adafruit_BME680 bme680;
BH1750 lightMeter;
Adafruit_NeoPixel strip(APP_LED_COUNT, APP_LED_STRIP_PIN, NEO_GRB + NEO_KHZ800);
AccelStepper liftStepper(AccelStepper::DRIVER, APP_LIFT_STEP_PIN, APP_LIFT_DIR_PIN);
AccelStepper tiltStepper(
    AccelStepper::HALF4WIRE,
    APP_TILT_IN1_PIN,
    APP_TILT_IN3_PIN,
    APP_TILT_IN2_PIN,
    APP_TILT_IN4_PIN);

bool displayReady = false;
bool wifiConnected = false;
bool accessPointMode = false;
bool bme680Ready = false;
bool lightMeterReady = false;
bool liftHomed = false;
bool wakeAssistLatched = false;
bool tiltOutputsEnabled = false;

long liftTravelSteps = APP_LIFT_FALLBACK_TRAVEL_STEPS;
float gasBaselineOhms = 0.0f;

ControlMode controlMode = ControlMode::AutoSunlight;
MoodMode moodMode = MoodMode::WarmWhite;
BlindState blindState;
BlindState blindTarget;
ScheduleState scheduleState;
SensorSnapshot sensors;
String lastAlertMessage = "System ready";

unsigned long lastSensorReadMs = 0;
unsigned long lastDisplayRefreshMs = 0;
unsigned long lastStatusPrintMs = 0;
unsigned long lastLightRefreshMs = 0;

int clampPercent(int value) {
  return constrain(value, 0, 100);
}

bool liftLimitTriggered(uint8_t pin) {
  return digitalRead(pin) == APP_LIFT_LIMIT_ACTIVE_LEVEL;
}

bool rainDetected() {
  return digitalRead(APP_RAIN_DIGITAL_PIN) == APP_RAIN_ACTIVE_LEVEL;
}

void setLiftDriverEnabled(bool enabled) {
  const uint8_t inactiveLevel = APP_LIFT_ENABLE_ACTIVE_LEVEL == LOW ? HIGH : LOW;
  digitalWrite(APP_LIFT_ENABLE_PIN, enabled ? APP_LIFT_ENABLE_ACTIVE_LEVEL : inactiveLevel);
}

long liftPercentToSteps(int percent) {
  return lroundf((clampPercent(percent) / 100.0f) * liftTravelSteps);
}

int liftStepsToPercent(long steps) {
  if (liftTravelSteps <= 0) {
    return 0;
  }
  return clampPercent(lroundf((steps * 100.0f) / liftTravelSteps));
}

long tiltPercentToSteps(int percent) {
  return lroundf((clampPercent(percent) / 100.0f) * APP_TILT_MAX_STEPS);
}

int tiltStepsToPercent(long steps) {
  if (APP_TILT_MAX_STEPS <= 0) {
    return 0;
  }
  return clampPercent(lroundf((steps * 100.0f) / APP_TILT_MAX_STEPS));
}

float readBatteryVoltage() {
  uint32_t millivolts = analogReadMilliVolts(APP_BATTERY_ANALOG_PIN);
  return (millivolts / 1000.0f) * APP_BATTERY_DIVIDER_RATIO;
}

int batteryPercentFromVoltage(float voltage) {
  float pct = ((voltage - APP_BATTERY_MIN_VOLTAGE) /
               (APP_BATTERY_MAX_VOLTAGE - APP_BATTERY_MIN_VOLTAGE)) * 100.0f;
  return clampPercent(lroundf(pct));
}

const char *modeToString(ControlMode mode) {
  return mode == ControlMode::AutoSunlight ? "auto" : "manual";
}

const char *moodToString(MoodMode mode) {
  switch (mode) {
    case MoodMode::Off:
      return "off";
    case MoodMode::WarmWhite:
      return "warm_white";
    case MoodMode::SoftBlue:
      return "soft_blue";
    case MoodMode::RomanticPurple:
      return "romantic_purple";
    case MoodMode::VitalGreen:
      return "vital_green";
    case MoodMode::SoftOrange:
      return "soft_orange";
  }
  return "warm_white";
}

MoodMode moodFromString(const String &value) {
  if (value == "off") {
    return MoodMode::Off;
  }
  if (value == "soft_blue") {
    return MoodMode::SoftBlue;
  }
  if (value == "romantic_purple") {
    return MoodMode::RomanticPurple;
  }
  if (value == "vital_green") {
    return MoodMode::VitalGreen;
  }
  if (value == "soft_orange") {
    return MoodMode::SoftOrange;
  }
  return MoodMode::WarmWhite;
}

String boolToJson(bool value) {
  return value ? "true" : "false";
}

String floatToJson(float value, int decimals = 1) {
  if (isnan(value)) {
    return "null";
  }
  return String(value, decimals);
}

bool getClock(struct tm *timeInfo) {
  return getLocalTime(timeInfo, 25);
}

int minuteOfDay(int hour, int minute) {
  return (hour * 60) + minute;
}

bool isMinuteInWindow(int nowMinute, int startMinute, int endMinute) {
  if (startMinute <= endMinute) {
    return nowMinute >= startMinute && nowMinute <= endMinute;
  }
  return nowMinute >= startMinute || nowMinute <= endMinute;
}

float progressInWindow(int nowMinute, int startMinute, int endMinute) {
  if (!isMinuteInWindow(nowMinute, startMinute, endMinute)) {
    return -1.0f;
  }

  int span = endMinute - startMinute;
  int current = nowMinute - startMinute;
  if (span < 0) {
    span += 24 * 60;
  }
  if (current < 0) {
    current += 24 * 60;
  }
  if (span == 0) {
    return 1.0f;
  }
  return constrain(current / static_cast<float>(span), 0.0f, 1.0f);
}

void showDisplayMessage(const char *line1, const char *line2 = "", const char *line3 = "") {
  if (!displayReady) {
    return;
  }

  display.clearBuffer();
  display.setFont(u8g2_font_6x12_tf);
  display.drawStr(0, 14, line1);
  if (line2[0] != '\0') {
    display.drawStr(0, 30, line2);
  }
  if (line3[0] != '\0') {
    display.drawStr(0, 46, line3);
  }
  display.sendBuffer();
}

void savePersistentState() {
  preferences.putUChar("mode", static_cast<uint8_t>(controlMode));
  preferences.putUChar("mood", static_cast<uint8_t>(moodMode));
  preferences.putUChar("lift", static_cast<uint8_t>(blindTarget.liftPercent));
  preferences.putUChar("tilt", static_cast<uint8_t>(blindTarget.tiltPercent));
  preferences.putBool("wakeOn", scheduleState.wakeEnabled);
  preferences.putBool("sleepOn", scheduleState.sleepEnabled);
  preferences.putUChar("wakeHr", static_cast<uint8_t>(scheduleState.wakeHour));
  preferences.putUChar("wakeMin", static_cast<uint8_t>(scheduleState.wakeMinute));
  preferences.putUChar("sleepHr", static_cast<uint8_t>(scheduleState.sleepHour));
  preferences.putUChar("sleepMin", static_cast<uint8_t>(scheduleState.sleepMinute));
  preferences.putULong("travel", static_cast<uint32_t>(liftTravelSteps));
}

void loadPersistentState() {
  uint8_t storedMode = preferences.getUChar("mode", 0);
  uint8_t storedMood = preferences.getUChar("mood", 1);
  controlMode = storedMode == 1 ? ControlMode::Manual : ControlMode::AutoSunlight;
  moodMode = static_cast<MoodMode>(storedMood <= static_cast<uint8_t>(MoodMode::SoftOrange) ? storedMood : 1);
  blindTarget.liftPercent = clampPercent(preferences.getUChar("lift", 0));
  blindTarget.tiltPercent = clampPercent(preferences.getUChar("tilt", 0));
  blindState = blindTarget;
  scheduleState.wakeEnabled = preferences.getBool("wakeOn", true);
  scheduleState.sleepEnabled = preferences.getBool("sleepOn", true);
  scheduleState.wakeHour = preferences.getUChar("wakeHr", 7);
  scheduleState.wakeMinute = preferences.getUChar("wakeMin", 0);
  scheduleState.sleepHour = preferences.getUChar("sleepHr", 23);
  scheduleState.sleepMinute = preferences.getUChar("sleepMin", 30);
  liftTravelSteps = static_cast<long>(preferences.getULong("travel", APP_LIFT_FALLBACK_TRAVEL_STEPS));
  if (liftTravelSteps <= 0) {
    liftTravelSteps = APP_LIFT_FALLBACK_TRAVEL_STEPS;
  }
}

void shortBeep(uint16_t frequency, uint16_t durationMs) {
  ledcWriteTone(APP_BUZZER_CHANNEL, frequency);
  delay(durationMs);
  ledcWriteTone(APP_BUZZER_CHANNEL, 0);
}

void playAlertPattern(const String &message) {
  if (message == lastAlertMessage) {
    return;
  }

  if (message.indexOf("Air poor") >= 0) {
    shortBeep(1760, 80);
    delay(50);
    shortBeep(2349, 120);
  } else if (message.indexOf("Rain") >= 0) {
    shortBeep(1200, 70);
    delay(40);
    shortBeep(1200, 70);
  } else if (message.indexOf("Homing failed") >= 0) {
    shortBeep(900, 180);
    delay(60);
    shortBeep(760, 180);
  }

  lastAlertMessage = message;
}

void setBlindTarget(int liftPercent, int tiltPercent, bool persist) {
  blindTarget.liftPercent = clampPercent(liftPercent);
  blindTarget.tiltPercent = clampPercent(tiltPercent);

  liftStepper.moveTo(liftPercentToSteps(blindTarget.liftPercent));
  tiltStepper.moveTo(tiltPercentToSteps(blindTarget.tiltPercent));
  tiltStepper.enableOutputs();
  tiltOutputsEnabled = true;

  if (persist) {
    savePersistentState();
  }
}

void updateBlindState() {
  blindState.liftPercent = liftStepsToPercent(liftStepper.currentPosition());
  blindState.tiltPercent = tiltStepsToPercent(tiltStepper.currentPosition());
}

float humidityComfortScore(float humidityPct) {
  if (isnan(humidityPct)) {
    return 50.0f;
  }
  float deviation = fabsf(humidityPct - 40.0f);
  return constrain(100.0f - (deviation * 2.2f), 0.0f, 100.0f);
}

float computeApproxAirScore(float humidityPct, float gasOhms) {
  if (gasOhms <= 0.0f || isnan(humidityPct)) {
    return NAN;
  }

  if (gasBaselineOhms <= 0.0f) {
    gasBaselineOhms = gasOhms;
  } else if (gasOhms > gasBaselineOhms) {
    gasBaselineOhms = gasOhms;
  }

  float gasRatio = gasBaselineOhms > 0.0f ? gasOhms / gasBaselineOhms : 0.5f;
  float gasScore = constrain(gasRatio * 100.0f, 0.0f, 100.0f);
  float humidityScore = humidityComfortScore(humidityPct);
  return (humidityScore * 0.25f) + (gasScore * 0.75f);
}

void readSensors() {
  if (bme680Ready && bme680.performReading()) {
    sensors.temperatureC = bme680.temperature;
    sensors.humidityPct = bme680.humidity;
    sensors.pressureHpa = bme680.pressure / 100.0f;
    sensors.gasResistanceKOhm = bme680.gas_resistance / 1000.0f;
    sensors.airScore = computeApproxAirScore(sensors.humidityPct, bme680.gas_resistance);
    sensors.airPoor = !isnan(sensors.airScore) && sensors.airScore < APP_IAQ_POOR_THRESHOLD;
  }

  if (lightMeterReady) {
    float lux = lightMeter.readLightLevel();
    if (lux >= 0.0f) {
      sensors.lightLux = lux;
    }
  }

  sensors.raining = rainDetected();
  sensors.batteryVoltage = readBatteryVoltage();
  sensors.batteryPct = batteryPercentFromVoltage(sensors.batteryVoltage);
  sensors.ventilationRecommended = sensors.airPoor && !sensors.raining;
}

void applySunlightAutomation() {
  if (controlMode != ControlMode::AutoSunlight) {
    return;
  }

  int targetLift = blindTarget.liftPercent;
  int targetTilt = blindTarget.tiltPercent;

  if (sensors.raining) {
    targetLift = 0;
    targetTilt = 0;
  } else if (!isnan(sensors.lightLux) && sensors.lightLux >= APP_LIGHT_VERY_BRIGHT_LUX) {
    targetLift = 10;
    targetTilt = 12;
  } else if (!isnan(sensors.lightLux) && sensors.lightLux >= APP_LIGHT_COMFORT_LUX) {
    targetLift = 75;
    targetTilt = 50;
  } else if (!isnan(sensors.lightLux) && sensors.lightLux >= APP_LIGHT_DAYLIGHT_MIN_LUX) {
    targetLift = 95;
    targetTilt = 70;
  } else {
    targetLift = 0;
    targetTilt = 0;
  }

  if (targetLift != blindTarget.liftPercent || targetTilt != blindTarget.tiltPercent) {
    setBlindTarget(targetLift, targetTilt, false);
  }
}

void evaluateAlerts() {
  String message = "Environment stable";

  if (!liftHomed) {
    message = "Lift homing failed";
  } else if (sensors.airPoor && sensors.raining) {
    message = "Air poor - raining";
  } else if (sensors.ventilationRecommended) {
    message = "Air poor - ventilate";
  } else if (sensors.raining) {
    message = "Rain detected";
  }

  playAlertPattern(message);
}

RgbColor baseMoodColor() {
  switch (moodMode) {
    case MoodMode::Off:
      return {0, 0, 0, 0};
    case MoodMode::WarmWhite:
      return {255, 180, 110, APP_LED_DEFAULT_BRIGHTNESS};
    case MoodMode::SoftBlue:
      return {80, 150, 255, APP_LED_DEFAULT_BRIGHTNESS};
    case MoodMode::RomanticPurple:
      return {180, 60, 220, APP_LED_DEFAULT_BRIGHTNESS};
    case MoodMode::VitalGreen:
      return {70, 210, 120, APP_LED_DEFAULT_BRIGHTNESS};
    case MoodMode::SoftOrange:
      return {255, 120, 40, APP_LED_DEFAULT_BRIGHTNESS};
  }
  return {255, 180, 110, APP_LED_DEFAULT_BRIGHTNESS};
}

RgbColor blendedColor(const RgbColor &a, const RgbColor &b, float progress) {
  progress = constrain(progress, 0.0f, 1.0f);
  RgbColor out;
  out.r = lroundf(a.r + (b.r - a.r) * progress);
  out.g = lroundf(a.g + (b.g - a.g) * progress);
  out.b = lroundf(a.b + (b.b - a.b) * progress);
  out.brightness = lroundf(a.brightness + (b.brightness - a.brightness) * progress);
  return out;
}

RgbColor activeMoodColor() {
  struct tm timeInfo;
  if (!getClock(&timeInfo)) {
    return baseMoodColor();
  }

  const int nowMinute = minuteOfDay(timeInfo.tm_hour, timeInfo.tm_min);

  if (scheduleState.wakeEnabled) {
    int wakeMinute = minuteOfDay(scheduleState.wakeHour, scheduleState.wakeMinute);
    int startMinute = wakeMinute - APP_WAKE_FADE_MINUTES;
    if (startMinute < 0) {
      startMinute += 24 * 60;
    }

    float wakeProgress = progressInWindow(nowMinute, startMinute, wakeMinute);
    if (wakeProgress >= 0.0f) {
      if (APP_WAKE_OPENS_BLINDS && !wakeAssistLatched && wakeProgress >= 0.99f) {
        setBlindTarget(APP_WAKE_BLIND_LIFT, APP_WAKE_BLIND_TILT, false);
        wakeAssistLatched = true;
      }
      return blendedColor({255, 96, 24, 12}, {255, 255, 220, 86}, wakeProgress);
    }

    if (nowMinute != wakeMinute) {
      wakeAssistLatched = false;
    }
  }

  if (scheduleState.sleepEnabled) {
    int sleepMinute = minuteOfDay(scheduleState.sleepHour, scheduleState.sleepMinute);
    int startMinute = sleepMinute - APP_SLEEP_FADE_MINUTES;
    if (startMinute < 0) {
      startMinute += 24 * 60;
    }

    float sleepProgress = progressInWindow(nowMinute, startMinute, sleepMinute);
    if (sleepProgress >= 0.0f) {
      return blendedColor({255, 180, 90, 40}, {0, 0, 0, 0}, sleepProgress);
    }
  }

  return baseMoodColor();
}

void renderMoodLight() {
  RgbColor color = activeMoodColor();
  strip.setBrightness(color.brightness);
  uint32_t packed = strip.Color(color.r, color.g, color.b);
  for (uint16_t i = 0; i < APP_LED_COUNT; ++i) {
    strip.setPixelColor(i, packed);
  }
  strip.show();
}

void drawDisplay() {
  if (!displayReady) {
    return;
  }

  char line[32];
  const char *wifiLabel = accessPointMode ? "AP" : (wifiConnected ? "STA" : "OFF");

  display.clearBuffer();
  display.setFont(u8g2_font_5x8_tf);

  snprintf(line, sizeof(line), "%s %s B:%d%%", modeToString(controlMode), wifiLabel, sensors.batteryPct);
  display.drawStr(0, 8, line);

  snprintf(line, sizeof(line), "Lift:%3d Tilt:%3d %s", blindState.liftPercent, blindState.tiltPercent, liftHomed ? "HOME" : "NOHOME");
  display.drawStr(0, 18, line);

  snprintf(line, sizeof(line), "T:%4.1fC H:%2.0f%%", sensors.temperatureC, sensors.humidityPct);
  display.drawStr(0, 28, line);

  snprintf(line, sizeof(line), "Lux:%6.0f Rain:%s", sensors.lightLux, sensors.raining ? "YES" : "NO");
  display.drawStr(0, 38, line);

  snprintf(line, sizeof(line), "IAQ:%4.0f Gas:%5.1fk", sensors.airScore, sensors.gasResistanceKOhm);
  display.drawStr(0, 48, line);

  String alert = lastAlertMessage;
  if (alert.length() > 21) {
    alert = alert.substring(0, 21);
  }
  display.drawStr(0, 60, alert.c_str());
  display.sendBuffer();
}

String buildStatusJson() {
  String json = "{";
  json += "\"device\":\"" + String(APP_DEVICE_NAME) + "\",";
  json += "\"mode\":\"" + String(modeToString(controlMode)) + "\",";
  json += "\"mood\":\"" + String(moodToString(moodMode)) + "\",";
  json += "\"alert\":\"" + lastAlertMessage + "\",";
  json += "\"wifi\":{\"connected\":" + boolToJson(wifiConnected) + ",\"apMode\":" + boolToJson(accessPointMode) + "},";
  json += "\"blind\":{\"lift\":" + String(blindState.liftPercent) + ",\"tilt\":" + String(blindState.tiltPercent) + ",\"homed\":" + boolToJson(liftHomed) + ",\"travelSteps\":" + String(liftTravelSteps) + "},";
  json += "\"schedule\":{\"wakeEnabled\":" + boolToJson(scheduleState.wakeEnabled);
  json += ",\"wake\":\"";
  if (scheduleState.wakeHour < 10) json += "0";
  json += String(scheduleState.wakeHour);
  json += ":";
  if (scheduleState.wakeMinute < 10) json += "0";
  json += String(scheduleState.wakeMinute);
  json += "\",\"sleepEnabled\":" + boolToJson(scheduleState.sleepEnabled);
  json += ",\"sleep\":\"";
  if (scheduleState.sleepHour < 10) json += "0";
  json += String(scheduleState.sleepHour);
  json += ":";
  if (scheduleState.sleepMinute < 10) json += "0";
  json += String(scheduleState.sleepMinute);
  json += "\"},";
  json += "\"sensors\":{";
  json += "\"temperatureC\":" + floatToJson(sensors.temperatureC) + ",";
  json += "\"humidityPct\":" + floatToJson(sensors.humidityPct) + ",";
  json += "\"pressureHpa\":" + floatToJson(sensors.pressureHpa) + ",";
  json += "\"gasResistanceKOhm\":" + floatToJson(sensors.gasResistanceKOhm) + ",";
  json += "\"airScore\":" + floatToJson(sensors.airScore) + ",";
  json += "\"lightLux\":" + floatToJson(sensors.lightLux, 0) + ",";
  json += "\"raining\":" + boolToJson(sensors.raining) + ",";
  json += "\"batteryVoltage\":" + floatToJson(sensors.batteryVoltage, 2) + ",";
  json += "\"batteryPct\":" + String(sensors.batteryPct) + ",";
  json += "\"ventilate\":" + boolToJson(sensors.ventilationRecommended);
  json += "}}";
  return json;
}

String buildHtmlPage() {
  return String(
      "<!doctype html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Smart Blind</title>"
      "<style>body{font-family:Arial,sans-serif;background:#f2eee8;color:#222;padding:20px;max-width:860px;margin:auto;}"
      "h1{margin-bottom:8px;}section{background:#fff;border-radius:16px;padding:16px;margin:14px 0;box-shadow:0 8px 24px rgba(0,0,0,.08);}"
      "button,input,select{font-size:16px;padding:10px;border-radius:10px;border:1px solid #bbb;width:100%;margin-top:8px;box-sizing:border-box;}"
      ".row{display:grid;grid-template-columns:1fr 1fr;gap:12px;}pre{background:#111;color:#c9f4c9;padding:12px;border-radius:12px;overflow:auto;}</style>"
      "</head><body><h1>Smart Blind Controller</h1>"
      "<p>ESP32 DevKitC indoor controller page for the capstone prototype.</p>"
      "<section><h2>Status</h2><pre id='status'>Loading...</pre><button onclick='refreshStatus()'>Refresh</button></section>"
      "<section><h2>Blind</h2><div class='row'><div><label>Lift %</label><input id='lift' type='number' min='0' max='100' value='75'></div>"
      "<div><label>Tilt %</label><input id='tilt' type='number' min='0' max='100' value='50'></div></div>"
      "<button onclick='sendBlind()'>Apply Manual Blind Position</button>"
      "<button onclick=\"setMode('manual')\">Switch to Manual</button>"
      "<button onclick=\"setMode('auto')\">Switch to Auto Sunlight</button>"
      "<button onclick='homeLift()'>Re-home Lift Axis</button></section>"
      "<section><h2>Mood Light</h2><select id='mood'>"
      "<option value='warm_white'>Warm White</option><option value='soft_blue'>Soft Blue</option><option value='romantic_purple'>Romantic Purple</option>"
      "<option value='vital_green'>Vital Green</option><option value='soft_orange'>Soft Orange</option><option value='off'>Off</option></select>"
      "<button onclick='sendMood()'>Apply Light Mode</button></section>"
      "<section><h2>Schedule</h2><div class='row'><div><label>Wake time</label><input id='wake' type='time' value='07:00'></div>"
      "<div><label>Sleep time</label><input id='sleep' type='time' value='23:30'></div></div><button onclick='sendSchedule()'>Save Schedule</button></section>"
      "<script>"
      "async function api(url,method='GET'){const res=await fetch(url,{method});return res.text();}"
      "async function refreshStatus(){const txt=await api('/api/status');document.getElementById('status').textContent=txt;}"
      "async function setMode(mode){await api('/api/mode?mode='+encodeURIComponent(mode),'POST');refreshStatus();}"
      "async function sendBlind(){const l=document.getElementById('lift').value;const t=document.getElementById('tilt').value;"
      "await api('/api/blinds?lift='+l+'&tilt='+t,'POST');refreshStatus();}"
      "async function sendMood(){const m=document.getElementById('mood').value;await api('/api/light?mode='+encodeURIComponent(m),'POST');refreshStatus();}"
      "async function sendSchedule(){const w=document.getElementById('wake').value;const s=document.getElementById('sleep').value;"
      "await api('/api/schedule?wake='+w+'&sleep='+s+'&enableWake=1&enableSleep=1','POST');refreshStatus();}"
      "async function homeLift(){document.getElementById('status').textContent='Homing...';await api('/api/home','POST');refreshStatus();}"
      "refreshStatus();setInterval(refreshStatus,4000);"
      "</script></body></html>");
}

bool parseTimeValue(const String &value, int &hour, int &minute) {
  int sep = value.indexOf(':');
  if (sep < 0) {
    return false;
  }
  hour = value.substring(0, sep).toInt();
  minute = value.substring(sep + 1).toInt();
  return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59;
}

void sendJsonResponse(const String &body) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", body);
}

bool moveLiftUntilSwitch(uint8_t switchPin, float speed) {
  long startPosition = liftStepper.currentPosition();
  liftStepper.setSpeed(speed);

  while (!liftLimitTriggered(switchPin)) {
    liftStepper.runSpeed();
    if (labs(liftStepper.currentPosition() - startPosition) >= APP_LIFT_HOMING_SEARCH_LIMIT_STEPS) {
      return false;
    }
    yield();
  }
  return true;
}

void moveLiftRelativeBlocking(long steps) {
  liftStepper.move(steps);
  while (liftStepper.distanceToGo() != 0) {
    liftStepper.run();
    yield();
  }
}

bool performLiftHoming() {
  setLiftDriverEnabled(true);
  showDisplayMessage("Lift homing", "Finding bottom");

  if (liftLimitTriggered(APP_LIFT_LIMIT_BOTTOM_PIN)) {
    moveLiftRelativeBlocking(APP_LIFT_HOMING_BACKOFF_STEPS);
  }

  if (!moveLiftUntilSwitch(APP_LIFT_LIMIT_BOTTOM_PIN, -fabsf(APP_LIFT_HOMING_SPEED))) {
    liftHomed = false;
    playAlertPattern("Lift homing failed");
    return false;
  }

  liftStepper.setCurrentPosition(0);
  moveLiftRelativeBlocking(APP_LIFT_HOMING_BACKOFF_STEPS);
  liftStepper.setCurrentPosition(APP_LIFT_HOMING_BACKOFF_STEPS);

  showDisplayMessage("Lift homing", "Finding top");
  if (!moveLiftUntilSwitch(APP_LIFT_LIMIT_TOP_PIN, fabsf(APP_LIFT_HOMING_SPEED))) {
    liftHomed = false;
    playAlertPattern("Lift homing failed");
    return false;
  }

  liftTravelSteps = liftStepper.currentPosition();
  if (liftTravelSteps <= APP_LIFT_HOMING_BACKOFF_STEPS) {
    liftTravelSteps = APP_LIFT_FALLBACK_TRAVEL_STEPS;
    liftHomed = false;
    playAlertPattern("Lift homing failed");
    return false;
  }

  moveLiftRelativeBlocking(-APP_LIFT_HOMING_BACKOFF_STEPS);
  liftHomed = true;
  preferences.putULong("travel", static_cast<uint32_t>(liftTravelSteps));
  lastAlertMessage = "Lift homed";
  return true;
}

void handleRoot() {
  server.send(200, "text/html", buildHtmlPage());
}

void handleStatus() {
  sendJsonResponse(buildStatusJson());
}

void handleMode() {
  if (server.hasArg("mode")) {
    controlMode = server.arg("mode") == "auto" ? ControlMode::AutoSunlight : ControlMode::Manual;
    savePersistentState();
  }
  sendJsonResponse(buildStatusJson());
}

void handleBlinds() {
  controlMode = ControlMode::Manual;
  int lift = server.hasArg("lift") ? server.arg("lift").toInt() : blindTarget.liftPercent;
  int tilt = server.hasArg("tilt") ? server.arg("tilt").toInt() : blindTarget.tiltPercent;
  setBlindTarget(lift, tilt, true);
  sendJsonResponse(buildStatusJson());
}

void handleLight() {
  if (server.hasArg("mode")) {
    moodMode = moodFromString(server.arg("mode"));
    savePersistentState();
  }
  sendJsonResponse(buildStatusJson());
}

void handleSchedule() {
  int hour = 0;
  int minute = 0;

  if (server.hasArg("wake") && parseTimeValue(server.arg("wake"), hour, minute)) {
    scheduleState.wakeHour = hour;
    scheduleState.wakeMinute = minute;
  }
  if (server.hasArg("sleep") && parseTimeValue(server.arg("sleep"), hour, minute)) {
    scheduleState.sleepHour = hour;
    scheduleState.sleepMinute = minute;
  }
  if (server.hasArg("enableWake")) {
    scheduleState.wakeEnabled = server.arg("enableWake").toInt() != 0;
  }
  if (server.hasArg("enableSleep")) {
    scheduleState.sleepEnabled = server.arg("enableSleep").toInt() != 0;
  }

  savePersistentState();
  sendJsonResponse(buildStatusJson());
}

void handleHome() {
  performLiftHoming();
  setBlindTarget(blindTarget.liftPercent, blindTarget.tiltPercent, true);
  sendJsonResponse(buildStatusJson());
}

void handleOptions() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(204, "text/plain", "");
}

void handleNotFound() {
  server.send(404, "application/json", "{\"error\":\"not_found\"}");
}

void setupHttpServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/status", HTTP_OPTIONS, handleOptions);
  server.on("/api/mode", HTTP_POST, handleMode);
  server.on("/api/mode", HTTP_OPTIONS, handleOptions);
  server.on("/api/blinds", HTTP_POST, handleBlinds);
  server.on("/api/blinds", HTTP_OPTIONS, handleOptions);
  server.on("/api/light", HTTP_POST, handleLight);
  server.on("/api/light", HTTP_OPTIONS, handleOptions);
  server.on("/api/schedule", HTTP_POST, handleSchedule);
  server.on("/api/schedule", HTTP_OPTIONS, handleOptions);
  server.on("/api/home", HTTP_POST, handleHome);
  server.on("/api/home", HTTP_OPTIONS, handleOptions);
  server.onNotFound(handleNotFound);
  server.begin();
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  if (strlen(APP_WIFI_SSID) > 0) {
    WiFi.begin(APP_WIFI_SSID, APP_WIFI_PASSWORD);
    unsigned long startMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startMs < 15000) {
      delay(300);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    accessPointMode = false;
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(APP_DEVICE_NAME, APP_AP_PASSWORD);
    wifiConnected = false;
    accessPointMode = true;
  }
}

void configureTimeSync() {
  configTime(APP_GMT_OFFSET_SECONDS, APP_DAYLIGHT_OFFSET_SECONDS, APP_NTP_SERVER);
}

void setupDisplay() {
  display.begin();
  displayReady = true;
  showDisplayMessage("Smart blind", "Booting...");
}

void setupSensors() {
  bme680Ready = bme680.begin(APP_BME680_ADDRESS, &Wire);
  if (bme680Ready) {
    bme680.setTemperatureOversampling(BME680_OS_8X);
    bme680.setHumidityOversampling(BME680_OS_2X);
    bme680.setPressureOversampling(BME680_OS_4X);
    bme680.setIIRFilterSize(BME680_FILTER_SIZE_3);
    bme680.setGasHeater(320, 150);
  }

  lightMeterReady = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, APP_BH1750_ADDRESS, &Wire);
}

void setupSteppers() {
  pinMode(APP_LIFT_ENABLE_PIN, OUTPUT);
  setLiftDriverEnabled(true);

  liftStepper.setMaxSpeed(APP_LIFT_MAX_SPEED);
  liftStepper.setAcceleration(APP_LIFT_ACCEL);

  tiltStepper.setMaxSpeed(APP_TILT_MAX_SPEED);
  tiltStepper.setAcceleration(APP_TILT_ACCEL);
  tiltStepper.disableOutputs();

  liftStepper.setCurrentPosition(liftPercentToSteps(blindState.liftPercent));
  tiltStepper.setCurrentPosition(tiltPercentToSteps(blindState.tiltPercent));
  setBlindTarget(blindState.liftPercent, blindState.tiltPercent, false);
}

void printStatusToSerial() {
  Serial.printf(
      "[status] mode=%s lift=%d tilt=%d homed=%s temp=%.1fC hum=%.0f%% lux=%.0f iaq=%.0f batt=%d%% rain=%s alert=%s\n",
      modeToString(controlMode),
      blindState.liftPercent,
      blindState.tiltPercent,
      liftHomed ? "yes" : "no",
      sensors.temperatureC,
      sensors.humidityPct,
      sensors.lightLux,
      sensors.airScore,
      sensors.batteryPct,
      sensors.raining ? "yes" : "no",
      lastAlertMessage.c_str());
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetPinAttenuation(APP_BATTERY_ANALOG_PIN, ADC_11db);

  pinMode(APP_RAIN_DIGITAL_PIN, INPUT);
  pinMode(APP_LIFT_LIMIT_BOTTOM_PIN, INPUT_PULLUP);
  pinMode(APP_LIFT_LIMIT_TOP_PIN, INPUT_PULLUP);
  pinMode(APP_BATTERY_ANALOG_PIN, INPUT);

  ledcSetup(APP_BUZZER_CHANNEL, 2000, 8);
  ledcAttachPin(APP_BUZZER_PIN, APP_BUZZER_CHANNEL);

  preferences.begin("smart-blind", false);
  loadPersistentState();

  Wire.begin(APP_I2C_SDA_PIN, APP_I2C_SCL_PIN);
  setupDisplay();

  strip.begin();
  strip.clear();
  strip.show();

  setupSensors();
  setupSteppers();
  showDisplayMessage("Smart blind", "Connecting Wi-Fi");
  connectWiFi();
  configureTimeSync();
  setupHttpServer();

  showDisplayMessage("Smart blind", "Reading sensors");
  readSensors();
  performLiftHoming();
  setBlindTarget(blindTarget.liftPercent, blindTarget.tiltPercent, false);
  evaluateAlerts();
  drawDisplay();
  renderMoodLight();
  printStatusToSerial();
}

void loop() {
  server.handleClient();

  liftStepper.run();
  tiltStepper.run();
  updateBlindState();

  if (tiltOutputsEnabled && tiltStepper.distanceToGo() == 0) {
    tiltStepper.disableOutputs();
    tiltOutputsEnabled = false;
  }

  unsigned long nowMs = millis();

  if (nowMs - lastSensorReadMs >= APP_SENSOR_INTERVAL_MS) {
    lastSensorReadMs = nowMs;
    readSensors();
    applySunlightAutomation();
    evaluateAlerts();
  }

  if (nowMs - lastLightRefreshMs >= APP_LIGHT_REFRESH_MS) {
    lastLightRefreshMs = nowMs;
    renderMoodLight();
  }

  if (nowMs - lastDisplayRefreshMs >= APP_DISPLAY_INTERVAL_MS) {
    lastDisplayRefreshMs = nowMs;
    drawDisplay();
  }

  if (nowMs - lastStatusPrintMs >= APP_STATUS_PRINT_INTERVAL_MS) {
    lastStatusPrintMs = nowMs;
    printStatusToSerial();
  }
}
