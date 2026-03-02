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
