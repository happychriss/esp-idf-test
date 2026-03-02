/*
 * Deep Sleep + IMU Wake-on-Motion
 * Waveshare ESP32-S3-Touch-LCD-1.9 (ST7789V2 + CST816S + QMI8658A)
 * LVGL 9 / esp_lvgl_port v2
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_sleep.h"

/* RTC GPIO (for pull-down on wakeup pin) */
#include "driver/rtc_io.h"

/* LCD / SPI */
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

/* I2C (touch + IMU share one bus) */
#include "driver/i2c_master.h"
#include "esp_lcd_touch_cst816s.h"

/* LVGL port */
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char *TAG = "sleep_ui";

/* ---------- LCD pins ---------- */
#define LCD_HOST          SPI2_HOST
#define LCD_CLK_GPIO      10
#define LCD_MOSI_GPIO     13
#define LCD_CS_GPIO       12
#define LCD_DC_GPIO       11
#define LCD_RST_GPIO      9
#define LCD_BL_GPIO       14
#define LCD_H_RES         170
#define LCD_V_RES         320
#define LCD_X_GAP         35
#define LCD_Y_GAP         0
#define LCD_SPI_CLK_HZ    (40 * 1000 * 1000)

/* ---------- Touch / IMU I2C ---------- */
#define I2C_PORT          I2C_NUM_0
#define I2C_SDA_GPIO      47
#define I2C_SCL_GPIO      48
#define I2C_CLK_HZ        400000

#define TOUCH_RST_GPIO    17
#define TOUCH_INT_GPIO    21

#define IMU_INT1_GPIO     8
#define IMU_I2C_ADDR      0x6B   /* SA0=GND; verify WHO_AM_I(0x00)==0x05 */

/* ---------- QMI8658A registers ---------- */
#define QMI_WHO_AM_I      0x00
#define QMI_CTRL1         0x02
#define QMI_CTRL2         0x03
#define QMI_CTRL7         0x08
#define QMI_CTRL8         0x09
#define QMI_CTRL9         0x0A
#define QMI_CAL1_L        0x0B
#define QMI_CAL1_H        0x0C
#define QMI_STATUSINT     0x2D
#define QMI_STATUS1       0x2F   /* read to clear WoM interrupt flag */

#define QMI_CMD_WOM       0x08   /* CTRL9: configure Wake-on-Motion */
#define WOM_THRESHOLD_MG  0xC0   /* 192 mg — shake required to wake */
#define WOM_BLANKING      0x20   /* 32 samples blanking (~250 ms at 128 Hz) */

/* ---------- Draw buffer ---------- */
#define DRAW_BUF_LINES    32
#define DRAW_BUF_SIZE     (LCD_H_RES * DRAW_BUF_LINES * sizeof(uint16_t))

/* ---------- Globals ---------- */
static esp_lcd_panel_handle_t  g_panel  = NULL;
static i2c_master_bus_handle_t g_i2c_bus = NULL;

static lv_obj_t *g_label_wake  = NULL;
static lv_obj_t *g_label_status = NULL;
static lv_obj_t *g_btn_sleep   = NULL;

/* ==========================================================================
 * I2C bus
 * ======================================================================= */

static i2c_master_bus_handle_t i2c_bus_init(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port              = I2C_PORT,
        .sda_io_num            = I2C_SDA_GPIO,
        .scl_io_num            = I2C_SCL_GPIO,
        .clk_source            = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt     = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &bus));
    return bus;
}

/* ==========================================================================
 * LCD
 * ======================================================================= */

static esp_lcd_panel_handle_t lcd_init(esp_lcd_panel_io_handle_t *io_out)
{
    spi_bus_config_t buscfg = {
        .mosi_io_num     = LCD_MOSI_GPIO,
        .miso_io_num     = -1,
        .sclk_io_num     = LCD_CLK_GPIO,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = DRAW_BUF_SIZE,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num       = LCD_DC_GPIO,
        .cs_gpio_num       = LCD_CS_GPIO,
        .pclk_hz           = LCD_SPI_CLK_HZ,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .spi_mode          = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io));

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST_GPIO,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel, LCD_X_GAP, LCD_Y_GAP));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    gpio_set_direction(LCD_BL_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_BL_GPIO, 0);   /* active-low: 0 = ON */

    *io_out = io;
    return panel;
}

/* ==========================================================================
 * Touch
 * ======================================================================= */

static esp_lcd_touch_handle_t touch_init(i2c_master_bus_handle_t bus)
{
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = {
        .dev_addr                  = ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS,
        .scl_speed_hz              = I2C_CLK_HZ,
        .control_phase_bytes       = 1,
        .dc_bit_offset             = 0,
        .lcd_cmd_bits              = 8,
        .lcd_param_bits            = 8,
        .flags.disable_control_phase = true,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bus, &tp_io_cfg, &tp_io));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max        = LCD_H_RES,
        .y_max        = LCD_V_RES,
        .rst_gpio_num = TOUCH_RST_GPIO,
        .int_gpio_num = TOUCH_INT_GPIO,
        .levels.reset = 0,
        .levels.interrupt = 0,
    };
    esp_lcd_touch_handle_t tp = NULL;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst816s(tp_io, &tp_cfg, &tp));
    return tp;
}

/* ==========================================================================
 * QMI8658A — raw I2C helpers
 * ======================================================================= */

static esp_err_t imu_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(dev, buf, 2, 100);
}

static esp_err_t imu_read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *val)
{
    esp_err_t ret = i2c_master_transmit(dev, &reg, 1, 100);
    if (ret != ESP_OK) return ret;
    return i2c_master_receive(dev, val, 1, 100);
}

static esp_err_t imu_ctrl9_cmd(i2c_master_dev_handle_t dev, uint8_t cmd)
{
    ESP_ERROR_CHECK(imu_write_reg(dev, QMI_CTRL9, cmd));
    /* Poll STATUSINT.bit7 (CmdDone) with timeout */
    for (int i = 0; i < 100; i++) {
        uint8_t status = 0;
        imu_read_reg(dev, QMI_STATUSINT, &status);
        if (status & 0x80) {
            imu_write_reg(dev, QMI_CTRL9, 0x00); /* ACK */
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    ESP_LOGE(TAG, "CTRL9 command 0x%02X timeout", cmd);
    return ESP_ERR_TIMEOUT;
}

static void imu_configure_wom(i2c_master_bus_handle_t bus)
{
    /* Add IMU device to shared I2C bus */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = IMU_I2C_ADDR,
        .scl_speed_hz    = I2C_CLK_HZ,
    };
    i2c_master_dev_handle_t dev;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &dev));

    /* Verify WHO_AM_I */
    uint8_t who = 0;
    imu_read_reg(dev, QMI_WHO_AM_I, &who);
    ESP_LOGI(TAG, "QMI8658A WHO_AM_I: 0x%02X (expect 0x05)", who);
    if (who != 0x05) {
        ESP_LOGW(TAG, "Unexpected WHO_AM_I — check I2C address (try 0x6A)");
    }

    /* 1. Disable all sensors */
    imu_write_reg(dev, QMI_CTRL7, 0x00);

    /* 2. Enable INT1 as push-pull output */
    imu_write_reg(dev, QMI_CTRL1, 0x08);

    /* 3. Use STATUSINT.bit7 for CTRL9 handshake (avoids INT1 conflict) */
    imu_write_reg(dev, QMI_CTRL8, 0x80);

    /* 4. Accel: ±8g full-scale, 128 Hz low-power ODR */
    imu_write_reg(dev, QMI_CTRL2, 0x27);

    /* 5. WoM parameters: threshold, INT1 initially LOW, blanking time */
    imu_write_reg(dev, QMI_CAL1_L, WOM_THRESHOLD_MG);
    imu_write_reg(dev, QMI_CAL1_H, WOM_BLANKING); /* bits[7:6]=00 INT1 low, bits[5:0]=blanking */

    /* 6. Execute WoM configure command */
    if (imu_ctrl9_cmd(dev, QMI_CMD_WOM) == ESP_OK) {
        ESP_LOGI(TAG, "WoM configured OK");
    }

    /* 7. Enable accelerometer */
    imu_write_reg(dev, QMI_CTRL7, 0x01);

    /* 8. Clear any pending WoM interrupt by reading STATUS1 */
    uint8_t dummy = 0;
    imu_read_reg(dev, QMI_STATUS1, &dummy);
    ESP_LOGI(TAG, "STATUS1 cleared: 0x%02X", dummy);

    i2c_master_bus_rm_device(dev);
}

/* ==========================================================================
 * Sleep sequence
 * ======================================================================= */

static void enter_deep_sleep_cb(lv_timer_t *timer)
{
    (void)timer;
    ESP_LOGI(TAG, "Entering deep sleep");

    /* Backlight off, hold GPIO state through deep sleep */
    gpio_set_level(LCD_BL_GPIO, 1);
    gpio_deep_sleep_hold_en();

    /* Display off */
    esp_lcd_panel_disp_on_off(g_panel, false);

    /* Configure IMU Wake-on-Motion */
    imu_configure_wom(g_i2c_bus);

    /* Pull GPIO8 LOW via RTC so it can't float HIGH during sleep */
    rtc_gpio_init(IMU_INT1_GPIO);
    rtc_gpio_set_direction(IMU_INT1_GPIO, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_en(IMU_INT1_GPIO);
    rtc_gpio_pullup_dis(IMU_INT1_GPIO);

    /* EXT1 wakeup: GPIO8 (IMU INT1) HIGH */
    esp_sleep_enable_ext1_wakeup(1ULL << IMU_INT1_GPIO, ESP_EXT1_WAKEUP_ANY_HIGH);

    esp_deep_sleep_start();
}

static void countdown_cb(lv_timer_t *timer)
{
    (void)timer;
    lv_label_set_text(g_label_status, "Sleeping now...");

    lv_timer_t *t = lv_timer_create(enter_deep_sleep_cb, 500, NULL);
    lv_timer_set_repeat_count(t, 1);
}

/* ==========================================================================
 * UI callbacks
 * ======================================================================= */

static void btn_sleep_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    lv_obj_add_flag(g_btn_sleep, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(g_label_status, "Place device down...");

    /* 3 s to put device down, then 0.5 s final message, then sleep */
    lv_timer_t *t = lv_timer_create(countdown_cb, 3000, NULL);
    lv_timer_set_repeat_count(t, 1);
}

/* ==========================================================================
 * UI
 * ======================================================================= */

static void ui_create(lv_display_t *disp, const char *wake_reason)
{
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    /* Wake reason label — top */
    g_label_wake = lv_label_create(scr);
    lv_label_set_text(g_label_wake, wake_reason);
    lv_obj_set_style_text_color(g_label_wake, lv_color_hex(0x00ffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(g_label_wake, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(g_label_wake, LV_ALIGN_CENTER, 0, -50);

    /* Status label — centre */
    g_label_status = lv_label_create(scr);
    lv_label_set_text(g_label_status, "Lift to wake");
    lv_obj_set_style_text_color(g_label_status, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(g_label_status, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(g_label_status, LV_ALIGN_CENTER, 0, 0);

    /* Sleep button — bottom */
    g_btn_sleep = lv_button_create(scr);
    lv_obj_set_size(g_btn_sleep, 140, 40);
    lv_obj_align(g_btn_sleep, LV_ALIGN_CENTER, 0, 60);
    lv_obj_add_event_cb(g_btn_sleep, btn_sleep_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_label = lv_label_create(g_btn_sleep);
    lv_label_set_text(btn_label, "Send me to sleep");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(btn_label);
}

/* ==========================================================================
 * app_main
 * ======================================================================= */

void app_main(void)
{
    /* Determine wake reason */
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    const char *wake_str;
    switch (cause) {
        case ESP_SLEEP_WAKEUP_EXT1: wake_str = "Wake: Motion";    break;
        default:                     wake_str = "Wake: Cold Boot"; break;
    }
    ESP_LOGI(TAG, "%s", wake_str);

    /* Shared I2C bus */
    g_i2c_bus = i2c_bus_init();

    /* LCD */
    esp_lcd_panel_io_handle_t lcd_io = NULL;
    g_panel = lcd_init(&lcd_io);

    /* Touch */
    esp_lcd_touch_handle_t touch = touch_init(g_i2c_bus);

    /* LVGL */
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle     = lcd_io,
        .panel_handle  = g_panel,
        .buffer_size   = DRAW_BUF_SIZE / sizeof(uint16_t),
        .double_buffer = false,
        .hres          = LCD_H_RES,
        .vres          = LCD_V_RES,
        .monochrome    = false,
        .color_format  = LV_COLOR_FORMAT_RGB565,
        .rotation      = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        .flags         = { .buff_dma = true, .swap_bytes = true },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);

    const lvgl_port_touch_cfg_t touch_cfg = { .disp = disp, .handle = touch };
    lvgl_port_add_touch(&touch_cfg);

    /* UI */
    lvgl_port_lock(0);
    ui_create(disp, wake_str);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "UI ready — %s", wake_str);
}
