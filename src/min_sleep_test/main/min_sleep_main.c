/*
 * Minimal Deep Sleep Current Test
 * Waveshare ESP32-S3-Touch-LCD-1.9
 *
 * Cycles through 4 stages automatically (60 s per stage, timer wakeup).
 * Measure supply current during the sleep window at each stage to isolate
 * what is consuming the excess ~2 mA.
 *
 * Stage 0  Baseline  — BL off, display still ACTIVE (no SLPIN), no SPI GPIO holds
 * Stage 1  LCD sleep — BL off + SLPIN + all LCD SPI GPIOs held in RTC domain
 * Stage 2  + Touch   — Stage 1 + CST816S in reset (GPIO17 = 0 held)
 * Stage 3  + IMU WoM — Stage 2 + QMI8658A Wake-on-Motion (timer wakeup, not EXT1)
 *            IMU WoM at 128 Hz LP = 55 µA per datasheet — should add negligibly
 *
 * Expected results if LCD SPI float is the culprit:
 *   Stage 0: ~2 mA  (display active)
 *   Stage 1: ~50 µA (display SLPIN + SPI bus held)
 *   Stage 2: ~50 µA (no change — CST816S in reset was already low)
 *   Stage 3: ~105 µA (+ 55 µA IMU WoM)
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"

static const char *TAG = "min_sleep";

/* ---------- LCD ---------- */
#define LCD_HOST      SPI2_HOST
#define LCD_CLK_GPIO  ((gpio_num_t)10)
#define LCD_MOSI_GPIO ((gpio_num_t)13)
#define LCD_CS_GPIO   ((gpio_num_t)12)
#define LCD_DC_GPIO   ((gpio_num_t)11)
#define LCD_RST_GPIO  ((gpio_num_t)9)
#define LCD_BL_GPIO   ((gpio_num_t)14)   /* active-low: 0=ON, 1=OFF */

/* ---------- Touch ---------- */
#define TOUCH_RST_GPIO ((gpio_num_t)17)  /* active-low reset */

/* ---------- IMU ---------- */
#define IMU_INT1_GPIO  ((gpio_num_t)8)
#define IMU_I2C_ADDR   0x6B
#define I2C_SDA_GPIO   ((gpio_num_t)47)
#define I2C_SCL_GPIO   ((gpio_num_t)48)
#define I2C_CLK_HZ     400000

/* ---------- Sleep duration per stage ---------- */
#define SLEEP_US  (60ULL * 1000000ULL)   /* 60 seconds */

/* ---------- Stage counter in RTC memory (survives deep sleep) ---------- */
static RTC_DATA_ATTR int s_stage = 0;
#define NUM_STAGES 4

static const char *s_stage_name[NUM_STAGES] = {
    "Stage 0 — Baseline: BL off, display ACTIVE, no SPI holds",
    "Stage 1 — LCD SLPIN + all SPI GPIOs held in RTC domain",
    "Stage 2 — Stage 1 + touch reset (GPIO17=0 held)",
    "Stage 3 — Stage 2 + IMU WoM at 128 Hz LP (~55 uA extra)",
};

/* ==========================================================================
 * Helpers
 * ======================================================================= */

/* Release every RTC hold from the previous boot to start clean. */
static void release_all_rtc_holds(void)
{
    for (int g = 0; g <= 21; g++) {
        rtc_gpio_hold_dis((gpio_num_t)g);
        rtc_gpio_deinit((gpio_num_t)g);
    }
}

/* Drive a digital output then lock it in the RTC domain so it is held stably
 * through deep sleep regardless of CONFIG_ESP_SLEEP_GPIO_RESET_WORKAROUND. */
static void hold_output(gpio_num_t gpio, int level)
{
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(gpio, level);
    rtc_gpio_init(gpio);
    rtc_gpio_set_direction(gpio, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level(gpio, level);
    rtc_gpio_hold_en(gpio);
}

/* Send DISPOFF (0x28) + SLPIN (0x10) to the ST7789V2 via raw SPI.
 * Uses polling mode — no DMA, no callbacks, no interrupt concerns. */
static void lcd_send_slpin(void)
{
    gpio_set_direction(LCD_DC_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(LCD_CS_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_CS_GPIO, 1);

    spi_bus_config_t buscfg = {
        .mosi_io_num   = LCD_MOSI_GPIO,
        .miso_io_num   = -1,
        .sclk_io_num   = LCD_CLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10 * 1000 * 1000,
        .mode           = 0,
        .spics_io_num   = LCD_CS_GPIO,
        .queue_size     = 1,
    };
    spi_device_handle_t spi;
    ESP_ERROR_CHECK(spi_bus_add_device(LCD_HOST, &devcfg, &spi));

    uint8_t cmd;
    spi_transaction_t t = { .length = 8, .tx_buffer = &cmd };

    gpio_set_level(LCD_DC_GPIO, 0);  /* command phase */

    cmd = 0x28;  /* DISPOFF */
    spi_device_polling_transmit(spi, &t);
    vTaskDelay(1);

    cmd = 0x10;  /* SLPIN — display draws ~7 µA after this */
    spi_device_polling_transmit(spi, &t);
    vTaskDelay(pdMS_TO_TICKS(5));   /* ST7789V2 needs ≥5 ms to enter sleep */

    spi_bus_remove_device(spi);
    spi_bus_free(LCD_HOST);         /* returns GPIO9-13 to digital GPIO mode */

    ESP_LOGI(TAG, "LCD: DISPOFF + SLPIN sent");
}

/* ==========================================================================
 * IMU — QMI8658A Wake-on-Motion (same as hello_world, timer wakeup not EXT1)
 * ======================================================================= */

static void imu_write(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    i2c_master_transmit(dev, buf, 2, 100);
}

static uint8_t imu_read(i2c_master_dev_handle_t dev, uint8_t reg)
{
    uint8_t val = 0;
    i2c_master_transmit(dev, &reg, 1, 100);
    i2c_master_receive(dev, &val, 1, 100);
    return val;
}

static void imu_configure_wom(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port              = I2C_NUM_0,
        .sda_io_num            = I2C_SDA_GPIO,
        .scl_io_num            = I2C_SCL_GPIO,
        .clk_source            = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt     = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = IMU_I2C_ADDR,
        .scl_speed_hz    = I2C_CLK_HZ,
    };
    i2c_master_dev_handle_t dev;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &dev));

    uint8_t who = imu_read(dev, 0x00);
    ESP_LOGI(TAG, "IMU WHO_AM_I: 0x%02X (expect 0x05)", who);

    imu_write(dev, 0x08, 0x00);  /* CTRL7: disable all sensors */
    imu_write(dev, 0x02, 0x08);  /* CTRL1: INT1 push-pull */
    imu_write(dev, 0x09, 0x80);  /* CTRL8: STATUSINT handshake */
    imu_write(dev, 0x03, 0x27);  /* CTRL2: ±8g, 128 Hz LP — 55 µA per datasheet */
    imu_write(dev, 0x0B, 0xC0);  /* CAL1_L: 192 mg threshold */
    imu_write(dev, 0x0C, 0x20);  /* CAL1_H: INT1 initial LOW, 32-sample blanking */

    /* CTRL9 WoM command with STATUSINT.bit7 polling */
    imu_write(dev, 0x0A, 0x08);
    for (int i = 0; i < 100; i++) {
        if (imu_read(dev, 0x2D) & 0x80) {
            imu_write(dev, 0x0A, 0x00);  /* ACK */
            ESP_LOGI(TAG, "IMU: WoM OK");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    imu_write(dev, 0x08, 0x01);      /* CTRL7: enable accel */
    imu_read(dev, 0x2F);              /* STATUS1: clear WoM flag */
    i2c_master_bus_rm_device(dev);
    /* I2C peripheral powers down in deep sleep — no need to delete bus */
}

/* ==========================================================================
 * app_main
 * ======================================================================= */

void app_main(void)
{
    /* Release all RTC holds from previous boot — must be first */
    release_all_rtc_holds();

    /* Advance stage after every timer wakeup */
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
        s_stage = (s_stage + 1) % NUM_STAGES;
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=========================================");
    ESP_LOGI(TAG, "  %s", s_stage_name[s_stage]);
    ESP_LOGI(TAG, "  Sleep: %llu s — measure current now!", SLEEP_US / 1000000ULL);
    ESP_LOGI(TAG, "=========================================");

    /* Always turn backlight off (active-low) */
    gpio_set_direction(LCD_BL_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_BL_GPIO, 1);

    switch (s_stage) {

    case 0:
        /* Baseline: backlight off held in RTC, display still active.
         * LCD SPI GPIOs (9-13) float during sleep via isolation workaround.
         * Hypothesis: floating SPI inputs on active ST7789V2 draw excess current. */
        hold_output(LCD_BL_GPIO, 1);
        break;

    case 1:
        /* LCD SLPIN + hold all SPI GPIOs in RTC domain with correct idle levels.
         * GPIO9-13 are all RTC-capable (GPIO0-21 on ESP32-S3).
         * This prevents floating inputs on the ST7789V2's SPI interface. */
        lcd_send_slpin();
        hold_output(LCD_BL_GPIO,   1);  /* backlight off */
        hold_output(LCD_RST_GPIO,  1);  /* RST high — display NOT in hard reset */
        hold_output(LCD_CLK_GPIO,  0);  /* SPI clock idle */
        hold_output(LCD_DC_GPIO,   0);  /* D/C idle */
        hold_output(LCD_CS_GPIO,   1);  /* CS deasserted */
        hold_output(LCD_MOSI_GPIO, 0);  /* MOSI idle */
        break;

    case 2:
        /* Stage 1 + CST816S touch in reset */
        lcd_send_slpin();
        hold_output(LCD_BL_GPIO,    1);
        hold_output(LCD_RST_GPIO,   1);
        hold_output(LCD_CLK_GPIO,   0);
        hold_output(LCD_DC_GPIO,    0);
        hold_output(LCD_CS_GPIO,    1);
        hold_output(LCD_MOSI_GPIO,  0);
        hold_output(TOUCH_RST_GPIO, 0); /* CST816S: assert reset, ~0.2 µA */
        break;

    case 3:
        /* Stage 2 + IMU WoM (datasheet: 55 µA at 128 Hz LP).
         * Wakeup via timer only — not testing EXT1 in this program. */
        lcd_send_slpin();
        hold_output(LCD_BL_GPIO,    1);
        hold_output(LCD_RST_GPIO,   1);
        hold_output(LCD_CLK_GPIO,   0);
        hold_output(LCD_DC_GPIO,    0);
        hold_output(LCD_CS_GPIO,    1);
        hold_output(LCD_MOSI_GPIO,  0);
        hold_output(TOUCH_RST_GPIO, 0);

        imu_configure_wom();

        /* GPIO8: RTC input with pulldown — prevents false wakeup if EXT1 added later */
        rtc_gpio_init(IMU_INT1_GPIO);
        rtc_gpio_set_direction(IMU_INT1_GPIO, RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_pulldown_en(IMU_INT1_GPIO);
        rtc_gpio_pullup_dis(IMU_INT1_GPIO);
        break;
    }

    esp_sleep_enable_timer_wakeup(SLEEP_US);
    vTaskDelay(pdMS_TO_TICKS(50));  /* allow serial to flush */
    ESP_LOGI(TAG, "Entering deep sleep...");
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_deep_sleep_start();
}
