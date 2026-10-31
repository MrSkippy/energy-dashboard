// CrowPanel 1.46" HMI ESP32 Rotary Display (Elecrow) — pure ESP-IDF bring-up.
//
// Board: ESP32-S3R8 (8 MB octal PSRAM, 16 MB flash)
// Display: 1.46" round IPS LCD, 360x360, ST77916 controller over standard SPI
//          (the vendor product page calls it "JD9855", but the shipping firmware
//           drives it as ST77916/ST77961 — that is what we target here).
// Touch:   CST816T capacitive touch, I2C @ 0x15.
//
// LVGL is initialized and bound to the panel: the flush callback pushes pixels
// with esp_lcd_panel_draw_bitmap(), and the panel's on_color_trans_done event
// calls lv_display_flush_ready().

#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_st77916.h"

#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst816s.h"

#include "lvgl.h"
#include "GUI.h"
#include "background_rotator.h"
#include "knob.h"
#include "home_clock.h"
#include "home_price.h"
#include "settings_store.h"
#include "wifi_manager.h"
#include "screen_setup.h"
#include "screen_settings.h"
#include "tibber.h"

#define TAG "CrowPanel146"

// ---- Display geometry ----
static const uint16_t LCD_H_RES = 360;
static const uint16_t LCD_V_RES = 360;

// ---- Display SPI pins (from Elecrow RotaryScreen_1_46.h) ----
#define LCD_SPI_HOST    SPI2_HOST
#define PIN_LCD_SCLK    GPIO_NUM_10
#define PIN_LCD_MOSI    GPIO_NUM_11
#define PIN_LCD_CS      GPIO_NUM_9
#define PIN_LCD_DC      GPIO_NUM_3
#define PIN_LCD_RST     GPIO_NUM_14
#define PIN_LCD_BL      GPIO_NUM_46
#define LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)

// Board power-enable: GPIO1 and GPIO2 must BOTH be driven high to power the
// display module (panel + backlight rail). Without them the whole LCD is dead
// (per the Elecrow RotaryScreen_1_46 reference: "Pin 1 and pin 2 need to be
// turned on simultaneously in order to turn on the screen's power").
#define PIN_LCD_POWER1  GPIO_NUM_1
#define PIN_LCD_POWER2  GPIO_NUM_2

// ---- Touch I2C pins (CST816T) ----
#define TOUCH_I2C_PORT  I2C_NUM_0
#define PIN_TOUCH_SDA   GPIO_NUM_6
#define PIN_TOUCH_SCL   GPIO_NUM_7
#define PIN_TOUCH_RST   GPIO_NUM_13
#define PIN_TOUCH_INT   GPIO_NUM_5

// ---- LVGL partial draw buffers (DMA-capable internal RAM) ----
enum { LVGL_BUFFER_LINES = 80 };
enum { LVGL_BUFFER_PIXELS = LCD_H_RES * LVGL_BUFFER_LINES };

static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_touch_handle_t s_touch = NULL;
static lv_color_t *s_buf1 = NULL;
static lv_color_t *s_buf2 = NULL;

// JD9853 / ST77961-family init sequence for the Elecrow CrowPanel 1.46" round
// panel, transcribed from the vendor firmware's LovyanGFX panel definition
// (Elecrow-RD/CrowPanel-1.46inch-HMI-ESP32-Rotary-Display, LovyanGFX
// Panel_ST77961.hpp -> getInitCommands list0 — the exact list Lovyan runs for
// this board). The shipping panel is a JD9853/ST77961, NOT the Waveshare-style
// ST77916 that the esp_lcd_st77916 driver's built-in default targets: it has a
// different register map, a different pixel-format encoding (COLMOD 0x3A=0x05
// for RGB565, NOT 0x55), and must run with inversion OFF (the vendor sets
// cfg.invert=false). The previous ST77916 sequence + the driver's pre-sent
// COLMOD 0x55 left the panel mis-initialized -> the whole UI rendered as a
// uniform dark navy. Feeding this generic command sender the real ST77961 list
// brings the panel up correctly.
//
// NOTE: panel_st77916_init() pre-sends MADCTL(0x36)=0x00 and COLMOD(0x3A)=0x55
// (from bits_per_pixel=16) before this list runs; the explicit 0x3A=0x05 below
// overrides the wrong-for-this-panel 0x55, and we never send INVON (0x21).
static const st77916_lcd_init_cmd_t vendor_init_cmds[] = {
    {0xDE, (uint8_t []){0x00}, 1, 0},
    {0xDF, (uint8_t []){0x98, 0x55}, 2, 0},
    {0xB2, (uint8_t []){0x1F}, 1, 0},
    {0xB7, (uint8_t []){0x00, 0x1D, 0x00, 0x45}, 4, 0},
    {0xBB, (uint8_t []){0x1B, 0x64, 0xC4, 0x1E, 0x3E, 0xF5}, 6, 0},
    {0xBC, (uint8_t []){0x03, 0x20, 0xF3, 0xC0}, 4, 0},
    {0xC0, (uint8_t []){0x22, 0xA1}, 2, 0},
    {0xC3, (uint8_t []){0x00, 0x02, 0x2A, 0x0B, 0x08, 0x48, 0x08, 0x04, 0x62, 0x30, 0x30}, 11, 0},
    {0xC4, (uint8_t []){0x40, 0x00, 0xAD, 0x68, 0x43, 0x07, 0x04, 0x16, 0x43, 0x07, 0x04}, 11, 0},
    {0xC8, (uint8_t []){0x3F, 0x36, 0x30, 0x2D, 0x2F, 0x31, 0x2B, 0x2A, 0x27, 0x25, 0x1F, 0x14, 0x10, 0x09, 0x04, 0x00, 0x3F, 0x36, 0x30, 0x2D, 0x2F, 0x31, 0x2B, 0x2A, 0x27, 0x25, 0x1F, 0x14, 0x10, 0x09, 0x04, 0x00}, 32, 0},
    {0xD3, (uint8_t []){0x28, 0x13}, 2, 0},
    {0xD9, (uint8_t []){0x00, 0x00, 0xFF, 0x00, 0xF0, 0x00}, 6, 0},
    {0xDE, (uint8_t []){0x01}, 1, 0},
    {0xB7, (uint8_t []){0x13, 0xE7, 0x64, 0x39, 0x06, 0x36, 0x19, 0x1C}, 8, 0},
    {0xBE, (uint8_t []){0x00}, 1, 0},
    {0xC1, (uint8_t []){0x00, 0x4A, 0x80}, 3, 0},
    {0xC2, (uint8_t []){0x00, 0x16, 0xDA, 0xE7}, 4, 0},
    {0xC7, (uint8_t []){0x00, 0x00, 0x00, 0x38, 0x08, 0x08, 0x00, 0x01}, 8, 0},
    {0xC8, (uint8_t []){0x00, 0x00, 0x00, 0x00, 0x15, 0x3B}, 6, 0},
    {0xC9, (uint8_t []){0x00, 0x16, 0x06, 0x04, 0x0A}, 5, 0},
    {0xCA, (uint8_t []){0x08, 0x35, 0x16, 0x1F, 0x1F}, 5, 0},
    {0xCB, (uint8_t []){0x01, 0x16, 0x07, 0x05, 0x0B}, 5, 0},
    {0xCC, (uint8_t []){0x09, 0x35, 0x16, 0x1F, 0x1F}, 5, 0},
    {0xCD, (uint8_t []){0x01, 0x16, 0x09, 0x0B, 0x05}, 5, 0},
    {0xCE, (uint8_t []){0x07, 0x15, 0x1F, 0x16, 0x1F}, 5, 0},
    {0xCF, (uint8_t []){0x00, 0x16, 0x08, 0x0A, 0x04}, 5, 0},
    {0xD0, (uint8_t []){0x06, 0x15, 0x1F, 0x16, 0x1F}, 5, 0},
    {0xD1, (uint8_t []){0x02, 0x30}, 2, 0},
    {0xD2, (uint8_t []){0x02, 0x03, 0x52, 0xDF, 0xDD}, 5, 0},
    {0xD3, (uint8_t []){0x3B, 0x04, 0x48}, 3, 0},
    {0xD5, (uint8_t []){0x10, 0x10, 0x07, 0x07, 0x0F, 0x94, 0x26}, 7, 0},
    {0xD6, (uint8_t []){0x00, 0x00, 0x40}, 3, 0},
    {0xD7, (uint8_t []){0x00, 0x00, 0x20}, 3, 0},
    {0xDE, (uint8_t []){0x02}, 1, 0},
    {0xB6, (uint8_t []){0x1C}, 1, 0},
    {0xDE, (uint8_t []){0x00}, 1, 0},
    {0x4D, (uint8_t []){0x00}, 1, 0},
    {0x4E, (uint8_t []){0x00}, 1, 0},
    {0x4F, (uint8_t []){0x00}, 1, 0},
    {0x4C, (uint8_t []){0x01}, 1, 10},
    {0x4C, (uint8_t []){0x00}, 1, 0},
    {0x35, (uint8_t []){0x00}, 0, 0},   // TEON (no data)
    {0x3A, (uint8_t []){0x05}, 1, 0},   // COLMOD = 16bpp RGB565 (JD9853 encoding)
    {0x2A, (uint8_t []){0x00, 0x00, 0x01, 0x67}, 4, 0},  // CASET 0..359
    {0x2B, (uint8_t []){0x00, 0x00, 0x01, 0x67}, 4, 0},  // RASET 0..359
    {0x20, (uint8_t []){0x00}, 0, 0},   // INVOFF (vendor runs with inversion off)
    {0x11, (uint8_t []){0x00}, 0, 120}, // SLPOUT (no data, 120 ms)
    {0x29, (uint8_t []){0x00}, 0, 0},   // DISPON (no data)
};

static uint32_t tick_get_cb(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// Panel signals "transfer done" -> tell LVGL the flush is complete.
static bool on_color_trans_done(esp_lcd_panel_io_handle_t io,
                                esp_lcd_panel_io_event_data_t *edata,
                                void *user_ctx)
{
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

// LVGL wants to push a rendered area to the panel.
static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    // LVGL renders RGB565 in native byte order; the panel expects big-endian.
    lv_draw_sw_rgb565_swap(px_map, lv_area_get_width(area) * lv_area_get_height(area));
    esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, px_map);
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint16_t x = 0, y = 0, strength = 0;
    uint8_t cnt = 0;

    esp_lcd_touch_read_data(s_touch);
    bool pressed = esp_lcd_touch_get_coordinates(s_touch, &x, &y, &strength, &cnt, 1);

    if (pressed && cnt > 0) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// Enable the display module's power rail (GPIO1 + GPIO2 high). Must run before
// the panel is reset/initialized, otherwise the LCD has no power and the SPI
// init silently goes nowhere (panel + backlight stay dark).
static void power_screen_on(void)
{
    gpio_config_t pwr = {};
    pwr.pin_bit_mask = (1ULL << PIN_LCD_POWER1) | (1ULL << PIN_LCD_POWER2);
    pwr.mode = GPIO_MODE_OUTPUT;
    gpio_config(&pwr);
    gpio_set_level((gpio_num_t)PIN_LCD_POWER1, 1);
    gpio_set_level((gpio_num_t)PIN_LCD_POWER2, 1);
    vTaskDelay(pdMS_TO_TICKS(20));  // let the rail settle before panel reset
}

// The backlight LED driver expects a PWM signal on GPIO46 (a static level does
// not light it) — drive it via LEDC, matching the Elecrow reference (5 kHz,
// 8-bit). Run near full brightness.
static void backlight_on(void)
{
    ledc_timer_config_t lt = {};
    lt.speed_mode = LEDC_LOW_SPEED_MODE;
    lt.duty_resolution = LEDC_TIMER_8_BIT;
    lt.timer_num = LEDC_TIMER_0;
    lt.freq_hz = 5000;
    lt.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer_config(&lt);

    ledc_channel_config_t lc = {};
    lc.gpio_num = PIN_LCD_BL;
    lc.speed_mode = LEDC_LOW_SPEED_MODE;
    lc.channel = LEDC_CHANNEL_0;
    lc.timer_sel = LEDC_TIMER_0;
    lc.intr_type = LEDC_INTR_DISABLE;
    lc.duty = 230;   // ~90 % of 255
    lc.hpoint = 0;
    ledc_channel_config(&lc);
}

static esp_err_t display_init(lv_display_t *disp)
{
    // SPI bus. Fill the struct field-by-field rather than via the driver's
    // C config macro: that macro uses C designated initializers whose order
    // does not satisfy C++'s stricter aggregate-init rules.
    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num = PIN_LCD_SCLK;
    buscfg.mosi_io_num = PIN_LCD_MOSI;
    buscfg.miso_io_num = -1;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = LCD_H_RES * LVGL_BUFFER_LINES * (int)sizeof(uint16_t);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO),
                        TAG, "spi_bus_initialize failed");

    // Panel IO (standard SPI, dedicated DC line on GPIO3)
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = PIN_LCD_CS;
    io_config.dc_gpio_num = PIN_LCD_DC;
    io_config.spi_mode = 0;
    io_config.pclk_hz = LCD_PIXEL_CLOCK_HZ;
    io_config.trans_queue_depth = 10;
    io_config.on_color_trans_done = on_color_trans_done;
    io_config.user_ctx = disp;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST,
                                                 &io_config, &io),
                        TAG, "new_panel_io_spi failed");

    // ST77916 panel with the board-specific (LovyanGFX-derived) init sequence
    st77916_vendor_config_t vendor_config = {};
    vendor_config.init_cmds = vendor_init_cmds;
    vendor_config.init_cmds_size = sizeof(vendor_init_cmds) / sizeof(vendor_init_cmds[0]);
    vendor_config.flags.use_qspi_interface = 0;

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = PIN_LCD_RST;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;
    panel_config.vendor_config = &vendor_config;

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st77916(io, &panel_config, &s_panel),
                        TAG, "new_panel_st77916 failed");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel_reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel_init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "disp_on failed");

    return ESP_OK;
}

// Touch is optional: any failure logs a warning and returns false so the display
// still comes up. The LVGL input device is only registered when this succeeds.
static bool touch_init(void)
{
    i2c_master_bus_config_t i2c_cfg = {};
    i2c_cfg.i2c_port = TOUCH_I2C_PORT;
    i2c_cfg.sda_io_num = (gpio_num_t)PIN_TOUCH_SDA;
    i2c_cfg.scl_io_num = (gpio_num_t)PIN_TOUCH_SCL;
    i2c_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_cfg.glitch_ignore_cnt = 7;
    i2c_cfg.flags.enable_internal_pullup = true;

    i2c_master_bus_handle_t i2c_bus = NULL;
    esp_err_t err = i2c_new_master_bus(&i2c_cfg, &i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Touch I2C bus init failed (%s); continuing without touch",
                 esp_err_to_name(err));
        return false;
    }

    // CST816 I2C panel-IO config, filled manually (the driver's _CONFIG() macro
    // relies on C designated-initializer ordering that C++ rejects).
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = {};
    tp_io_cfg.dev_addr = ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS;  // 0x15
    tp_io_cfg.scl_speed_hz = 100000;
    tp_io_cfg.control_phase_bytes = 1;
    tp_io_cfg.dc_bit_offset = 0;
    tp_io_cfg.lcd_cmd_bits = 8;
    tp_io_cfg.lcd_param_bits = 0;
    tp_io_cfg.flags.disable_control_phase = 1;
    // The new-I2C-driver variant must be called explicitly: the generic
    // esp_lcd_new_panel_io_i2c() macro dispatches via C11 _Generic, unavailable in C++.
    // err = esp_lcd_new_panel_io_i2c_v2(i2c_bus, &tp_io_cfg, &tp_io); // had error
    err = esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_cfg, &tp_io);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Touch panel IO init failed (%s); continuing without touch",
                 esp_err_to_name(err));
        return false;
    }

    esp_lcd_touch_config_t tp_cfg = {};
    tp_cfg.x_max = LCD_H_RES;
    tp_cfg.y_max = LCD_V_RES;
    tp_cfg.rst_gpio_num = (gpio_num_t)PIN_TOUCH_RST;
    tp_cfg.int_gpio_num = (gpio_num_t)PIN_TOUCH_INT;
    tp_cfg.levels.reset = 0;
    tp_cfg.levels.interrupt = 0;

    err = esp_lcd_touch_new_i2c_cst816s(tp_io, &tp_cfg, &s_touch);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "CST816 touch init failed (%s); continuing without touch",
                 esp_err_to_name(err));
        s_touch = NULL;
        return false;
    }

    ESP_LOGI(TAG, "CST816T touch initialized");
    return true;
}

extern "C" void app_main(void)
{
    lv_init();
    lv_tick_set_cb(tick_get_cb);

    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);

    power_screen_on();  // GPIO1+GPIO2: power the display module before any panel I/O

    if (display_init(disp) != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed");
        return;
    }
    backlight_on();

    // DMA-capable internal RAM for the partial draw buffers.
    s_buf1 = (lv_color_t *)heap_caps_malloc(LVGL_BUFFER_PIXELS * sizeof(uint16_t),
                                            MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    s_buf2 = (lv_color_t *)heap_caps_malloc(LVGL_BUFFER_PIXELS * sizeof(uint16_t),
                                            MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_buf1 || !s_buf2) {
        ESP_LOGE(TAG, "LVGL draw buffer alloc failed");
        return;
    }

    lv_display_set_buffers(disp, s_buf1, s_buf2,
                           LVGL_BUFFER_PIXELS * sizeof(uint16_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, disp_flush_cb);

    if (touch_init()) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, touch_read_cb);
        lv_indev_set_display(indev, disp);
    }

    GUI_init();
    knob_init();
    background_rotator_start();
    home_clock_start();
    home_price_start();
    screen_settings_start();
    screen_setup_start();

    ESP_ERROR_CHECK(settings_init());
    wifi_manager_start();
    tibber_start();

    ESP_LOGI(TAG, "Initialization done.");

    // With CONFIG_FREERTOS_HZ=100 a tick is 10 ms, so pdMS_TO_TICKS(<10) is 0 and
    // vTaskDelay(0) never blocks -> IDLE0 starves and the task watchdog fires.
    // Sleep until LVGL's next timer is due, but always for at least one tick.
    while (1) {
        uint32_t wait_ms = lv_timer_handler();
        TickType_t wait_ticks = pdMS_TO_TICKS(wait_ms);
        if (wait_ticks < 1) wait_ticks = 1;
        if (wait_ticks > pdMS_TO_TICKS(100)) wait_ticks = pdMS_TO_TICKS(100);
        vTaskDelay(wait_ticks);
    }
}
