// Electricity prices from a Tibber API response, and the short-term trend.
// No LVGL: the UI side is in home_price.c.

#ifndef PRICE_H
#define PRICE_H

#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// Matches the Price_Trend variants in the SquareLine project.
typedef enum {
    PRICE_TREND_DOWN_HARD = -2,
    PRICE_TREND_DOWN = -1,
    PRICE_TREND_EQUAL = 0,
    PRICE_TREND_UP = 1,
    PRICE_TREND_UP_HARD = 2,
} price_trend_t;

// Quarters (15 min) looked ahead for the trend: 8 = 2 hours.
enum { PRICE_LOOKAHEAD = 8 };

typedef struct {
    double current;                 // EUR/kWh incl. tax ("total")
    double next[PRICE_LOOKAHEAD];   // totals of the quarters after "current"
    int next_count;                 // < PRICE_LOOKAHEAD near the end of the table
} price_info_t;

// Today + tomorrow, in time order. 200: a DST day has 100 quarters.
enum { PRICE_MAX_QUARTERS = 200 };

typedef struct {
    time_t start;   // UTC
    double total;   // EUR/kWh incl. tax
} price_quarter_t;

typedef struct {
    price_quarter_t q[PRICE_MAX_QUARTERS];
    int count;
} price_table_t;

// Parses a Tibber GraphQL response: priceInfo.today/tomorrow of the first home
// in data.viewer.homes that has a subscription. json must be NUL-terminated.
bool price_parse_tibber(const char *json, price_table_t *out);

// The quarter containing `now` and the ones after it. False when `now` is
// outside the table (clock not synced yet, or the data is too old).
bool price_at(const price_table_t *t, time_t now, price_info_t *out);

// Average of the next quarters vs the current price: within +-2 % = equal,
// beyond +-10 % = hard up/down (bands in price.c).
price_trend_t price_trend(const price_info_t *p);

double price_next_average(const price_info_t *p);

#ifdef __cplusplus
}
#endif

#endif
