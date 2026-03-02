# Project Memory

## Hardware: Waveshare ESP32-S3-Touch-LCD-1.9

See full pin map and specs → `/workspace/hardware/waveshare-esp32s3-touch-lcd-1.9.md`

**Quick reference:**
- MCU: ESP32-S3R8 (8MB PSRAM, 16MB flash)
- Display: 1.9" IPS ST7789V2, 170×320, SPI
- Touch: CST816, I2C on GPIO47/48
- IMU: QMI8658A, I2C on GPIO47/48 (shared bus)
- Serial device: `/dev/ttyESP` (USB-CDC, ttyACM0)

## ESP-IDF Setup

- Version: v5.4.1, installed at `/opt/esp/esp-idf`
- Target: `esp32s3`
- Activate: `. /opt/esp/esp-idf/export.sh`
- Project: `/workspace/src/hello_world`

## Flashing

`idf.py flash` fails with write timeout — use esptool directly with `--before=usb_reset`.
See skill → `/workspace/skills/esp32s3-usb-serial-flash.md`

## Display & Touch (confirmed working)

Full driver config → `/workspace/hardware/waveshare-esp32s3-touch-lcd-1.9.md`

Key quirks to remember:
- ST7789V2: `invert_color=true`, `set_gap(35,0)`, `rgb_ele_order=RGB`
- CST816S IC id=182, I2C addr `0x15`, shared bus with IMU on GPIO47/48

## LVGL 9 / esp_lvgl_port v2

Working project: `/workspace/src/hello_world`
Component versions: LVGL 9.5.0, esp_lvgl_port v2, esp_lcd_touch_cst816s v1.x

**idf_component.yml:**
```yaml
dependencies:
  idf: ">=5.4.0"
  lvgl/lvgl: "^9.2.0"
  espressif/esp_lvgl_port: "^2.3.0"
  espressif/esp_lcd_touch_cst816s: "^1.1.0"
```

**CMakeLists.txt REQUIRES:** `esp_lcd esp_driver_spi esp_driver_i2c esp_driver_gpio esp_timer log freertos`
- Use `log`, not `esp_log` (component dir is `components/log/`)

**Key gotcha:** `lvgl_port_display_cfg_t.io_handle` is required — pass the SPI panel IO handle, not NULL.

**LVGL 9 API (vs LVGL 8):** `lv_display_t*`, `lv_button_create()`, `lv_obj_remove_flag()`, `lv_display_get_screen_active(disp)`

## Deep Sleep Current (confirmed measurements)

- **Measuring at LiPo J1 JST connector: always ~2 mA** — ETA6098 BAT→SYS active quiescent, not firmware-controllable
- `min_sleep_test` (4 stages) all identical ~2 mA → confirms ETA6098 is the floor
- Firmware-controlled floor at 3.3V rail (bypass charger): **~220 µA** with EXT1+IMU WoM
  - PSRAM ~140 µA (VDD_SPI stays on with EXT1), IMU 55 µA, ESP32 RTC 8 µA, buck 11 µA, LCD 7 µA
- ETA6098 "1 µA at BAT" = shutdown mode only, NOT active BAT→SYS mode
- Full details → `/workspace/hardware/waveshare-esp32s3-touch-lcd-1.9.md` Power section

## Deep Sleep GPIO Hold Pattern (confirmed working, ESP32-S3)

- Use `rtc_gpio_init()` + `rtc_gpio_set_level()` + `rtc_gpio_hold_en()` for output pins
- Release at every boot: `rtc_gpio_hold_dis()` + `rtc_gpio_deinit()` before re-using as digital
- Do NOT use `gpio_deep_sleep_hold_en()` — blocks EXT1 wakeup on GPIO8
- EXT1 wakeup pin (GPIO8): `rtc_gpio_pulldown_en()` required — floats HIGH without it
- Full pattern → `/workspace/skills/esp-idf-skills.md` sections 7–8

## hello_world Firmware Features

- HDC2080 temperature read at boot, displayed on UI (amber label)
- CST816S touch in reset before sleep (GPIO17=0 held in RTC domain, ~0.2 µA)
- QMI8658A WoM configured before sleep (55 µA, INT1 → EXT1 GPIO8)
- ST7789V2 DISPOFF + SLPIN before sleep (~7 µA)
- Backlight GPIO14=1 held in RTC domain
- Wakeup cause shown at boot: "Cold Boot" vs "Motion"
