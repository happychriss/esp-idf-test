---
name: esp-idf-component-registry
description: Find and add ready-made components from the ESP-IDF Component Registry (components.espressif.com) to an ESP-IDF project
---

# ESP-IDF Component Registry

## What It Is

https://components.espressif.com — Espressif's official package registry for ESP-IDF components. Similar to npm or PyPI. Espressif and the community publish reusable drivers and libraries here (LCD panels, touch ICs, sensors, protocol stacks, LVGL, etc.).

The component manager (`idf-component-manager`) is included with ESP-IDF v5.x — no extra install needed.

## Adding a Component to a Project

### 1. Via CLI (recommended)

Run inside the project directory with ESP-IDF environment active:

```bash
. /opt/esp/esp-idf/export.sh
idf.py add-dependency "espressif/component_name==version"
```

This creates or updates `main/idf_component.yml` automatically.

### 2. Manually — create `main/idf_component.yml`

```yaml
dependencies:
  espressif/esp_lcd_st7789:
    version: ">=1.0.0"
  espressif/esp_lcd_touch_cst816:
    version: ">=1.0.0"
  lvgl/lvgl:
    version: ">=8.3.0"
  idf:
    version: ">=5.0.0"
```

Place this file next to `main/CMakeLists.txt`.

### 3. Build — components are fetched automatically

```bash
idf.py build
```

Components are downloaded to `managed_components/` in the project root on first build.

## Finding Components

- Browse: https://components.espressif.com
- Search by chip, peripheral, or keyword
- Each component page shows the exact `idf_component.yml` snippet to copy

## Useful Components for This Board (Waveshare ESP32-S3-Touch-LCD-1.9)

| Hardware       | Component                              |
|----------------|----------------------------------------|
| ST7789V2 LCD   | `espressif/esp_lcd_st7789`             |
| CST816 touch   | `espressif/esp_lcd_touch_cst816`       |
| QMI8658A IMU   | search registry or use community fork  |
| LVGL UI        | `lvgl/lvgl` + `espressif/esp_lvgl_port`|

## Notes

- `managed_components/` should be added to `.gitignore` (like `node_modules`)
- Component versions follow semver; pin versions for production builds
- `idf_component.yml` can live in `main/` or any custom component directory
