/*
 * Hello World Display + Touch UI
 * Waveshare ESP32-S3-Touch-LCD-1.9 (ST7789V2 + CST816S)
 * LVGL 9 / esp_lvgl_port v2
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

/* LCD / SPI */
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

/* Touch / I2C */
#include "driver/i2c_master.h"
#include "esp_lcd_touch_cst816s.h"

/* LVGL port */
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char *TAG = "hello_ui";

/* ---------- Pin definitions ---------- */
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

#define TOUCH_I2C_PORT    I2C_NUM_0
#define TOUCH_SDA_GPIO    47
#define TOUCH_SCL_GPIO    48
#define TOUCH_RST_GPIO    17
#define TOUCH_INT_GPIO    21
#define TOUCH_I2C_HZ      (400 * 1000)

/* Draw buffer: 170 * 32 lines * 2 bytes/pixel */
#define DRAW_BUF_LINES    32
#define DRAW_BUF_SIZE     (LCD_H_RES * DRAW_BUF_LINES * sizeof(uint16_t))

/* ---------- Globals for UI callbacks ---------- */
static lv_obj_t *g_label_hello  = NULL;
static lv_obj_t *g_label_thanks = NULL;
static lv_obj_t *g_btn_ok       = NULL;

/* ---------- LCD init ---------- */
static esp_lcd_panel_handle_t lcd_init(esp_lcd_panel_io_handle_t *io_handle_out)
{
    ESP_LOGI(TAG, "Init SPI bus");
    spi_bus_config_t buscfg = {
        .mosi_io_num   = LCD_MOSI_GPIO,
        .miso_io_num   = -1,
        .sclk_io_num   = LCD_CLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DRAW_BUF_SIZE,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Init LCD panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num       = LCD_DC_GPIO,
        .cs_gpio_num       = LCD_CS_GPIO,
        .pclk_hz           = LCD_SPI_CLK_HZ,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .spi_mode          = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io_handle));

    ESP_LOGI(TAG, "Init ST7789 panel");
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST_GPIO,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_cfg, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, LCD_X_GAP, LCD_Y_GAP));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    /* Backlight on */
    gpio_set_direction(LCD_BL_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_BL_GPIO, 0);

    *io_handle_out = io_handle;
    return panel_handle;
}

/* ---------- Touch init ---------- */
static esp_lcd_touch_handle_t touch_init(void)
{
    ESP_LOGI(TAG, "Init I2C master bus");
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port      = TOUCH_I2C_PORT,
        .sda_io_num    = TOUCH_SDA_GPIO,
        .scl_io_num    = TOUCH_SCL_GPIO,
        .clk_source    = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus_handle));

    ESP_LOGI(TAG, "Init CST816S touch");
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = {
        .dev_addr         = ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS,
        .scl_speed_hz     = TOUCH_I2C_HZ,
        .control_phase_bytes = 1,
        .dc_bit_offset    = 0,
        .lcd_cmd_bits     = 8,
        .lcd_param_bits   = 8,
        .flags.disable_control_phase = true,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bus_handle, &tp_io_cfg, &tp_io_handle));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max        = LCD_H_RES,
        .y_max        = LCD_V_RES,
        .rst_gpio_num = TOUCH_RST_GPIO,
        .int_gpio_num = TOUCH_INT_GPIO,
        .levels.reset = 0,
        .levels.interrupt = 0,
        .flags.swap_xy  = false,
        .flags.mirror_x = false,
        .flags.mirror_y = false,
    };
    esp_lcd_touch_handle_t tp_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, &tp_handle));

    return tp_handle;
}

/* ---------- UI callbacks ---------- */
static void revert_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    lv_obj_remove_flag(g_label_hello, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_btn_ok,      LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_label_thanks,   LV_OBJ_FLAG_HIDDEN);
}

static void btn_ok_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        lv_obj_add_flag(g_label_hello,    LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_btn_ok,         LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(g_label_thanks, LV_OBJ_FLAG_HIDDEN);

        lv_timer_t *t = lv_timer_create(revert_timer_cb, 1500, NULL);
        lv_timer_set_repeat_count(t, 1);
    }
}

/* ---------- UI create ---------- */
static void ui_create(lv_display_t *disp)
{
    lv_obj_t *scr = lv_display_get_screen_active(disp);

    /* Background colour */
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    /* "Hello World" label */
    g_label_hello = lv_label_create(scr);
    lv_label_set_text(g_label_hello, "Hello World");
    lv_obj_set_style_text_color(g_label_hello, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(g_label_hello, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(g_label_hello, LV_ALIGN_CENTER, 0, -40);

    /* "THANK YOU" label (hidden initially) */
    g_label_thanks = lv_label_create(scr);
    lv_label_set_text(g_label_thanks, "THANK YOU");
    lv_obj_set_style_text_color(g_label_thanks, lv_color_hex(0x00ffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(g_label_thanks, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(g_label_thanks, LV_ALIGN_CENTER, 0, -40);
    lv_obj_add_flag(g_label_thanks, LV_OBJ_FLAG_HIDDEN);

    /* OK button */
    g_btn_ok = lv_button_create(scr);
    lv_obj_set_size(g_btn_ok, 80, 40);
    lv_obj_align(g_btn_ok, LV_ALIGN_CENTER, 0, 40);
    lv_obj_add_event_cb(g_btn_ok, btn_ok_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_label = lv_label_create(g_btn_ok);
    lv_label_set_text(btn_label, "OK");
    lv_obj_center(btn_label);
}

/* ---------- app_main ---------- */
void app_main(void)
{
    ESP_LOGI(TAG, "Starting Hello World UI");

    /* 1. LCD */
    esp_lcd_panel_io_handle_t lcd_io = NULL;
    esp_lcd_panel_handle_t panel = lcd_init(&lcd_io);

    /* 2. Touch */
    esp_lcd_touch_handle_t touch = touch_init();

    /* 3. LVGL port init */
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    /* 4. Register display */
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle     = lcd_io,
        .panel_handle  = panel,
        .buffer_size   = DRAW_BUF_SIZE / sizeof(uint16_t),
        .double_buffer = false,
        .hres          = LCD_H_RES,
        .vres          = LCD_V_RES,
        .monochrome    = false,
        .color_format  = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy  = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma    = true,
            .swap_bytes  = true,
        },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);

    /* 5. Register touch */
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp        = disp,
        .handle      = touch,
    };
    lvgl_port_add_touch(&touch_cfg);

    /* 6. Create UI (must hold LVGL lock) */
    lvgl_port_lock(0);
    ui_create(disp);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "UI ready");
    /* app_main may return; LVGL port task keeps running */
}
