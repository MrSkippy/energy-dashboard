// WiFi connection + setup mode (hotspot with captive portal, see setup_portal.c).
//
// Setup mode runs AP+STA: the phone stays on our hotspot while the STA side
// tries the entered credentials, so the page can show the result. Credentials
// are only stored (settings_store) once they actually connect.

#include "wifi_manager.h"
#include "settings_store.h"
#include "setup_portal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#define TAG "wifi"

#define BOOT_CONNECT_TIMEOUT_US (60 * 1000 * 1000LL)  // no WiFi this long after boot -> setup
#define TEST_TIMEOUT_US         (20 * 1000 * 1000LL)  // setup page credentials
#define SETUP_DONE_DELAY_US     (5 * 1000 * 1000LL)   // keep hotspot up so the page shows "Verbonden"
#define SETUP_IDLE_US           (5 * 60 * 1000 * 1000LL)  // unused setup + saved WiFi -> back to it
#define RETRY_MIN_DELAY_MS      1000
#define RETRY_MAX_DELAY_MS      30000
#define TEST_MAX_ATTEMPTS       3
#define AP_MAX_CLIENTS          2
#define AP_PASS_LEN             8
#define NTP_SERVER              "pool.ntp.org"

static SemaphoreHandle_t s_lock;
static wifi_state_t s_state = WIFI_STATE_IDLE;
static wifi_info_t s_info;
static bool s_wifi_started = false;
static bool s_sntp_started = false;
static uint32_t s_retry_delay_ms = RETRY_MIN_DELAY_MS;
static int s_test_attempts = 0;

// Credentials from the setup page, saved only after they connect.
static char s_pending_ssid[SETTINGS_SSID_LEN];
static char s_pending_pass[SETTINGS_PASS_LEN];
static char s_pending_token[SETTINGS_TOKEN_LEN];

static esp_timer_handle_t s_retry_timer;
static esp_timer_handle_t s_boot_timer;
static esp_timer_handle_t s_test_timer;
static esp_timer_handle_t s_done_timer;
static esp_timer_handle_t s_idle_timer;

// The AP netif hands out this URL as DHCP option 114 (captive portal, RFC 8910);
// esp_netif keeps the pointer, so it must stay valid.
static const char s_portal_url[] = "http://192.168.4.1";

const char *wifi_manager_state_name(wifi_state_t state)
{
    switch (state) {
    case WIFI_STATE_CONNECTING:    return "connecting";
    case WIFI_STATE_CONNECTED:     return "connected";
    case WIFI_STATE_SETUP:         return "setup";
    case WIFI_STATE_SETUP_TESTING: return "testing";
    case WIFI_STATE_SETUP_FAILED:  return "failed";
    case WIFI_STATE_SETUP_DONE:    return "done";
    default:                       return "idle";
    }
}

static wifi_state_t get_state(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    wifi_state_t state = s_state;
    xSemaphoreGive(s_lock);
    return state;
}

static void set_state(wifi_state_t state)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    wifi_state_t old = s_state;
    s_state = state;
    xSemaphoreGive(s_lock);
    if (old != state) {
        ESP_LOGI(TAG, "State %s -> %s", wifi_manager_state_name(old), wifi_manager_state_name(state));
    }
}

static bool in_setup(wifi_state_t state)
{
    return state == WIFI_STATE_SETUP || state == WIFI_STATE_SETUP_TESTING ||
           state == WIFI_STATE_SETUP_FAILED || state == WIFI_STATE_SETUP_DONE;
}

static void set_sta_config(const char *ssid, const char *pass)
{
    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));

    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(s_info.sta_ssid, ssid, sizeof(s_info.sta_ssid));
    s_info.sta_ip[0] = '\0';
    xSemaphoreGive(s_lock);
}

static void start_wifi(void)
{
    if (!s_wifi_started) {
        ESP_ERROR_CHECK(esp_wifi_start());
        s_wifi_started = true;
    }
}

static void start_sntp(void)
{
    if (s_sntp_started) {
        return;
    }
    // Sets the system time; home_clock.c follows it automatically.
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(NTP_SERVER);
    if (esp_netif_sntp_init(&cfg) == ESP_OK) {
        s_sntp_started = true;
        ESP_LOGI(TAG, "SNTP started (%s)", NTP_SERVER);
    }
}

// ---- normal mode: saved network ----

static void schedule_retry(void)
{
    ESP_LOGI(TAG, "Reconnecting in %" PRIu32 " ms", s_retry_delay_ms);
    esp_timer_stop(s_retry_timer);
    esp_timer_start_once(s_retry_timer, (uint64_t)s_retry_delay_ms * 1000);
    s_retry_delay_ms = MIN(s_retry_delay_ms * 2, RETRY_MAX_DELAY_MS);
}

static void retry_timer_cb(void *arg)
{
    wifi_state_t state = get_state();
    if (state == WIFI_STATE_CONNECTING || state == WIFI_STATE_SETUP_TESTING) {
        esp_wifi_connect();
    }
}

static void boot_timer_cb(void *arg)
{
    if (get_state() == WIFI_STATE_CONNECTING) {
        ESP_LOGW(TAG, "No WiFi %d s after boot, opening setup", (int)(BOOT_CONNECT_TIMEOUT_US / 1000000));
        wifi_manager_start_setup();
    }
}

static void connect_saved(void)
{
    settings_t s;
    settings_load(&s);
    s_retry_delay_ms = RETRY_MIN_DELAY_MS;
    set_state(WIFI_STATE_CONNECTING);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    set_sta_config(s.wifi_ssid, s.wifi_pass);
    if (s_wifi_started) {
        esp_wifi_connect();
    } else {
        start_wifi();  // WIFI_EVENT_STA_START then connects
    }
    ESP_LOGI(TAG, "Connecting to '%s'", s.wifi_ssid);
}

// ---- setup mode: testing credentials from the setup page ----

static void test_failed(wifi_fail_reason_t reason)
{
    esp_timer_stop(s_test_timer);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_info.fail_reason = reason;
    xSemaphoreGive(s_lock);
    set_state(WIFI_STATE_SETUP_FAILED);  // before disconnect: its event is then ignored
    esp_wifi_disconnect();
}

static void test_timer_cb(void *arg)
{
    if (get_state() == WIFI_STATE_SETUP_TESTING) {
        ESP_LOGW(TAG, "Test connection timed out");
        test_failed(WIFI_FAIL_OTHER);
    }
}

// Setup opened (e.g. router was off at boot) but nobody uses it: go back to the
// saved network instead of staying in setup forever.
static void idle_timer_cb(void *arg)
{
    wifi_sta_list_t clients;
    if (esp_wifi_ap_get_sta_list(&clients) == ESP_OK && clients.num > 0) {
        esp_timer_start_once(s_idle_timer, SETUP_IDLE_US);  // phone connected: check again later
        return;
    }
    ESP_LOGI(TAG, "Setup unused, back to saved WiFi");
    wifi_manager_cancel_setup();
}

static void done_timer_cb(void *arg)
{
    setup_portal_stop();
    esp_wifi_set_mode(WIFI_MODE_STA);
    set_state(WIFI_STATE_CONNECTED);
}

static void test_succeeded(void)
{
    esp_timer_stop(s_test_timer);
    esp_timer_stop(s_idle_timer);
    settings_save_wifi(s_pending_ssid, s_pending_pass);
    if (s_pending_token[0] != '\0') {
        settings_save_token(s_pending_token);
    }
    set_state(WIFI_STATE_SETUP_DONE);
    esp_timer_start_once(s_done_timer, SETUP_DONE_DELAY_US);
}

static wifi_fail_reason_t fail_reason_from(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
        return WIFI_FAIL_WRONG_PASSWORD;
    case WIFI_REASON_NO_AP_FOUND:
        return WIFI_FAIL_NOT_FOUND;
    default:
        return WIFI_FAIL_OTHER;
    }
}

// ---- events ----

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (get_state() == WIFI_STATE_CONNECTING) {
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *e = data;
        wifi_state_t state = get_state();
        ESP_LOGI(TAG, "Disconnected (reason %d) in state %s", e->reason, wifi_manager_state_name(state));
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_info.sta_ip[0] = '\0';
        xSemaphoreGive(s_lock);

        if (state == WIFI_STATE_CONNECTED) {
            set_state(WIFI_STATE_CONNECTING);
            s_retry_delay_ms = RETRY_MIN_DELAY_MS;
            schedule_retry();
        } else if (state == WIFI_STATE_CONNECTING) {
            schedule_retry();
        } else if (state == WIFI_STATE_SETUP_TESTING) {
            if (++s_test_attempts < TEST_MAX_ATTEMPTS) {
                esp_timer_stop(s_retry_timer);
                esp_timer_start_once(s_retry_timer, RETRY_MIN_DELAY_MS * 1000);
            } else {
                test_failed(fail_reason_from(e->reason));
            }
        }
        // SETUP / SETUP_FAILED / SETUP_DONE: our own disconnects, ignore.
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        snprintf(s_info.sta_ip, sizeof(s_info.sta_ip), IPSTR, IP2STR(&e->ip_info.ip));
        ESP_LOGI(TAG, "Connected to '%s', IP %s", s_info.sta_ssid, s_info.sta_ip);
        xSemaphoreGive(s_lock);

        if (get_state() == WIFI_STATE_SETUP_TESTING) {
            test_succeeded();
        } else {
            esp_timer_stop(s_boot_timer);
            s_retry_delay_ms = RETRY_MIN_DELAY_MS;
            set_state(WIFI_STATE_CONNECTED);
        }
        start_sntp();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "Phone joined the setup hotspot");
    }
}

// ---- public ----

void wifi_manager_start(void)
{
    s_lock = xSemaphoreCreateMutex();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();

    // Captive-portal URL via DHCP (Android 11+, iOS 14+ open it directly).
    esp_netif_dhcps_stop(ap_netif);
    esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI,
                           (void *)s_portal_url, strlen(s_portal_url));
    esp_netif_dhcps_start(ap_netif);

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));  // credentials live in settings_store

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));

    const esp_timer_create_args_t timers[] = {
        {.callback = retry_timer_cb, .name = "wifi_retry"},
        {.callback = boot_timer_cb, .name = "wifi_boot"},
        {.callback = test_timer_cb, .name = "wifi_test"},
        {.callback = done_timer_cb, .name = "wifi_done"},
        {.callback = idle_timer_cb, .name = "wifi_idle"},
    };
    esp_timer_handle_t *handles[] = {&s_retry_timer, &s_boot_timer, &s_test_timer, &s_done_timer, &s_idle_timer};
    for (int i = 0; i < (int)(sizeof(handles) / sizeof(handles[0])); i++) {
        ESP_ERROR_CHECK(esp_timer_create(&timers[i], handles[i]));
    }

    // Hotspot name from the AP MAC, so two dashboards don't clash.
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_info.ap_ssid, sizeof(s_info.ap_ssid), "EnergyDashboard-%02X%02X", mac[4], mac[5]);
    strlcpy(s_info.ap_url, s_portal_url, sizeof(s_info.ap_url));

    if (settings_has_wifi()) {
        connect_saved();
        esp_timer_start_once(s_boot_timer, BOOT_CONNECT_TIMEOUT_US);
    } else {
        ESP_LOGI(TAG, "No WiFi saved");
        wifi_manager_start_setup();
    }
}

wifi_state_t wifi_manager_get_state(void)
{
    return get_state();
}

void wifi_manager_get_info(wifi_info_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_info;
    xSemaphoreGive(s_lock);
}

void wifi_manager_start_setup(void)
{
    if (in_setup(get_state())) {
        return;
    }
    set_state(WIFI_STATE_SETUP);  // before disconnect: its event is then ignored
    esp_timer_stop(s_retry_timer);
    esp_timer_stop(s_boot_timer);
    if (s_wifi_started) {
        esp_wifi_disconnect();
    }

    // New random password each session; no look-alike characters (0/O, 1/l).
    static const char alphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";
    char pass[AP_PASS_LEN + 1];
    for (int i = 0; i < AP_PASS_LEN; i++) {
        pass[i] = alphabet[esp_random() % (sizeof(alphabet) - 1)];
    }
    pass[AP_PASS_LEN] = '\0';

    wifi_config_t ap = {0};
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(s_info.ap_pass, pass, sizeof(s_info.ap_pass));
    s_info.fail_reason = WIFI_FAIL_NONE;
    s_info.sta_ip[0] = '\0';
    strlcpy((char *)ap.ap.ssid, s_info.ap_ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(s_info.ap_ssid);
    xSemaphoreGive(s_lock);
    strlcpy((char *)ap.ap.password, pass, sizeof(ap.ap.password));
    ap.ap.max_connection = AP_MAX_CLIENTS;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    start_wifi();
    setup_portal_start();
    if (settings_has_wifi()) {
        esp_timer_stop(s_idle_timer);
        esp_timer_start_once(s_idle_timer, SETUP_IDLE_US);
    }

    ESP_LOGI(TAG, "Setup hotspot '%s' password '%s', page %s", s_info.ap_ssid, pass, s_portal_url);
    ESP_LOGI(TAG, "QR: WIFI:S:%s;T:WPA;P:%s;;", s_info.ap_ssid, pass);
}

void wifi_manager_cancel_setup(void)
{
    wifi_state_t state = get_state();
    if ((state != WIFI_STATE_SETUP && state != WIFI_STATE_SETUP_FAILED) || !settings_has_wifi()) {
        return;
    }
    esp_timer_stop(s_idle_timer);
    setup_portal_stop();
    connect_saved();
}

esp_err_t wifi_manager_try_credentials(const char *ssid, const char *pass, const char *token)
{
    wifi_state_t state = get_state();
    if (state != WIFI_STATE_SETUP && state != WIFI_STATE_SETUP_FAILED) {
        return ESP_ERR_INVALID_STATE;
    }
    strlcpy(s_pending_ssid, ssid, sizeof(s_pending_ssid));
    strlcpy(s_pending_pass, pass, sizeof(s_pending_pass));
    strlcpy(s_pending_token, token, sizeof(s_pending_token));
    s_test_attempts = 0;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_info.fail_reason = WIFI_FAIL_NONE;
    xSemaphoreGive(s_lock);
    set_state(WIFI_STATE_SETUP_TESTING);

    ESP_LOGI(TAG, "Testing '%s'", ssid);
    set_sta_config(ssid, pass);
    esp_timer_stop(s_test_timer);
    esp_timer_start_once(s_test_timer, TEST_TIMEOUT_US);
    return esp_wifi_connect();
}

esp_err_t wifi_manager_scan(wifi_ap_record_t *records, uint16_t *count)
{
    wifi_state_t state = get_state();
    if (state != WIFI_STATE_SETUP && state != WIFI_STATE_SETUP_FAILED) {
        *count = 0;
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = esp_wifi_scan_start(NULL, true);
    if (err != ESP_OK) {
        *count = 0;
        return err;
    }
    return esp_wifi_scan_get_ap_records(count, records);
}
