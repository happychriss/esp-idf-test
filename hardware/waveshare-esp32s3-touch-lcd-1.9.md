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

## IMU — QMI8658A 6-DOF (I2C, shared bus with touch)

| Signal   | GPIO |
|----------|------|
| SDA      | 47   |
| SCL      | 48   |
| IMU_INT1 | 8    |
| IMU_INT2 | 7    |

## SD Card (SPI via JTAG pins)

| Signal  | GPIO |
|---------|------|
| SD_MOSI | 39   |
| SD_MISO | 40   |
| SD_CLK  | 41   |

## Power
- USB-C → ETA6098 LiPo charger (max 2A)
- LiPo battery connector (J1 JST)
- MP1605GTF-Z boost → 3.314V
- BAT_ADC: GPIO4

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
