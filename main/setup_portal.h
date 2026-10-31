// Setup web page on the setup hotspot (captive portal): WiFi + Tibber token.
// Started/stopped by wifi_manager.c.

#ifndef SETUP_PORTAL_H
#define SETUP_PORTAL_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t setup_portal_start(void);
void setup_portal_stop(void);

#ifdef __cplusplus
}
#endif

#endif
