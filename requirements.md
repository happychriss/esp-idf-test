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

## Future Requirements

> Add new functional specifications here as the project grows.
