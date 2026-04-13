#pragma once

#include <Arduino.h>

#define APP_USE_SH1106_DISPLAY 1

static constexpr char APP_DEVICE_NAME[] = "smart-blind";
static constexpr char APP_WIFI_SSID[] = "";
static constexpr char APP_WIFI_PASSWORD[] = "";
static constexpr char APP_AP_PASSWORD[] = "blindsetup";
static constexpr char APP_NTP_SERVER[] = "pool.ntp.org";

static constexpr long APP_GMT_OFFSET_SECONDS = 9 * 3600;
static constexpr int APP_DAYLIGHT_OFFSET_SECONDS = 0;
static constexpr uint16_t APP_HTTP_PORT = 80;

static constexpr uint8_t APP_I2C_SDA_PIN = 21;
static constexpr uint8_t APP_I2C_SCL_PIN = 22;
static constexpr uint8_t APP_BH1750_ADDRESS = 0x23;
static constexpr uint8_t APP_BME680_ADDRESS = 0x77;

static constexpr uint8_t APP_RAIN_DIGITAL_PIN = 35;
static constexpr uint8_t APP_RAIN_ACTIVE_LEVEL = LOW;

static constexpr uint8_t APP_BATTERY_ANALOG_PIN = 36;
static constexpr float APP_BATTERY_DIVIDER_RATIO = 2.0f;
static constexpr float APP_BATTERY_MIN_VOLTAGE = 3.30f;
static constexpr float APP_BATTERY_MAX_VOLTAGE = 4.15f;

static constexpr uint8_t APP_BUZZER_PIN = 25;
static constexpr uint8_t APP_BUZZER_CHANNEL = 0;

static constexpr uint8_t APP_LED_STRIP_PIN = 26;
static constexpr uint16_t APP_LED_COUNT = 16;
static constexpr uint8_t APP_LED_DEFAULT_BRIGHTNESS = 48;

static constexpr uint8_t APP_LIFT_STEP_PIN = 27;
static constexpr uint8_t APP_LIFT_DIR_PIN = 14;
static constexpr uint8_t APP_LIFT_ENABLE_PIN = 13;
static constexpr uint8_t APP_LIFT_ENABLE_ACTIVE_LEVEL = LOW;
static constexpr uint8_t APP_LIFT_LIMIT_BOTTOM_PIN = 32;
static constexpr uint8_t APP_LIFT_LIMIT_TOP_PIN = 33;
static constexpr uint8_t APP_LIFT_LIMIT_ACTIVE_LEVEL = LOW;

static constexpr uint8_t APP_TILT_IN1_PIN = 16;
static constexpr uint8_t APP_TILT_IN2_PIN = 17;
static constexpr uint8_t APP_TILT_IN3_PIN = 18;
static constexpr uint8_t APP_TILT_IN4_PIN = 19;

static constexpr long APP_LIFT_FALLBACK_TRAVEL_STEPS = 6000;
static constexpr float APP_LIFT_MAX_SPEED = 1400.0f;
static constexpr float APP_LIFT_ACCEL = 900.0f;
static constexpr float APP_LIFT_HOMING_SPEED = 500.0f;
static constexpr long APP_LIFT_HOMING_BACKOFF_STEPS = 120;
static constexpr long APP_LIFT_HOMING_SEARCH_LIMIT_STEPS = 9000;

static constexpr long APP_TILT_MAX_STEPS = 2048;
static constexpr float APP_TILT_MAX_SPEED = 700.0f;
static constexpr float APP_TILT_ACCEL = 350.0f;

static constexpr uint32_t APP_SENSOR_INTERVAL_MS = 2000;
static constexpr uint32_t APP_DISPLAY_INTERVAL_MS = 1000;
static constexpr uint32_t APP_STATUS_PRINT_INTERVAL_MS = 5000;
static constexpr uint32_t APP_LIGHT_REFRESH_MS = 40;

static constexpr float APP_LIGHT_VERY_BRIGHT_LUX = 25000.0f;
static constexpr float APP_LIGHT_COMFORT_LUX = 8000.0f;
static constexpr float APP_LIGHT_DAYLIGHT_MIN_LUX = 1200.0f;
static constexpr float APP_IAQ_POOR_THRESHOLD = 55.0f;

static constexpr int APP_WAKE_FADE_MINUTES = 10;
static constexpr int APP_SLEEP_FADE_MINUTES = 15;
static constexpr bool APP_WAKE_OPENS_BLINDS = true;
static constexpr uint8_t APP_WAKE_BLIND_LIFT = 70;
static constexpr uint8_t APP_WAKE_BLIND_TILT = 50;
