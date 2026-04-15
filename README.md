# Smart Blind ESP32 Firmware

This firmware is now aligned to the actual BOM you shared for the smart blind capstone.

## Target hardware

- Arduino Nano ESP32
- 2x 18650 cells
- TP4056 USB-C charger
- XL6009 step-up converter
- 5V 400mA solar panel
- BH1750 light sensor
- rain sensor module
- 1.3 inch OLED module
- BME680 environmental sensor
- 2x 8-pixel WS2812B stick modules
- lift motor:
  - hybrid stepper motor
  - DRV8825
  - 2x KW-10R limit switch
- slat tilt motor:
  - 5V stepper motor
  - ULN2003 board

## What the firmware does

- Reads ambient light from `BH1750`.
- Reads temperature, humidity, pressure, and gas resistance from `BME680`.
- Calculates an approximate indoor air score from `BME680` humidity and gas resistance.
- Reads the rain sensor digital output.
- Reads battery voltage through an analog divider.
- Controls the lift axis through `DRV8825`.
- Homes the lift axis with top and bottom `KW-10R` limit switches.
- Controls the slat tilt axis with the `ULN2003` stepper.
- Drives `16` total WS2812 LEDs.
- Shows live status on the OLED.
- Exposes a small built-in web UI and JSON API over Wi-Fi.

## Important hardware notes

- `TP4056` is a `1S` charger. That means your two `18650` cells must be used as `1S2P`, not `2S`.
- `BME680` does not measure true `CO2`, `eCO2`, or `PM2.5`. In this firmware it is used as an approximate indoor air-quality sensor only.
- Your current BOM does not include a real outdoor dust sensor, so the presentation's outdoor fine-dust feature is not implemented here.
- Many `1.3"` OLED modules are `SH1106`, so this firmware defaults to `SH1106`. If your display is actually `SSD1306`, change `APP_USE_SH1106_DISPLAY` in [include/app_config.h](include/app_config.h).

## Project files

- [platformio.ini](platformio.ini): PlatformIO environment and libraries
- [include/app_config.h](include/app_config.h): pin map, thresholds, addresses, schedules
- [src/main.cpp](src/main.cpp): main firmware logic

## Default pin map for Arduino Nano ESP32

- `SDA`: I2C SDA
- `SCL`: I2C SCL
- `D2`: rain sensor digital input
- `A0`: battery voltage sense
- `D3`: passive buzzer
- `D4`: WS2812 data
- `D5`: DRV8825 STEP
- `D6`: DRV8825 DIR
- `D7`: DRV8825 ENABLE
- `D8`: lift bottom limit switch
- `D9`: lift top limit switch
- `D10`: ULN2003 IN1
- `D11`: ULN2003 IN2
- `D12`: ULN2003 IN3
- `A1`: ULN2003 IN4

## Implemented behavior

- `auto sunlight` mode:
  - very bright outdoor light: mostly close blind
  - normal daylight: open blind for daylight
  - dark / night: close blind for privacy
  - rain detected: close blind
- `manual` mode:
  - set lift and tilt percentages from the web UI or API
- wake / sleep mood light:
  - wake mode fades from orange to white
  - sleep mode fades down to off
- lift homing:
  - startup homing runs automatically
  - `/api/home` can re-home the lift axis later

## Web API

- `GET /api/status`
- `POST /api/mode?mode=auto|manual`
- `POST /api/blinds?lift=0..100&tilt=0..100`
- `POST /api/light?mode=warm_white|soft_blue|romantic_purple|vital_green|soft_orange|off`
- `POST /api/schedule?wake=07:00&sleep=23:30&enableWake=1&enableSleep=1`
- `POST /api/home`

The root page `/` gives you a simple browser control page for testing before the mobile app exists.

## Bring-up steps

1. Open the project in VS Code with PlatformIO.
2. Edit Wi-Fi credentials in [include/app_config.h](include/app_config.h).
3. Check sensor I2C addresses:
   - `BH1750` usually `0x23`
   - `BME680` often `0x76` or `0x77`
4. Verify OLED controller type and switch the display macro if needed.
5. Wire the modules to the Nano ESP32 pin map above, or change the pin constants in [include/app_config.h](include/app_config.h) to match your wiring.
6. Adjust `APP_BATTERY_DIVIDER_RATIO` to match your resistor divider.
7. Adjust `APP_LIFT_FALLBACK_TRAVEL_STEPS` and `APP_TILT_MAX_STEPS` after first mechanical test.
8. Upload firmware and open serial monitor at `115200`.
9. If station Wi-Fi is not configured, connect to the ESP32 access point named `smart-blind`.

## What you may still want to add

- real outdoor dust sensing
- real `CO2` or `eCO2` sensing
- mobile app integration
- MQTT or Home Assistant integration
- calibration storage for the BME680 air score
- safer power-path and battery telemetry if you move beyond prototype level
