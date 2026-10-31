// Device settings in NVS: WiFi credentials and the Tibber API token.
// Never log the password or token themselves.

#include "settings_store.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define TAG "settings"
#define NVS_NAMESPACE "cfg"

esp_err_t settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS layout outdated, erasing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static void load_str(nvs_handle_t h, const char *key, char *buf, size_t len)
{
    buf[0] = '\0';
    if (nvs_get_str(h, key, buf, &len) != ESP_OK) {
        buf[0] = '\0';
    }
}

void settings_load(settings_t *out)
{
    memset(out, 0, sizeof(*out));
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return;  // namespace doesn't exist until the first save
    }
    load_str(h, "wifi_ssid", out->wifi_ssid, sizeof(out->wifi_ssid));
    load_str(h, "wifi_pass", out->wifi_pass, sizeof(out->wifi_pass));
    load_str(h, "tibber_token", out->tibber_token, sizeof(out->tibber_token));
    nvs_close(h);
}

bool settings_has_wifi(void)
{
    settings_t s;
    settings_load(&s);
    return s.wifi_ssid[0] != '\0';
}

static esp_err_t save_strs(const char *const *keys, const char *const *values, int n)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    for (int i = 0; i < n && err == ESP_OK; i++) {
        err = nvs_set_str(h, keys[i], values[i]);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t settings_save_wifi(const char *ssid, const char *pass)
{
    const char *keys[] = {"wifi_ssid", "wifi_pass"};
    const char *values[] = {ssid, pass};
    esp_err_t err = save_strs(keys, values, 2);
    ESP_LOGI(TAG, "Saved WiFi '%s': %s", ssid, esp_err_to_name(err));
    return err;
}

esp_err_t settings_save_token(const char *token)
{
    const char *keys[] = {"tibber_token"};
    const char *values[] = {token};
    esp_err_t err = save_strs(keys, values, 1);
    ESP_LOGI(TAG, "Saved Tibber token (%u chars): %s", (unsigned)strlen(token), esp_err_to_name(err));
    return err;
}

esp_err_t settings_clear_wifi(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    nvs_erase_key(h, "wifi_ssid");
    nvs_erase_key(h, "wifi_pass");
    err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Cleared WiFi credentials");
    return err;
}
