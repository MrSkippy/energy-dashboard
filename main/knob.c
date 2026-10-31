// Rotary encoder knob (CrowPanel 1.46"), decoded in hardware by the PCNT unit.
//
// Pins from the Elecrow reference (RotaryScreen_1_46.h): encoder A = GPIO45,
// B = GPIO42, push button = GPIO41 (active low). Both channels count every edge
// of A and B (x4 decoding), so one detent = one full quadrature cycle = 4 counts.

#include "knob.h"

#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"

#define TAG "knob"

#define KNOB_PIN_A           GPIO_NUM_45
#define KNOB_PIN_B           GPIO_NUM_42
#define KNOB_PIN_BUTTON      GPIO_NUM_41
#define KNOB_COUNTS_PER_STEP 2    // tune on hardware if one detent != one step
#define KNOB_COUNT_LIMIT     1000

static pcnt_unit_handle_t s_unit = NULL;
static int s_last = 0;

static esp_err_t knob_setup(void)
{
    pcnt_unit_config_t unit_cfg = {0};
    unit_cfg.low_limit = -KNOB_COUNT_LIMIT;
    unit_cfg.high_limit = KNOB_COUNT_LIMIT;
    unit_cfg.flags.accum_count = 1;  // keep counting past the limits instead of wrapping to 0
    ESP_RETURN_ON_ERROR(pcnt_new_unit(&unit_cfg, &s_unit), TAG, "new unit");

    pcnt_glitch_filter_config_t filter_cfg = {0};
    filter_cfg.max_glitch_ns = 1000;  // contact bounce
    ESP_RETURN_ON_ERROR(pcnt_unit_set_glitch_filter(s_unit, &filter_cfg), TAG, "glitch filter");

    pcnt_chan_config_t chan_a_cfg = {0};
    chan_a_cfg.edge_gpio_num = KNOB_PIN_A;
    chan_a_cfg.level_gpio_num = KNOB_PIN_B;
    pcnt_channel_handle_t chan_a = NULL;
    ESP_RETURN_ON_ERROR(pcnt_new_channel(s_unit, &chan_a_cfg, &chan_a), TAG, "channel A");

    pcnt_chan_config_t chan_b_cfg = {0};
    chan_b_cfg.edge_gpio_num = KNOB_PIN_B;
    chan_b_cfg.level_gpio_num = KNOB_PIN_A;
    pcnt_channel_handle_t chan_b = NULL;
    ESP_RETURN_ON_ERROR(pcnt_new_channel(s_unit, &chan_b_cfg, &chan_b), TAG, "channel B");

    ESP_RETURN_ON_ERROR(pcnt_channel_set_edge_action(chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                                     PCNT_CHANNEL_EDGE_ACTION_INCREASE), TAG, "edge A");
    ESP_RETURN_ON_ERROR(pcnt_channel_set_level_action(chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                                      PCNT_CHANNEL_LEVEL_ACTION_INVERSE), TAG, "level A");
    ESP_RETURN_ON_ERROR(pcnt_channel_set_edge_action(chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                                     PCNT_CHANNEL_EDGE_ACTION_DECREASE), TAG, "edge B");
    ESP_RETURN_ON_ERROR(pcnt_channel_set_level_action(chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                                      PCNT_CHANNEL_LEVEL_ACTION_INVERSE), TAG, "level B");

    // accum_count only works with watch points on the limits.
    ESP_RETURN_ON_ERROR(pcnt_unit_add_watch_point(s_unit, KNOB_COUNT_LIMIT), TAG, "watch high");
    ESP_RETURN_ON_ERROR(pcnt_unit_add_watch_point(s_unit, -KNOB_COUNT_LIMIT), TAG, "watch low");

    // The PCNT driver sets no pulls, and the board's schematic doesn't say whether
    // A/B have external pull-ups; the internal ones are harmless if they do.
    gpio_pullup_en(KNOB_PIN_A);
    gpio_pullup_en(KNOB_PIN_B);

    ESP_RETURN_ON_ERROR(pcnt_unit_enable(s_unit), TAG, "enable");
    ESP_RETURN_ON_ERROR(pcnt_unit_clear_count(s_unit), TAG, "clear");
    ESP_RETURN_ON_ERROR(pcnt_unit_start(s_unit), TAG, "start");
    return ESP_OK;
}

void knob_init(void)
{
    gpio_config_t btn_cfg = {
        .pin_bit_mask = 1ULL << KNOB_PIN_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    if (gpio_config(&btn_cfg) != ESP_OK) {
        ESP_LOGW(TAG, "Knob button init failed");
    }

    if (knob_setup() != ESP_OK) {
        ESP_LOGW(TAG, "Knob init failed; continuing without knob");
        s_unit = NULL;
        return;
    }
    ESP_LOGI(TAG, "Rotary knob initialized");
}

int32_t knob_take_steps(void)
{
    int count = 0;
    if (s_unit == NULL || pcnt_unit_get_count(s_unit, &count) != ESP_OK) {
        return 0;
    }
    // Keep the remainder of a half-turned detent for the next call.
    int32_t steps = (count - s_last) / KNOB_COUNTS_PER_STEP;
    s_last += steps * KNOB_COUNTS_PER_STEP;
    return steps;
}

bool knob_button_pressed(void)
{
    return gpio_get_level(KNOB_PIN_BUTTON) == 0;
}
