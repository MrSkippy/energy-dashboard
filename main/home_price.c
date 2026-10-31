// Shows the current electricity price and trend on the Home screen.
//
// Data: the price table from tibber.c. Redrawn when new prices arrive and at
// every quarter boundary; "--" until there are prices and the clock is synced.
//
// The widgets come from the SquareLine Vision export (main/GUI/); this file is
// hand-written so a re-export does not overwrite it.

#include "home_price.h"
#include "price.h"
#include "tibber.h"
#include "GUI.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "esp_log.h"

#define TAG "home_price"

#define UPDATE_MS 1000
#define QUARTER_S (15 * 60)

static price_table_t s_table;  // copy of tibber's; static: ~3 KB
static uint32_t s_generation;
static int64_t s_quarter = -1;

// EUR/kWh -> cents with 1 decimal and a comma: 1.4355 -> "143,6", -0.1234 -> "-12,3".
// Integer formatting: no %f (picolibc) and no locale needed.
static void format_cents(double eur, char *buf, size_t len)
{
    long tenths = lround(eur * 1000.0);
    snprintf(buf, len, "%s%ld,%ld", tenths < 0 ? "-" : "", labs(tenths) / 10, labs(tenths) % 10);
}

static const char *trend_name(price_trend_t t)
{
    switch (t) {
    case PRICE_TREND_UP_HARD:   return "UP_HARD";
    case PRICE_TREND_UP:        return "UP";
    case PRICE_TREND_DOWN:      return "DOWN";
    case PRICE_TREND_DOWN_HARD: return "DOWN_HARD";
    default:                    return "EQUAL";
    }
}

// Each Price_Trend variant is exported as its own component instance in
// Price_Trend_Container; show the one matching the trend, hide the others.
// show < 0 hides all of them.
static void show_trend_icon(int show)
{
    lv_obj_t *const variants[] = {
        ui_Home_Price_Trend_Down_High,  // PRICE_TREND_DOWN_HARD
        ui_Home_Price_Trend_Down,       // PRICE_TREND_DOWN
        ui_Home_Price_Trend_Equal,      // PRICE_TREND_EQUAL
        ui_Home_Price_Trend_Up,         // PRICE_TREND_UP
        ui_Home_Price_Trend_Up_High,    // PRICE_TREND_UP_HARD
    };
    for (int i = 0; i < (int)(sizeof(variants) / sizeof(variants[0])); i++) {
        if (i == show) {
            lv_obj_remove_flag(variants[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(variants[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void home_price_show(const price_info_t *p)
{
    char price[16];
    format_cents(p->current, price, sizeof(price));
    lv_label_set_text(GUI_Label__Home__Price, price);

    price_trend_t trend = price_trend(p);
    show_trend_icon((int)trend - (int)PRICE_TREND_DOWN_HARD);

    char avg[16];
    format_cents(price_next_average(p), avg, sizeof(avg));
    ESP_LOGI(TAG, "price %s ct, next %d quarters avg %s ct -> trend %s",
             price, p->next_count, avg, trend_name(trend));
}

static void show_no_price(void)
{
    lv_label_set_text(GUI_Label__Home__Price, "--");
    show_trend_icon(-1);
}

static void update_cb(lv_timer_t *timer)
{
    time_t now = time(NULL);
    int64_t quarter = (int64_t)now / QUARTER_S;
    uint32_t gen = tibber_prices_generation();
    if (gen == s_generation && quarter == s_quarter) {
        return;
    }
    if (gen != s_generation) {
        s_generation = tibber_get_prices(&s_table);
    }
    s_quarter = quarter;

    price_info_t info;
    if (s_generation == 0 || !price_at(&s_table, now, &info)) {
        show_no_price();
        return;
    }
    home_price_show(&info);
}

void home_price_start(void)
{
    show_no_price();
    lv_timer_create(update_cb, UPDATE_MS, NULL);
}
