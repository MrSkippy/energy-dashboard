// Setup web page on the setup hotspot (captive portal): WiFi + Tibber token.
//
// Based on ESP-IDF's examples/protocols/http_server/captive_portal: a DNS
// server answers every name with our IP, and unknown URLs (the phones'
// connectivity checks) redirect to the page, so phones open it by themselves.
//
//   GET  /        setup page (web/setup.html, embedded)
//   GET  /scan    nearby networks as JSON
//   POST /save    ssid, password, token (form-urlencoded) -> test + save
//   GET  /status  {"state", "ssid", "reason"} while the page waits

#include "setup_portal.h"
#include "settings_store.h"
#include "wifi_manager.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"   // before dns_server.h: it uses esp_ip4_addr_t without including it
#include "dns_server.h"

#define TAG "setup_portal"

#define SCAN_MAX_RESULTS 20
#define BODY_MAX_LEN     1024

extern const char setup_html_start[] asm("_binary_setup_html_start");

static httpd_handle_t s_server = NULL;
static dns_server_handle_t s_dns = NULL;

static esp_err_t send_json(httpd_req_t *req, cJSON *json)
{
    char *text = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (text == NULL) {
        return httpd_resp_send_500(req);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, text);
    free(text);
    return err;
}

// In place: "+" -> space, "%XX" -> byte.
static void url_decode(char *s)
{
    char *out = s;
    for (char *in = s; *in != '\0'; in++) {
        if (*in == '+') {
            *out++ = ' ';
        } else if (in[0] == '%' && isxdigit((unsigned char)in[1]) && isxdigit((unsigned char)in[2])) {
            char hex[3] = {in[1], in[2], '\0'};
            *out++ = (char)strtol(hex, NULL, 16);
            in += 2;
        } else {
            *out++ = *in;
        }
    }
    *out = '\0';
}

static bool form_field(const char *body, const char *key, char *val, size_t len)
{
    if (httpd_query_key_value(body, key, val, len) != ESP_OK) {
        val[0] = '\0';
        return false;
    }
    url_decode(val);
    return true;
}

static esp_err_t root_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, setup_html_start);
}

static esp_err_t scan_get(httpd_req_t *req)
{
    static wifi_ap_record_t records[SCAN_MAX_RESULTS];  // httpd task stack is small
    uint16_t count = SCAN_MAX_RESULTS;
    wifi_manager_scan(records, &count);

    cJSON *list = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        const char *ssid = (const char *)records[i].ssid;
        if (ssid[0] == '\0') {
            continue;  // hidden network
        }
        cJSON *ap = cJSON_CreateObject();
        cJSON_AddStringToObject(ap, "ssid", ssid);
        cJSON_AddNumberToObject(ap, "rssi", records[i].rssi);
        cJSON_AddBoolToObject(ap, "secure", records[i].authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(list, ap);
    }
    return send_json(req, list);
}

static esp_err_t save_post(httpd_req_t *req)
{
    static char body[BODY_MAX_LEN];
    if (req->content_len >= sizeof(body)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Te lang");
    }
    int received = 0;
    while (received < (int)req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n <= 0) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Ontvangen mislukt");
        }
        received += n;
    }
    body[received] = '\0';

    // Encoded values can be 3x longer than decoded ("%XX").
    static char ssid[SETTINGS_SSID_LEN * 3];
    static char pass[SETTINGS_PASS_LEN * 3];
    static char token[SETTINGS_TOKEN_LEN * 3];
    form_field(body, "ssid", ssid, sizeof(ssid));
    form_field(body, "password", pass, sizeof(pass));
    form_field(body, "token", token, sizeof(token));

    const char *error = NULL;
    size_t pass_len = strlen(pass);
    if (ssid[0] == '\0' || strlen(ssid) >= SETTINGS_SSID_LEN) {
        error = "Kies een WiFi-netwerk.";
    } else if (pass_len != 0 && (pass_len < 8 || pass_len >= SETTINGS_PASS_LEN)) {
        error = "Het wachtwoord moet 8 tot 63 tekens zijn.";
    } else if (strlen(token) >= SETTINGS_TOKEN_LEN) {
        error = "De Tibber-token is te lang.";
    } else if (wifi_manager_try_credentials(ssid, pass, token) != ESP_OK) {
        error = "Bezig, probeer het zo opnieuw.";
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", error == NULL);
    if (error != NULL) {
        cJSON_AddStringToObject(resp, "error", error);
    }
    return send_json(req, resp);
}

static esp_err_t status_get(httpd_req_t *req)
{
    wifi_info_t info;
    wifi_manager_get_info(&info);
    static const char *const reasons[] = {
        [WIFI_FAIL_NONE] = "",
        [WIFI_FAIL_WRONG_PASSWORD] = "password",
        [WIFI_FAIL_NOT_FOUND] = "notfound",
        [WIFI_FAIL_OTHER] = "other",
    };

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "state", wifi_manager_state_name(wifi_manager_get_state()));
    cJSON_AddStringToObject(resp, "ssid", info.sta_ssid);
    cJSON_AddStringToObject(resp, "reason", reasons[info.fail_reason]);
    return send_json(req, resp);
}

// Everything else (incl. the phones' connectivity checks) -> the setup page.
static esp_err_t redirect_404(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    // iOS needs a body to detect the captive portal.
    return httpd_resp_sendstr(req, "Doorsturen naar de instelpagina");
}

esp_err_t setup_portal_start(void)
{
    if (s_server != NULL) {
        return ESP_OK;
    }
    // Redirected junk requests otherwise flood the log.
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_open_sockets = 7;
    cfg.lru_purge_enable = true;
    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        s_server = NULL;
        return err;
    }

    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = root_get},
        {.uri = "/scan", .method = HTTP_GET, .handler = scan_get},
        {.uri = "/save", .method = HTTP_POST, .handler = save_post},
        {.uri = "/status", .method = HTTP_GET, .handler = status_get},
    };
    for (int i = 0; i < (int)(sizeof(routes) / sizeof(routes[0])); i++) {
        httpd_register_uri_handler(s_server, &routes[i]);
    }
    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, redirect_404);

    dns_server_config_t dns_cfg = DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");
    s_dns = start_dns_server(&dns_cfg);

    ESP_LOGI(TAG, "Setup page running");
    return ESP_OK;
}

void setup_portal_stop(void)
{
    if (s_dns != NULL) {
        stop_dns_server(s_dns);
        s_dns = NULL;
    }
    if (s_server != NULL) {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "Setup page stopped");
    }
}
