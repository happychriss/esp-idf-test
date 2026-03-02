---
board: Waveshare ESP32-S3-Touch-LCD-1.9
mcu: ESP32-S3R8
---

# Waveshare ESP32-S3-Touch-LCD-1.9

## MCU
- ESP32-S3R8 — dual-core Xtensa LX7, 8MB embedded PSRAM
- 16MB SPI Flash (W25Q128JVSI)
- Built-in USB-Serial/JTAG → /dev/ttyACM0 (often aliased to /dev/ttyESP)

## Display — ST7789V2
- 1.9" IPS, 170×320, 262K colors, 500 cd/m², 900:1 contrast
- Interface: SPI

| Signal       | GPIO | Notes                    |
|--------------|------|--------------------------|
| LCD_RST      | 9    |                          |
| LCD_CLK      | 10   |                          |
| LCD_DC       | 11   |                          |
| LCD_CS       | 12   |                          |
| LCD_DIN      | 13   |                          |
| LCD_BL (PWM) | 14   | active-low (0=ON, 1=OFF) |

### ESP-IDF Driver (esp_lcd, built-in)

- SPI host: `SPI2_HOST`, 40 MHz, mode 0
- `lcd_cmd_bits=8`, `lcd_param_bits=8`, `trans_queue_depth=10`
- `rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB`
- `bits_per_pixel = 16`
- `invert_color = true`
- `set_gap(x=35, y=0)` — panel is offset 35 px in X

### Deep Sleep Power Sequence (confirmed working)

1. `gpio_set_level(LCD_BL_GPIO, 1)` — backlight off
2. `esp_lcd_panel_disp_on_off(panel, false)` — DISPOFF (0x28)
3. `esp_lcd_panel_io_tx_param(io, 0x10, NULL, 0)` — SLPIN (~17mA → ~7µA)
4. `vTaskDelay(pdMS_TO_TICKS(5))` — wait for sleep-in
5. `gpio_deep_sleep_hold_en()` — hold all GPIO output levels through sleep

### LVGL display flags (esp_lvgl_port v2)

- `color_format = LV_COLOR_FORMAT_RGB565`
- `flags.swap_bytes = true` (SPI big-endian vs LVGL little-endian)
- `flags.buff_dma = true`
- Draw buffer: 170 × 32 lines × 2 B ≈ 10 KB

## Touch — CST816S (I2C, shared bus with IMU)

- IC id reported at runtime: **182**
- I2C address: `0x15` (`ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS`)
- Component: `espressif/esp_lcd_touch_cst816s` (IDF Component Registry)

| Signal   | GPIO |
|----------|------|
| SDA      | 47   |
| SCL      | 48   |
| TP_RESET | 17   |
| TP_INT   | 21   |

### ESP-IDF Driver (esp_lcd_touch_cst816s)

- I2C port: `I2C_NUM_0`, 400 kHz, internal pullups enabled
- `rst_gpio_num=17`, `int_gpio_num=21`
- `levels.reset=0` (active-low reset), `levels.interrupt=0` (active-low INT)
- `x_max=170`, `y_max=320`, no swap/mirror
- panel_io_i2c: `control_phase_bytes=1`, `dc_bit_offset=0`, `lcd_cmd/param_bits=8`, `disable_control_phase=true`

### Deep Sleep Power

- Assert reset before sleep: `gpio_set_level(17, 0)` — ~0.2µA in reset vs ~2µA active
- GPIO17 held low by `gpio_deep_sleep_hold_en()` (called after setting level)

## IMU — QMI8658A 6-DOF (I2C, shared bus with touch)

- Datasheet: `/workspace/external-docs/QMI8658A_Datasheet_Rev_A.pdf`
- WHO_AM_I register: 0x00 → should return 0x05

| Signal   | GPIO | Notes                        |
|----------|------|------------------------------|
| SDA      | 47   | shared with touch CST816S    |
| SCL      | 48   | shared with touch CST816S    |
| IMU_INT1 | 8    | RTC-capable, EXT1 wakeup     |
| IMU_INT2 | 7    | RTC-capable                  |

### ESP-IDF Driver (raw I2C — no registry component)

- I2C address: **0x6B** (SA0=GND) — confirmed working
- Uses shared `i2c_master_bus_handle_t` — init bus once, pass handle to both touch and IMU
- No IMU init needed at boot — only configured when entering deep sleep

### Wake-on-Motion (WoM) Configuration

Key registers:
| Register | Address | Purpose |
|----------|---------|---------|
| CTRL1    | 0x02    | INT pin enable (bit3=INT1_EN push-pull) |
| CTRL2    | 0x03    | Accel ODR + full-scale |
| CTRL7    | 0x08    | Sensor enable (bit0=aEN) |
| CTRL8    | 0x09    | Motion routing (bit7=STATUSINT handshake) |
| CTRL9    | 0x0A    | Host command register |
| CAL1_L   | 0x0B    | WoM threshold (mg, 0x01–0xFF) |
| CAL1_H   | 0x0C    | WoM INT pin + polarity |
| STATUSINT| 0x2D    | bit7=CmdDone (poll for CTRL9 ACK) |

WoM config sequence (confirmed working):
1. CTRL7 = 0x00 (disable all sensors)
2. CTRL1 = 0x08 (INT1 push-pull enabled)
3. CTRL8 = 0x80 (use STATUSINT.bit7 for CTRL9 handshake, avoids INT1 conflict)
4. CTRL2 = 0x27 (accel: ±8g, 128 Hz low-power)
5. CAL1_L = 0xC0 (192 mg threshold — shake to wake; lower = more sensitive)
6. CAL1_H = 0x20 (INT1 initial LOW, 32-sample blanking ≈ 250 ms — prevents config glitch wakeup)
7. CTRL9 = 0x08 (CTRL_CMD_CONFIGURE_WOM)
8. Poll STATUSINT until bit7 = 1
9. CTRL9 = 0x00 (host ACK)
10. CTRL7 = 0x01 (enable accel)
11. Read STATUS1 (0x2F) to clear any pending WoM interrupt flag

### Deep Sleep Wakeup (confirmed working)

- EXT1 on GPIO8 (IMU_INT1), trigger = HIGH (`ESP_EXT1_WAKEUP_ANY_HIGH`)
- **Must** configure RTC GPIO pull-down on GPIO8 before sleep — pin floats HIGH otherwise causing immediate wakeup:
  ```c
  rtc_gpio_init(8); rtc_gpio_set_direction(8, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pulldown_en(8); rtc_gpio_pullup_dis(8);
  ```
- Use `gpio_deep_sleep_hold_en()` to hold backlight GPIO14=1 (OFF) during sleep
- `esp_sleep_get_wakeup_cause()` at boot: `ESP_SLEEP_WAKEUP_EXT1` = motion wake
- Include `driver/rtc_io.h` — no extra CMakeLists REQUIRES needed (part of `esp_driver_gpio`)

## SD Card (SPI via JTAG pins)

| Signal  | GPIO |
|---------|------|
| SD_MOSI | 39   |
| SD_MISO | 40   |
| SD_CLK  | 41   |

## Power
- USB-C → ETA6098 LiPo charger (max 2A)
  - Shutdown quiescent (CE=0 or VIN=0 shutdown mode): **1 µA at BAT**
  - **BAT→SYS active quiescent (no USB, board powered from LiPo): ~1–2 mA** — confirmed by measurement; this dominates deep sleep budget when powered from the LiPo/J1 connector
- LiPo battery connector (J1 JST)
- MP1605GTF-Z buck (5V → 3.3V), quiescent: **11 µA**
- BAT_ADC: GPIO4

### Deep Sleep Current Budget (confirmed via firmware test + measurement)

| Condition | Current | Notes |
|-----------|---------|-------|
| USB host (PC) connected | **~2 mA** | BBPLL kept alive by `CONFIG_RTC_CLOCK_BBPLL_POWER_ON_WITH_USB=y` |
| LiPo/J1 connector (no USB), EXT1 + IMU WoM | **~2 mA** | ETA6098 BAT→SYS path quiescent dominates |
| LiPo battery direct (bypassing ETA6098) | **~200–220 µA** | Firmware-controlled floor with IMU WoM |
| Timer only + VDDSDIO off (no EXT1) | **~80 µA** | PSRAM powered off; incompatible with EXT1 |

**Measurement notes:**
- Measuring at the LiPo J1 JST connector includes the ETA6098 charger IC's BAT-mode
  quiescent (~1–2 mA). This is NOT firmware-controllable.
- `min_sleep_test` confirmed: all 4 stages (display active → SLPIN → touch reset → IMU WoM)
  measured identical ~2 mA from the J1 connector. The firmware is correct; the ETA6098 is the floor.
- To see the actual MCU/peripheral current, measure on the 3.3V rail directly (bypass the charger).
- **Never measure with USB host (PC) connected** — BBPLL adds ~1–2 mA on top of everything.
- Connecting to a USB *charger* (not a PC) does NOT trigger BBPLL — only live USB host enumeration does.

### Firmware-controlled deep sleep floor (at 3.3V rail, EXT1 wakeup)

| Component | Current |
|-----------|---------|
| ESP32-S3 RTC domain | ~8 µA |
| PSRAM (VDD_SPI cannot power down with EXT1) | ~140 µA |
| QMI8658A WoM 128 Hz Low-Power | ~55 µA |
| ST7789V2 SLPIN | ~7 µA |
| MP1605GTF-Z buck Iq | ~11 µA |
| CST816S in reset (GPIO17=0) | ~0.2 µA |
| **Total** | **~221 µA** |

PSRAM breakdown: ESP32-S3R8 embeds 8 MB octal PSRAM on VDD_SPI rail shared with
flash. With any GPIO/EXT1 wakeup source, VDD_SPI cannot be powered down → PSRAM
draws **~140 µA** permanently (WROOM-1 datasheet Table 6-7).

To disable BBPLL during sleep (breaks USB reconnect after wakeup):
```
CONFIG_RTC_CLOCK_BBPLL_POWER_ON_WITH_USB=n
```

## System

| Signal  | GPIO |
|---------|------|
| BAT_ADC | 4    |
| U0_TX   | 43   |
| U0_RX   | 44   |
| BOOT0   | 0    |

## Expansion
- J3 (20-pin): IO0–IO21
- J4 (20-pin): IO20–IO48
- J5: U.FL/IPEX antenna connector
- KEY1, KEY2: user buttons
- BOOT + RESET buttons
