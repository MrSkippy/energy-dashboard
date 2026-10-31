// Fetches electricity prices from the Tibber GraphQL API.
//
// One request returns today + tomorrow per quarter hour; home_price.c picks the
// current quarter with the clock. So a fetch about every hour is enough, and
// the prices keep working while the internet is down. Tomorrow's prices are
// published around 13:00 and arrive with the next hourly fetch.
//
// priceInfo defaults to HOURLY; resolution: QUARTER_HOURLY gives 96 prices a
// day (https://developer.tibber.com/docs/changelog, "Quarter-hourly prices").
//
// Tibber asks for an identifying User-Agent, and for backoff with jitter on
// errors; no retries on an invalid token. Never log the token.

#include "tibber.h"
#include "settings_store.h"
#include "wifi_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define TAG "tibber"

#define TIBBER_URL   "https://api.tibber.com/v1-beta/gql"
#define TIBBER_QUERY "{\"query\":\"{viewer{homes{currentSubscription{priceInfo(resolution: QUARTER_HOURLY)" \
                     "{today{total startsAt} tomorrow{total startsAt}}}}}}\"}"

#define HTTP_TIMEOUT_MS  15000
#define RESPONSE_MAX     (32 * 1024)  // ~14 KB for today + tomorrow
#define FETCH_INTERVAL_S (60 * 60)
#define FETCH_JITTER_S   (5 * 60)
#define POLL_MS          10000        // token / next fetch check
#define WIFI_POLL_MS     1000
#define TASK_STACK       8192
#define TASK_PRIO        3

static const int s_backoff_s[] = {60, 120, 300, 900};

typedef enum {
    FETCH_OK,
    FETCH_UNAUTHORIZED,
    FETCH_API_ERROR,
    FETCH_NO_CONNECTION,
} fetch_result_t;

static SemaphoreHandle_t s_lock;
static price_table_t s_prices;  // published
static uint32_t s_generation;
static tibber_status_t s_status;

static price_table_t s_fetched;  // task only
static settings_t s_settings;    // task only
static char s_used_token[SETTINGS_TOKEN_LEN];

static void set_state(tibber_state_t state)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.state = state;
    xSemaphoreGive(s_lock);
}

// cJSON makes a node per value (~800 for a response); keep those small
// allocations out of the internal RAM that LVGL's DMA buffers need.
static void *psram_malloc(size_t size)
{
    return heap_caps_malloc_prefer(size, 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_DEFAULT);
}

// GraphQL errors come with HTTP 200 or 400. Logs them; true if there were any.
static bool api_errors(const char *body, bool *unauthenticated)
{
    *unauthenticated = false;
    if (strstr(body, "\"errors\"") == NULL) {
        return false;
    }
    cJSON *root = cJSON_Parse(body);
    const cJSON *errors = cJSON_GetObjectItemCaseSensitive(root, "errors");
    bool any = false;
    const cJSON *e = NULL;
    cJSON_ArrayForEach(e, errors) {
        any = true;
        const cJSON *msg = cJSON_GetObjectItemCaseSensitive(e, "message");
        const cJSON *code = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(e, "extensions"), "code");
        if (cJSON_IsString(code) && strcmp(code->valuestring, "UNAUTHENTICATED") == 0) {
            *unauthenticated = true;
        }
        ESP_LOGW(TAG, "API error: %s", cJSON_IsString(msg) ? msg->valuestring : "?");
    }
    cJSON_Delete(root);
    return any;
}

static fetch_result_t fetch(const char *token, price_table_t *out)
{
    char auth[SETTINGS_TOKEN_LEN + 8];
    snprintf(auth, sizeof(auth), "Bearer %s", token);
    char user_agent[64];
    snprintf(user_agent, sizeof(user_agent), "EnergyDashboard/1.0 esp-idf/%s", esp_get_idf_version());

    esp_http_client_config_t cfg = {
        .url = TIBBER_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .buffer_size = 2048,  // response headers
        .user_agent = user_agent,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return FETCH_NO_CONNECTION;
    }
    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    fetch_result_t result = FETCH_NO_CONNECTION;
    char *body = NULL;
    const int query_len = strlen(TIBBER_QUERY);

    esp_err_t err = esp_http_client_open(client, query_len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Connect failed: %s", esp_err_to_name(err));
        goto done;
    }
    if (esp_http_client_write(client, TIBBER_QUERY, query_len) != query_len ||
        esp_http_client_fetch_headers(client) < 0) {
        ESP_LOGW(TAG, "Request failed");
        goto done;
    }

    int status = esp_http_client_get_status_code(client);
    body = heap_caps_malloc(RESPONSE_MAX, MALLOC_CAP_SPIRAM);
    if (body == NULL) {
        ESP_LOGE(TAG, "No memory for the response");
        goto done;
    }
    int len = 0;
    int n;
    while (len < RESPONSE_MAX - 1 && (n = esp_http_client_read(client, body + len, RESPONSE_MAX - 1 - len)) > 0) {
        len += n;
    }
    body[len] = '\0';
    ESP_LOGI(TAG, "HTTP %d, %d bytes", status, len);

    bool unauthenticated;
    bool errors = api_errors(body, &unauthenticated);
    if (status == 401 || status == 403 || unauthenticated) {
        result = FETCH_UNAUTHORIZED;
    } else if (status == 429 || status >= 500) {
        result = FETCH_NO_CONNECTION;
    } else if (errors || status != 200) {
        result = FETCH_API_ERROR;
    } else if (!price_parse_tibber(body, out)) {
        result = FETCH_API_ERROR;  // logged by the parser
    } else {
        ESP_LOGI(TAG, "%d quarters", out->count);
        result = FETCH_OK;
    }

done:
    free(body);
    esp_http_client_cleanup(client);
    return result;
}

static int64_t seconds_us(int s)
{
    return (int64_t)s * 1000000;
}

static void tibber_task(void *arg)
{
    int64_t next_fetch_us = 0;  // esp_timer time: not affected by SNTP jumps
    int failures = 0;
    bool token_invalid = false;
    tibber_state_t result_state = TIBBER_STATE_FETCHING;  // of the last fetch

    for (;;) {
        if (wifi_manager_get_state() != WIFI_STATE_CONNECTED) {
            if (!token_invalid) {
                set_state(TIBBER_STATE_WAIT_WIFI);
            }
            vTaskDelay(pdMS_TO_TICKS(WIFI_POLL_MS));
            continue;
        }

        settings_load(&s_settings);
        const char *token = s_settings.tibber_token;
        if (token[0] == '\0') {
            set_state(TIBBER_STATE_NO_TOKEN);
            vTaskDelay(pdMS_TO_TICKS(POLL_MS));
            continue;
        }
        if (strcmp(token, s_used_token) != 0) {
            // New token (setup page): fetch right away.
            strlcpy(s_used_token, token, sizeof(s_used_token));
            token_invalid = false;
            failures = 0;
            next_fetch_us = 0;
        }
        if (token_invalid || esp_timer_get_time() < next_fetch_us) {
            set_state(result_state);  // e.g. after WAIT_WIFI
            vTaskDelay(pdMS_TO_TICKS(POLL_MS));
            continue;
        }

        set_state(TIBBER_STATE_FETCHING);
        fetch_result_t result = fetch(token, &s_fetched);
        int delay_s;
        switch (result) {
        case FETCH_OK:
            result_state = TIBBER_STATE_OK;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_prices = s_fetched;
            s_generation++;
            s_status.state = result_state;
            s_status.last_update = time(NULL);
            xSemaphoreGive(s_lock);
            failures = 0;
            delay_s = FETCH_INTERVAL_S + esp_random() % FETCH_JITTER_S;
            break;
        case FETCH_UNAUTHORIZED:
            ESP_LOGW(TAG, "Token rejected; waiting for a new one");
            result_state = TIBBER_STATE_TOKEN_INVALID;
            set_state(result_state);
            token_invalid = true;
            delay_s = 0;
            break;
        default: {
            result_state = result == FETCH_API_ERROR ? TIBBER_STATE_ERROR : TIBBER_STATE_NO_CONNECTION;
            set_state(result_state);
            int max_i = sizeof(s_backoff_s) / sizeof(s_backoff_s[0]) - 1;
            int backoff = s_backoff_s[failures < max_i ? failures : max_i];
            failures++;
            delay_s = backoff / 2 + esp_random() % (backoff / 2);  // jitter
            break;
        }
        }
        next_fetch_us = esp_timer_get_time() + seconds_us(delay_s);
        if (delay_s > 0) {
            ESP_LOGI(TAG, "Next fetch in %d min", delay_s / 60);
        }
    }
}

void tibber_start(void)
{
    cJSON_Hooks hooks = {.malloc_fn = psram_malloc, .free_fn = free};
    cJSON_InitHooks(&hooks);

    s_lock = xSemaphoreCreateMutex();
    xTaskCreate(tibber_task, "tibber", TASK_STACK, NULL, TASK_PRIO, NULL);
}

uint32_t tibber_prices_generation(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t gen = s_generation;
    xSemaphoreGive(s_lock);
    return gen;
}

uint32_t tibber_get_prices(price_table_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t gen = s_generation;
    if (gen > 0) {
        *out = s_prices;
    }
    xSemaphoreGive(s_lock);
    return gen;
}

void tibber_get_status(tibber_status_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_status;
    xSemaphoreGive(s_lock);
}
