// Device settings in NVS: WiFi credentials and the Tibber API token.

#ifndef SETTINGS_STORE_H
#define SETTINGS_STORE_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Sizes include the NUL terminator. WiFi limits: SSID 32, WPA2 passphrase 63.
#define SETTINGS_SSID_LEN  33
#define SETTINGS_PASS_LEN  65
#define SETTINGS_TOKEN_LEN 128

typedef struct {
    char wifi_ssid[SETTINGS_SSID_LEN];
    char wifi_pass[SETTINGS_PASS_LEN];
    char tibber_token[SETTINGS_TOKEN_LEN];
} settings_t;

// Initializes NVS flash (erases it when its layout is outdated). Call once at boot.
esp_err_t settings_init(void);

// Missing keys are returned as empty strings.
void settings_load(settings_t *out);

bool settings_has_wifi(void);

esp_err_t settings_save_wifi(const char *ssid, const char *pass);
esp_err_t settings_save_token(const char *token);
esp_err_t settings_clear_wifi(void);

#ifdef __cplusplus
}
#endif

#endif
