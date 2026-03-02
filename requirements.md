# Project Requirements

## Project Type

Embedded firmware — follow `/workspace/skills/embedded-project-setup.md` for working conventions, folder structure, knowledge flow, and build workflow.

---

## Hardware

**Board:** Waveshare ESP32-S3-Touch-LCD-1.9
**Full config:** `/workspace/hardware/waveshare-esp32s3-touch-lcd-1.9.md`

---

## Functional Specification

### 1. Hello World Display + Touch UI

**Status:** Complete
**Source:** `/workspace/src/hello_world`

**Description:**
Bring up the ST7789V2 LCD and CST816S touch controller and render an interactive UI.

**Requirements:**
- Display a "Hello World" label centred on screen
- Display an "OK" button below the label
- On OK button press: hide the label and button, show "THANK YOU" in cyan
- After 1500 ms: revert to the Hello World screen automatically
- Backlight on at boot

**Implementation notes:**
- LVGL 9 via esp_lvgl_port v2
- Components: `lvgl/lvgl ^9.2.0`, `espressif/esp_lvgl_port ^2.3.0`, `espressif/esp_lcd_touch_cst816s ^1.1.0`
- Draw buffer: 170 × 32 lines DMA, RGB565 with byte-swap
- One-shot `lv_timer` (repeat_count=1) handles the revert — no manual delete needed

---

### 2. Deep Sleep + IMU Wake-on-Motion

**Status:** Complete
**Source:** `/workspace/src/hello_world`

**Description:**
Extend the UI with a sleep/wake cycle. The device shows its boot reason, sleeps on button press with minimal power draw, and wakes on motion via the QMI8658A IMU.

**Requirements:**
- At boot: display wake-up reason ("Cold Boot" or "Motion Wake")
- Display a "Send me to sleep" button
- On button press:
  - Show "Going to sleep..." briefly (1 s)
  - Turn off backlight
  - Turn off LCD panel
  - Put IMU into Wake-on-Motion mode (accel only, low ODR, motion threshold ~50 mg)
  - ESP32-S3 enters deep sleep; EXT1 wakeup on IMU INT1 (GPIO8) rising edge
- On wakeup: full reinit (deep sleep = reset), show "Motion Wake" as reason
- Device lifting should reliably trigger wakeup

**Hardware involved:**
- QMI8658A IMU: I2C on GPIO47/48 (shared bus), INT1=GPIO8, INT2=GPIO7
- ST7789V2: backlight GPIO14 active-low, panel off via esp_lcd API
- ESP32-S3 deep sleep: EXT1 on GPIO8 (RTC-capable)

**Implementation notes:**
- No registry component for QMI8658A — use raw I2C master writes
- I2C address: 0x6B (SA0=GND, to be confirmed at first run via WHO_AM_I register 0x00 = 0x05)
- CTRL9 protocol: use STATUSINT.bit7 polling (CTRL8.bit7=1) to avoid INT1 contention
- WoM config sequence: disable sensors → set CTRL2 ODR → write CAL1 → CTRL9 0x08 → poll done → ACK → enable accel
- RTC domain GPIO hold (rtc_gpio_hold_en) keeps backlight and touch reset stable during sleep — no gpio_deep_sleep_hold_en()
- IMU only initialised when entering sleep — not needed at boot
- User intervention required to test: lift device to verify wakeup

---

### 3. HDC2080 Temperature Display

**Status:** Complete
**Source:** `/workspace/src/hello_world`

**Description:**
Read ambient temperature from an externally connected HDC2080 sensor at boot and display
it on-screen. Ensure the sensor is in sleep mode before deep sleep entry.

**Requirements:**
- At boot: read temperature from HDC2080 over shared I2C bus
- Display temperature in °C on the main UI screen (amber label, below wake reason)
- Show "Temp: N/A" if sensor not found or read fails
- Before deep sleep: confirm HDC2080 MEAS_CFG=0x00 (no pending trigger, sensor asleep)

**Hardware involved:**
- HDC2080: external I2C sensor, SDA=GPIO47, SCL=GPIO48 (shared bus)
- I2C address: 0x40 (ADDR=GND) or 0x41 (ADDR=VDD)
- DRDY/INT pin: not wired — polling with 15 ms delay after trigger

**Implementation notes:**
- No registry component — raw I2C via i2c_master_bus_add_device / i2c_master_bus_rm_device
- Single-shot measurement: write 0x01 to MEAS_CFG (0x0F), wait 15 ms, read 0x00–0x01
- Temperature formula: T(°C) = (raw[15:0] / 65536) × 165 − 40.5
- Device is already in sleep after power-up and after each single-shot measurement (AMM=0 default)
- Sleep before deep sleep: write 0x00 to MEAS_CFG — called in enter_deep_sleep_cb before IMU config
- Driver config → `/workspace/hardware/hdc2080.md`
