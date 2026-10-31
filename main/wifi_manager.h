// WiFi connection + setup mode (hotspot with captive portal, see setup_portal.c).
//
// Thread-safety: WiFi events run in the event-loop task. Nothing here calls
// LVGL; the UI polls wifi_manager_get_state()/get_info() from an lv_timer.

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdint.h>
#include "esp_err.h"
#include "esp_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_STATE_IDLE,
    WIFI_STATE_CONNECTING,     // saved network, (re)connecting
    WIFI_STATE_CONNECTED,
    WIFI_STATE_SETUP,          // hotspot + setup page, waiting for the phone
    WIFI_STATE_SETUP_TESTING,  // setup page sent credentials, trying them
    WIFI_STATE_SETUP_FAILED,   // that failed; hotspot stays up for another try
    WIFI_STATE_SETUP_DONE,     // that worked; hotspot closes in a few seconds
} wifi_state_t;

typedef enum {
    WIFI_FAIL_NONE,
    WIFI_FAIL_WRONG_PASSWORD,
    WIFI_FAIL_NOT_FOUND,
    WIFI_FAIL_OTHER,
} wifi_fail_reason_t;

typedef struct {
    char ap_ssid[33];      // setup hotspot name, e.g. "EnergyDashboard-A1B2"
    char ap_pass[16];      // setup hotspot password (new each setup session)
    char ap_url[32];       // "http://192.168.4.1"
    char sta_ssid[33];     // network being used / tested
    char sta_ip[16];       // "" when not connected
    wifi_fail_reason_t fail_reason;
} wifi_info_t;

// Starts WiFi: connects to the saved network, or opens setup mode when there is
// none. Without a connection 60 s after boot, setup mode opens as well.
// Requires settings_init() first.
void wifi_manager_start(void);

wifi_state_t wifi_manager_get_state(void);
void wifi_manager_get_info(wifi_info_t *out);
const char *wifi_manager_state_name(wifi_state_t state);

// Opens setup mode (knob long-press, Settings screen). No-op when already open.
void wifi_manager_start_setup(void);

// Leaves setup mode and reconnects to the saved network. No-op without one.
void wifi_manager_cancel_setup(void);

// For the setup page. Credentials are only saved once they connect; an empty
// token keeps the stored one.
esp_err_t wifi_manager_try_credentials(const char *ssid, const char *pass, const char *token);

// Blocking scan (~2 s) for the setup page; only in setup mode.
esp_err_t wifi_manager_scan(wifi_ap_record_t *records, uint16_t *count);

#ifdef __cplusplus
}
#endif

#endif
