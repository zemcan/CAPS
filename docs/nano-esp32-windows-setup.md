# Arduino Nano ESP32 Windows Setup Log

This document records the Windows setup and connection verification steps completed for the Arduino Nano ESP32 on 2026-04-15.

## Environment

- OS: Windows
- IDE: Arduino IDE 2.3.8
- Board core: `arduino:esp32 2.0.18-arduino.5`
- Target board: `Arduino Nano ESP32`

## What was installed

### 1. Espressif USB JTAG driver package

The Espressif driver package was downloaded with `idf-env` and registered in the Windows driver store.

- Tool used: `idf-env.exe`
- Installed package: `USB_JTAG_debug_unit.inf`
- Device match: `VID_303A&PID_1001&MI_02`

This step prepares the PC for ESP32 USB JTAG / WinUSB-based interfaces.

### 2. Arduino IDE

Arduino IDE was installed locally with `winget`.

- Installed version: `2.3.8`

### 3. Arduino Nano ESP32 board support

The Arduino ESP32 core was installed through `arduino-cli`.

- Installed core: `arduino:esp32`
- Installed version: `2.0.18-arduino.5`
- Verified board entry: `Arduino Nano ESP32`
- FQBN: `arduino:esp32:nano_nora`

### 4. Nano ESP32 DFU driver

When the board was connected, Windows detected the DFU interface but initially showed a driver error. The Arduino-provided driver was then installed manually from the board core package.

- Driver file:
  `C:\Users\USER\AppData\Local\Arduino15\packages\arduino\hardware\esp32\2.0.18-arduino.5\drivers\nanoesp32.inf`
- Installed device name: `Arduino DFU`
- USB match: `USB\VID_2341&PID_0070&MI_00`

After this step, the DFU interface changed from error state to normal operation.

## Verification results

The board is now detected correctly in both serial mode and DFU mode.

### Windows device status

- `USB 직렬 장치 (COM3)` -> normal
- `Arduino DFU` -> normal
- `USB Composite Device` for the board -> normal

### Arduino CLI status

- Serial port: `COM3`
- Board name: `Arduino Nano ESP32`
- FQBN: `arduino:esp32:nano_nora`
- DFU interface also detected by `arduino-cli board list`

## How to use it now

1. Open Arduino IDE.
2. Select board: `Arduino Nano ESP32`.
3. Select port: `COM3`.
4. Upload sketches normally.

If the board enters DFU mode again during recovery or bootloader operations, the required Windows driver is already installed.
