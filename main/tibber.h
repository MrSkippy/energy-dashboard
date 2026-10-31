// Fetches electricity prices from the Tibber API in a background task.
//
// Thread-safety: no LVGL here; the UI polls tibber_prices_generation() and
// tibber_get_status() from an lv_timer.

#ifndef TIBBER_H
#define TIBBER_H

#include <stdint.h>
#include <time.h>
#include "price.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TIBBER_STATE_WAIT_WIFI,
    TIBBER_STATE_NO_TOKEN,
    TIBBER_STATE_FETCHING,
    TIBBER_STATE_OK,
    TIBBER_STATE_TOKEN_INVALID,  // no retries until a new token is saved
    TIBBER_STATE_NO_CONNECTION,  // network, 429 or 5xx; retried with backoff
    TIBBER_STATE_ERROR,          // API returned an error; retried with backoff
} tibber_state_t;

typedef struct {
    tibber_state_t state;
    time_t last_update;  // last successful fetch, 0 = never
} tibber_status_t;

// Starts the fetch task: about hourly while WiFi is connected and a token is
// saved, right away when the token changes. Requires settings_init() and
// wifi_manager_start() first.
void tibber_start(void);

// Changes whenever new prices arrive; 0 = no prices yet.
uint32_t tibber_prices_generation(void);

// Copies the latest prices; returns their generation (0 = none, out untouched).
uint32_t tibber_get_prices(price_table_t *out);

void tibber_get_status(tibber_status_t *out);

#ifdef __cplusplus
}
#endif

#endif
